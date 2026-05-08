using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.Json;
using AliceManager.Models;

namespace AliceManager.Services;

public partial class ModManager
{
    private readonly string _modsDir;
    private readonly string _modsSrcDir;
    private readonly string _sdkDir;
    private readonly string _sdkDll;
    private readonly GitHelper _git = new();

    public ObservableCollection<ModInfo> Mods { get; } = [];
    public event Action<string, string>? OnLog;

    // Manager 调用: 通知 Server 重载/卸载插件
    public Func<string, Task>? RequestReload;
    public Func<string, Task>? RequestUnload;

    public ModManager(string baseDir)
    {
        _modsDir = Path.Combine(baseDir, "mods");
        _modsSrcDir = Path.Combine(baseDir, "mods-src");
        _sdkDir = Path.Combine(baseDir, "sdk");
        _sdkDll = Path.Combine(_sdkDir, "Alice.SDK.dll");

        Directory.CreateDirectory(_modsDir);
        Directory.CreateDirectory(_modsSrcDir);

        EnsureBuildProps();

        _git.OnOutput += line => OnLog?.Invoke("info", $"[git] {line}");
    }

    // ── Scan ──

    public void Scan()
    {
        Mods.Clear();
        var seen = new HashSet<string>();

        // 扫 mods-src/
        if (Directory.Exists(_modsSrcDir))
        {
            foreach (var dir in Directory.GetDirectories(_modsSrcDir))
            {
                var mod = ReadModFromDir(dir);
                if (mod == null) continue;

                mod.SrcPath = dir;
                mod.Source = _git.IsGitRepo(dir) ? ModSource.Git : ModSource.Local;
                if (mod.Source == ModSource.Git)
                    mod.GitUrl = _git.GetRemoteUrl(dir) ?? "";

                var runtimeDir = Path.Combine(_modsDir, mod.Id);
                mod.RuntimePath = runtimeDir;

                if (Directory.Exists(runtimeDir) && HasEntryFile(runtimeDir, mod))
                    mod.Status = ModStatus.Loaded;
                else
                    mod.Status = mod.IsCSharp ? ModStatus.NotCompiled : ModStatus.Unknown;

                Mods.Add(mod);
                seen.Add(mod.Id);
            }
        }

        // 扫 mods/ (只有运行时, 无源码)
        if (Directory.Exists(_modsDir))
        {
            foreach (var dir in Directory.GetDirectories(_modsDir))
            {
                var mod = ReadModFromDir(dir);
                if (mod == null) continue;
                if (seen.Contains(mod.Id)) continue;

                mod.RuntimePath = dir;
                mod.Source = ModSource.PreBuilt;
                mod.Status = ModStatus.Loaded;
                Mods.Add(mod);
            }
        }
    }

    private static bool HasEntryFile(string dir, ModInfo mod)
    {
        if (mod.IsCSharp)
            return Directory.GetFiles(dir, "*.dll").Any();
        return File.Exists(Path.Combine(dir, "main.lua"))
            || File.Exists(Path.Combine(dir, "main.mjs"))
            || File.Exists(Path.Combine(dir, "main.js"));
    }

    // ── Validate ──

    public static ModValidation Validate(string dir)
    {
        var aliceJson = Path.Combine(dir, "alice.json");
        if (!File.Exists(aliceJson)) return ModValidation.NoAliceJson;

        try
        {
            var json = JsonDocument.Parse(File.ReadAllText(aliceJson));
            var root = json.RootElement;

            if (!root.TryGetProperty("id", out var idProp) || string.IsNullOrEmpty(idProp.GetString()))
                return ModValidation.InvalidAliceJson;

            return ModValidation.Valid;
        }
        catch
        {
            return ModValidation.InvalidAliceJson;
        }
    }

    // ── Install from Git ──

    public async Task<(bool success, string message)> InstallFromGitAsync(string url)
    {
        var repoName = ExtractRepoName(url);
        var srcDir = Path.Combine(_modsSrcDir, repoName);

        OnLog?.Invoke("info", $"Cloning {url}...");
        var (ok, output) = await _git.CloneAsync(url, srcDir);
        if (!ok)
            return (false, $"Clone failed: {output}");

        var validation = Validate(srcDir);
        if (validation != ModValidation.Valid)
        {
            OnLog?.Invoke("error", $"Invalid mod: {validation}");
            return (false, $"Invalid mod: {validation}");
        }

        return await BuildAndDeployAsync(srcDir);
    }

