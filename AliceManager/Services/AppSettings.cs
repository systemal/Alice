using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.Win32;

namespace AliceManager.Services;

public class AppSettings
{
    public bool StartWithWindows { get; set; }
    public bool MinimizeToTray { get; set; } = true;
    public bool AutoConnect { get; set; } = true;
    public bool WatchModsFolder { get; set; } = true;
    public string Host { get; set; } = "localhost";
    public int Port { get; set; } = 645;

    private static readonly string ConfigPath =
        Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "data", "config.json");

    public static AppSettings Load()
    {
        try
        {
            if (!File.Exists(ConfigPath)) return new AppSettings();

            var root = JsonNode.Parse(File.ReadAllText(ConfigPath));
            var mgr = root?["manager"];
            if (mgr == null) return ReadServerDefaults(root);

            var s = new AppSettings
            {
                StartWithWindows = mgr["start_with_windows"]?.GetValue<bool>() ?? false,
                MinimizeToTray = mgr["minimize_to_tray"]?.GetValue<bool>() ?? true,
                AutoConnect = mgr["auto_connect"]?.GetValue<bool>() ?? true,
                WatchModsFolder = mgr["watch_mods_folder"]?.GetValue<bool>() ?? true,
            };

            // server 连接信息从 server 节点读
            var srv = root?["server"];
            if (srv != null)
            {
                s.Host = srv["host"]?.GetValue<string>() ?? "localhost";
                s.Port = srv["port"]?.GetValue<int>() ?? 645;
            }

            return s;
        }
        catch { }
        return new AppSettings();
    }

    private static AppSettings ReadServerDefaults(JsonNode? root)
    {
        var s = new AppSettings();
        var srv = root?["server"];
        if (srv != null)
        {
            s.Host = srv["host"]?.GetValue<string>() ?? "localhost";
            s.Port = srv["port"]?.GetValue<int>() ?? 645;
        }
        return s;
    }

    public void Save()
    {
        try
        {
            var dir = Path.GetDirectoryName(ConfigPath)!;
            Directory.CreateDirectory(dir);

            JsonNode? root = null;
            if (File.Exists(ConfigPath))
            {
                try { root = JsonNode.Parse(File.ReadAllText(ConfigPath)); }
                catch { }
            }
            root ??= new JsonObject();

            // 写 manager 节点
            root["manager"] = new JsonObject
            {
                ["start_with_windows"] = StartWithWindows,
                ["minimize_to_tray"] = MinimizeToTray,
                ["auto_connect"] = AutoConnect,
                ["watch_mods_folder"] = WatchModsFolder,
            };

            // 写 server 节点 (保留已有的其他字段)
            var srv = root["server"]?.AsObject() ?? new JsonObject();
            srv["host"] = Host;
            srv["port"] = Port;
            root["server"] = srv;

            File.WriteAllText(ConfigPath, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        }
        catch { }
    }

    public static void SetStartWithWindows(bool enable)
    {
#if WINDOWS
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Run", true);
            if (key == null) return;

            const string appName = "AliceManager";
            if (enable)
            {
                var exePath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "AliceManager.exe");
                key.SetValue(appName, $"\"{exePath}\"");
            }
            else
            {
                key.DeleteValue(appName, false);
            }
        }
        catch { }
#endif
    }
}
