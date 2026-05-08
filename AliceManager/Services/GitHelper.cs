using System.Diagnostics;
using System.IO;
using System.Text;

namespace AliceManager.Services;

public class GitHelper
{
    public event Action<string>? OnOutput;

    public async Task<(bool success, string output)> CloneAsync(string url, string targetDir)
    {
        if (Directory.Exists(targetDir))
        {
            var gitDir = Path.Combine(targetDir, ".git");
            if (Directory.Exists(gitDir))
                return (false, $"Directory already exists: {targetDir}");
        }

        return await RunGitAsync($"clone \"{url}\" \"{targetDir}\"");
    }

    public async Task<(bool success, string output)> PullAsync(string repoDir)
    {
        return await RunGitAsync($"-C \"{repoDir}\" pull");
    }

    public string? GetRemoteUrl(string repoDir)
    {
        var gitDir = Path.Combine(repoDir, ".git");
        if (!Directory.Exists(gitDir)) return null;

        var result = RunGitSync($"-C \"{repoDir}\" remote get-url origin");
        return result.success ? result.output.Trim() : null;
    }

    public bool IsGitRepo(string dir)
    {
        return Directory.Exists(Path.Combine(dir, ".git"));
    }

    private async Task<(bool success, string output)> RunGitAsync(string args)
    {
        var sb = new StringBuilder();
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = "git",
                Arguments = args,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8,
            };

            using var proc = Process.Start(psi);
            if (proc == null) return (false, "Failed to start git");

            proc.OutputDataReceived += (_, e) =>
            {
                if (e.Data == null) return;
                sb.AppendLine(e.Data);
                OnOutput?.Invoke(e.Data);
            };
            proc.ErrorDataReceived += (_, e) =>
            {
                if (e.Data == null) return;
                sb.AppendLine(e.Data);
                OnOutput?.Invoke(e.Data);
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

    private static (bool success, string output) RunGitSync(string args)
    {
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = "git",
                Arguments = args,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                CreateNoWindow = true,
            };
            using var proc = Process.Start(psi);
            if (proc == null) return (false, "");
            var output = proc.StandardOutput.ReadToEnd();
            proc.WaitForExit();
            return (proc.ExitCode == 0, output);
        }
        catch
        {
            return (false, "");
        }
    }
}