    // ── Install from Local ──

    public async Task<(bool success, string message)> InstallFromLocalAsync(string srcDir)
    {
        var validation = Validate(srcDir);
        if (validation != ModValidation.Valid)
            return (false, $"Invalid mod: {validation}");

        var mod = ReadModFromDir(srcDir);
        if (mod == null) return (false, "Cannot read alice.json");

        // 复制到 mods-src/ (如果不在那里)
        var targetSrc = Path.Combine(_modsSrcDir, mod.Id);
        if (!Path.GetFullPath(srcDir).Equals(Path.GetFullPath(targetSrc), StringComparison.OrdinalIgnoreCase))
        {
            OnLog?.Invoke("info", $"Copying source to mods-src/{mod.Id}/...");
            CopyDirectory(srcDir, targetSrc);
        }

        return await BuildAndDeployAsync(targetSrc);
    }

    // ── Install PreBuilt ──

    public (bool success, string message) InstallPreBuilt(string filePath, string modId)
    {
        if (string.IsNullOrEmpty(modId))
            return (false, "Mod ID is required");

        var ext = Path.GetExtension(filePath).ToLowerInvariant();
        var targetDir = Path.Combine(_modsDir, modId);
        Directory.CreateDirectory(targetDir);

        try
        {
            if (ext == ".dll")
            {
                File.Copy(filePath, Path.Combine(targetDir, Path.GetFileName(filePath)), true);
                var pdb = Path.ChangeExtension(filePath, ".pdb");
                if (File.Exists(pdb))
                    File.Copy(pdb, Path.Combine(targetDir, Path.GetFileName(pdb)), true);
                WriteAliceJson(targetDir, modId, "dotnet", Path.GetFileName(filePath));
            }
            else
            {
                var targetName = ext == ".lua" ? "main.lua" : "main.mjs";
                File.Copy(filePath, Path.Combine(targetDir, targetName), true);
                WriteAliceJson(targetDir, modId, "", "");
            }

            OnLog?.Invoke("info", $"Installed pre-built: {modId}");
            Scan();
            return (true, "OK");
        }
        catch (Exception ex)
        {
            return (false, ex.Message);
        }
    }

    // ── Compile ──

    public async Task<(bool success, string output)> CompileAsync(string modId)
    {
        var mod = Mods.FirstOrDefault(m => m.Id == modId);
        if (mod == null) return (false, "Mod not found");
        if (!mod.HasSource) return (false, "No source");
        return await BuildAndDeployAsync(mod.SrcPath);
    }

    public async Task<(int success, int failed)> CompileAllAsync()
    {
        int ok = 0, fail = 0;
        foreach (var mod in Mods.Where(m => m.CanCompile).ToList())
        {
            var (success, _) = await CompileAsync(mod.Id);
            if (success) ok++; else fail++;
        }
        return (ok, fail);
    }

    // ── Update ──

    public async Task<(bool success, string message)> UpdateAsync(string modId)
    {
        var mod = Mods.FirstOrDefault(m => m.Id == modId);
        if (mod == null) return (false, "Mod not found");
        if (!mod.CanUpdate) return (false, "Not a git mod");

        OnLog?.Invoke("info", $"Updating {modId}...");
        var (ok, output) = await _git.PullAsync(mod.SrcPath);
        if (!ok) return (false, output);

        if (output.Contains("Already up to date"))
        {
            OnLog?.Invoke("info", $"{modId}: Already up to date");
            return (true, "Already up to date");
        }

        return await BuildAndDeployAsync(mod.SrcPath);
    }

    public async Task<(int updated, int failed, int upToDate)> UpdateAllAsync()
    {
        int updated = 0, failed = 0, upToDate = 0;
        foreach (var mod in Mods.Where(m => m.CanUpdate).ToList())
        {
            var (success, msg) = await UpdateAsync(mod.Id);
            if (msg == "Already up to date") upToDate++;
            else if (success) updated++;
            else failed++;
        }
        return (updated, failed, upToDate);
    }

    // ── Remove ──

    public async Task RemoveAsync(string modId)
    {
        var mod = Mods.FirstOrDefault(m => m.Id == modId);

        // 先通知 Server 卸载
        if (RequestUnload != null)
        {
            try { await RequestUnload(modId); }
            catch { }
            await Task.Delay(500);
        }

        // 删运行时目录
        var runtimeDir = mod?.RuntimePath ?? Path.Combine(_modsDir, modId);
        if (Directory.Exists(runtimeDir))
        {
            try { Directory.Delete(runtimeDir, true); }
            catch (Exception ex) { OnLog?.Invoke("error", $"Remove runtime failed: {ex.Message}"); }
        }

        // 删源码目录 (用实际 SrcPath, 不是拼 id)
        var srcDir = mod?.SrcPath;
        if (!string.IsNullOrEmpty(srcDir) && Directory.Exists(srcDir))
        {
            try { Directory.Delete(srcDir, true); }
            catch (Exception ex) { OnLog?.Invoke("error", $"Remove source failed: {ex.Message}"); }
        }

        OnLog?.Invoke("info", $"Removed: {modId}");
        if (mod != null) Mods.Remove(mod);
    }

    // ── Open Folders ──

    public void OpenSourceFolder(string modId)
    {
        var mod = Mods.FirstOrDefault(m => m.Id == modId);
        if (mod?.HasSource == true)
            Process.Start("explorer", mod.SrcPath);
    }

    public void OpenRuntimeFolder(string modId)
    {
        var mod = Mods.FirstOrDefault(m => m.Id == modId);
        if (mod?.HasRuntime == true)
            Process.Start("explorer", mod.RuntimePath);
    }

    public void OpenGitHub(string modId)
    {
        var mod = Mods.FirstOrDefault(m => m.Id == modId);
        if (!string.IsNullOrEmpty(mod?.GitUrl))
            Process.Start(new ProcessStartInfo(mod.GitUrl) { UseShellExecute = true });
    }

    // ── 内部: 编译 + 部署 ──

    private async Task<(bool success, string message)> BuildAndDeployAsync(string srcDir)
    {
        var mod = ReadModFromDir(srcDir);
        if (mod == null) return (false, "Cannot read alice.json");

        var runtimeDir = Path.Combine(_modsDir, mod.Id);

        var csproj = Directory.GetFiles(srcDir, "*.csproj").FirstOrDefault();
        if (csproj != null)
        {
            // alice.json 必须先于 DLL 存在, 否则 FsWatcher 找不到 plugin_id
            Directory.CreateDirectory(runtimeDir);
            File.Copy(Path.Combine(srcDir, "alice.json"), Path.Combine(runtimeDir, "alice.json"), true);

            OnLog?.Invoke("info", $"Compiling {mod.Id}...");
            var (ok, output) = await DotnetBuildAsync(csproj, runtimeDir);
            if (!ok)
            {
                // 编译失败: 清理 (只留 alice.json 没意义)
                try { Directory.Delete(runtimeDir, true); } catch { }
                OnLog?.Invoke("error", $"Compile failed for {mod.Id}");
                Scan();
                return (false, output);
            }

            OnLog?.Invoke("info", $"Compiled {mod.Id} OK");
        }
        else
        {
            // Lua/JS → 复制
            Directory.CreateDirectory(runtimeDir);
            OnLog?.Invoke("info", $"Deploying {mod.Id}...");
            CopyModFiles(srcDir, runtimeDir);
            OnLog?.Invoke("info", $"Deployed {mod.Id}");
        }

        // 显式通知 Server 加载/重载插件
        if (RequestReload != null)
        {
            try { await RequestReload(mod.Id); }
            catch { }
        }

        Scan();
        return (true, "OK");
    }

    private async Task<(bool success, string output)> DotnetBuildAsync(string csproj, string outputDir)
    {
        var sb = new StringBuilder();
        try
        {
            // 用 -p:AliceSdkDll 引用编译好的 DLL, 不依赖 csproj 相对路径
            var args = $"build \"{csproj}\" -c Release -o \"{outputDir}\"";
            if (File.Exists(_sdkDll))
                args += $" -p:AliceSdkDll=\"{_sdkDll}\"";

            var psi = new ProcessStartInfo
            {
                FileName = "dotnet",
                Arguments = args,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8,
            };
            // 强制 dotnet 输出 UTF-8
            psi.Environment["DOTNET_CLI_UI_LANGUAGE"] = "en";

            using var proc = Process.Start(psi);
            if (proc == null) return (false, "Failed to start dotnet");

            proc.OutputDataReceived += (_, e) =>
            {
                if (e.Data == null) return;
                sb.AppendLine(e.Data);
                OnLog?.Invoke("info", $"[build] {e.Data}");
            };
            proc.ErrorDataReceived += (_, e) =>
            {
                if (e.Data == null) return;
                sb.AppendLine(e.Data);
                OnLog?.Invoke("error", $"[build] {e.Data}");
            };

            proc.BeginOutputReadLine();
            proc.BeginErrorReadLine();
            await proc.WaitForExitAsync();

            return (proc.ExitCode == 0, sb.ToString());
        }
        catch (Exception ex)
        {
            return (false, ex.Message);
        }
    }

    // ── 内部: 文件操作 ──

    private static void CopyModFiles(string srcDir, string dstDir)
    {
        var extensions = new HashSet<string> { ".lua", ".mjs", ".js", ".json", ".txt", ".cfg" };
        foreach (var file in Directory.GetFiles(srcDir))
        {
            var ext = Path.GetExtension(file).ToLowerInvariant();
            if (extensions.Contains(ext))
                File.Copy(file, Path.Combine(dstDir, Path.GetFileName(file)), true);
        }
    }

    private static ModInfo? ReadModFromDir(string dir)
    {
        var aliceJson = Path.Combine(dir, "alice.json");
        if (!File.Exists(aliceJson)) return null;

        try
        {
            var text = File.ReadAllText(aliceJson);
            var json = JsonDocument.Parse(text).RootElement;

            var id = json.TryGetProperty("id", out var idProp) ? idProp.GetString() ?? "" : "";
            if (string.IsNullOrEmpty(id)) id = Path.GetFileName(dir);

            var runtime = json.TryGetProperty("runtime", out var rt) ? rt.GetString() ?? "" : "";
            if (string.IsNullOrEmpty(runtime))
            {
                if (Directory.GetFiles(dir, "*.csproj").Any()) runtime = "dotnet";
                else if (File.Exists(Path.Combine(dir, "main.lua"))) runtime = "lua";
                else if (File.Exists(Path.Combine(dir, "main.mjs")) || File.Exists(Path.Combine(dir, "main.js"))) runtime = "js";
            }

            return new ModInfo
            {
                Id = id,
                Name = json.TryGetProperty("name", out var n) ? n.GetString() ?? id : id,
                Type = json.TryGetProperty("type", out var t) ? t.GetString() ?? "plugin" : "plugin",
                Version = json.TryGetProperty("version", out var v) ? v.GetString() ?? "0.0.0" : "0.0.0",
                Runtime = runtime,
                Description = json.TryGetProperty("description", out var d) ? d.GetString() ?? "" : "",
                EntryFile = json.TryGetProperty("entry_file", out var ef) ? ef.GetString() ?? "" : "",
                EntryType = json.TryGetProperty("entry_type", out var et) ? et.GetString() ?? "" : "",
            };
        }
        catch { return null; }
    }

    private static void WriteAliceJson(string dir, string id, string runtime, string entryFile)
    {
        var json = new Dictionary<string, string> { ["id"] = id, ["name"] = id, ["version"] = "1.0.0", ["type"] = "plugin" };
        if (!string.IsNullOrEmpty(runtime)) json["runtime"] = runtime;
        if (!string.IsNullOrEmpty(entryFile)) json["entry_file"] = entryFile;
        File.WriteAllText(Path.Combine(dir, "alice.json"),
            JsonSerializer.Serialize(json, new JsonSerializerOptions { WriteIndented = true }));
    }

    private static string ExtractRepoName(string url)
    {
        var name = url.TrimEnd('/');
        if (name.EndsWith(".git")) name = name[..^4];
        var slash = name.LastIndexOf('/');
        return slash >= 0 ? name[(slash + 1)..] : name;
    }

    private static void CopyDirectory(string src, string dst)
    {
        Directory.CreateDirectory(dst);
        foreach (var file in Directory.GetFiles(src))
            File.Copy(file, Path.Combine(dst, Path.GetFileName(file)), true);
        foreach (var dir in Directory.GetDirectories(src))
        {
            var dirName = Path.GetFileName(dir);
            if (dirName is ".git" or "bin" or "obj" or ".vs") continue;
            CopyDirectory(dir, Path.Combine(dst, dirName));
        }
    }
}
