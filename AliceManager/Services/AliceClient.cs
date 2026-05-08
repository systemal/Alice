using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Net.Http;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;

namespace AliceManager.Services;

public class PluginInfo
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Type { get; set; } = "";
    public string Version { get; set; } = "";
    public string Runtime { get; set; } = "";
    public string Path { get; set; } = "";
    public bool Enabled { get; set; } = true;
    public string Location => Path;
}

public class ServiceInfo
{
    public string Capability { get; set; } = "";
    public string Provider { get; set; } = "";
    public bool Healthy { get; set; } = true;
}

public class AliceClient : IDisposable
{
    private ClientWebSocket? _ws;
    private NamedPipeClientStream? _pipe;
    private StreamReader? _pipeReader;
    private StreamWriter? _pipeWriter;
    private CancellationTokenSource? _cts;
    private readonly string _host;
    private readonly int _port;
    private bool _managed;
    private Process? _serverProcess;

    public event Action<List<PluginInfo>, List<ServiceInfo>>? OnStateInit;
    public event Action<PluginInfo>? OnPluginLoaded;
    public event Action<string>? OnPluginUnloaded;
    public event Action<string, string, string, string>? OnLog;  // level, source, plugin, message
    public event Action<bool>? OnConnectionChanged;

    public bool IsConnected => _managed ? _pipe?.IsConnected == true : _ws?.State == WebSocketState.Open;
    public bool IsManaged => _managed;

    public AliceClient(string host = "localhost", int port = 645)
    {
        _host = host;
        _port = port;
    }

    public static bool IsServerRunning()
    {
        try
        {
            using var mutex = Mutex.OpenExisting("Alice_Server_Instance");
            return true;
        }
        catch (WaitHandleCannotBeOpenedException)
        {
            return false;
        }
    }

    public bool TryLaunchServer()
    {
        var exeDir = AppDomain.CurrentDomain.BaseDirectory;
        var serverExe = System.IO.Path.Combine(exeDir, "AliceServer.exe");

        if (!System.IO.File.Exists(serverExe))
            return false;

        try
        {
            _serverProcess = Process.Start(new ProcessStartInfo
            {
                FileName = serverExe,
                Arguments = "--managed",
                WorkingDirectory = exeDir,
                UseShellExecute = false,
                CreateNoWindow = true,
            });
            _managed = true;
            return true;
        }
        catch
        {
            return false;
        }
    }

    public async Task<bool> ConnectAsync(int maxRetries = 10, int retryDelayMs = 500)
    {
        _cts = new CancellationTokenSource();

        if (_managed)
            return await ConnectPipeAsync(maxRetries, retryDelayMs);

        return await ConnectWsAsync(maxRetries, retryDelayMs);
    }

    private async Task<bool> ConnectPipeAsync(int maxRetries, int retryDelayMs)
    {
        for (int i = 0; i < maxRetries; i++)
        {
            try
            {
                _pipe = new NamedPipeClientStream(".", "Alice_IPC", PipeDirection.InOut, PipeOptions.Asynchronous);
                await _pipe.ConnectAsync(retryDelayMs, _cts!.Token);
                _pipeReader = new StreamReader(_pipe, Encoding.UTF8);
                _pipeWriter = new StreamWriter(_pipe, Encoding.UTF8) { AutoFlush = true };
                OnConnectionChanged?.Invoke(true);
                _ = Task.Run(() => PipeReceiveLoop(_cts.Token));
                return true;
            }
            catch
            {
                _pipe?.Dispose();
                _pipe = null;
                if (i < maxRetries - 1)
                    await Task.Delay(retryDelayMs);
            }
        }

        OnConnectionChanged?.Invoke(false);
        return false;
    }

    private async Task<bool> ConnectWsAsync(int maxRetries, int retryDelayMs)
    {
        for (int i = 0; i < maxRetries; i++)
        {
            try
            {
                _ws = new ClientWebSocket();
                await _ws.ConnectAsync(new Uri($"ws://{_host}:{_port}/ws"), _cts!.Token);
                OnConnectionChanged?.Invoke(true);
                _ = Task.Run(() => WsReceiveLoop(_cts.Token));
                return true;
            }
            catch
            {
                _ws?.Dispose();
                _ws = null;
                if (i < maxRetries - 1)
                    await Task.Delay(retryDelayMs);
            }
        }

        OnConnectionChanged?.Invoke(false);
        return false;
    }

    public async Task SendAsync(string type, object? data = null)
    {
        var msg = JsonSerializer.Serialize(new { type, data });

        if (_managed && _pipeWriter != null)
        {
            await _pipeWriter.WriteLineAsync(msg);
            return;
        }

        if (_ws?.State == WebSocketState.Open)
        {
            var bytes = Encoding.UTF8.GetBytes(msg);
            await _ws.SendAsync(bytes, WebSocketMessageType.Text, true, _cts?.Token ?? default);
        }
    }

    public async Task SendShutdownAsync()
    {
        await SendAsync("shutdown");
    }

    public void SendShutdownCommand()
    {
        try
        {
            if (_managed && _pipeWriter != null && _pipe?.IsConnected == true)
            {
                _pipeWriter.WriteLine(JsonSerializer.Serialize(new { type = "shutdown" }));
                _pipeWriter.Flush();
            }
        }
        catch { }
    }

    public bool WaitForServerExit(int timeoutMs = 5000)
    {
        if (_serverProcess == null || _serverProcess.HasExited) return true;
        return _serverProcess.WaitForExit(timeoutMs);
    }

    public void ForceKillServer()
    {
        if (_serverProcess != null && !_serverProcess.HasExited)
        {
            try { _serverProcess.Kill(entireProcessTree: true); }
            catch { }
        }
        _serverProcess = null;
    }

    public void ShutdownServer()
    {
        SendShutdownCommand();
        if (!WaitForServerExit(5000))
            ForceKillServer();
    }

    public async Task UnloadPluginAsync(string pluginId)
    {
        if (_managed && _pipeWriter != null)
        {
            await SendAsync("plugin.unload", new { id = pluginId });
            return;
        }

        // HTTP fallback (TODO: add /api/plugin/unload endpoint)
    }

    public async Task ReloadPluginAsync(string pluginId)
    {
        if (_managed && _pipeWriter != null)
        {
            await SendAsync("plugin.reload", new { id = pluginId });
            return;
        }

        using var http = new HttpClient();
        var json = JsonSerializer.Serialize(new { id = pluginId });
        var content = new StringContent(json, Encoding.UTF8, "application/json");
        await http.PostAsync($"http://{_host}:{_port}/api/plugin/reload", content);
    }

    private async Task PipeReceiveLoop(CancellationToken ct)
    {
        try
        {
            while (!ct.IsCancellationRequested && _pipe?.IsConnected == true)
            {
                var line = await _pipeReader!.ReadLineAsync(ct);
                if (line == null) break;
                HandleMessage(line);
            }
        }
        catch (OperationCanceledException) { }
        catch { }

        OnConnectionChanged?.Invoke(false);
    }

    private async Task WsReceiveLoop(CancellationToken ct)
    {
        var buffer = new byte[8192];
        var sb = new StringBuilder();

        try
        {
            while (!ct.IsCancellationRequested && _ws?.State == WebSocketState.Open)
            {
                sb.Clear();
                WebSocketReceiveResult result;

                do
                {
                    result = await _ws.ReceiveAsync(buffer, ct);
                    if (result.MessageType == WebSocketMessageType.Close)
                    {
                        OnConnectionChanged?.Invoke(false);
                        return;
                    }
                    sb.Append(Encoding.UTF8.GetString(buffer, 0, result.Count));
                } while (!result.EndOfMessage);

                HandleMessage(sb.ToString());
            }
        }
        catch (OperationCanceledException) { }
        catch
        {
            OnConnectionChanged?.Invoke(false);
        }
    }

    private void HandleMessage(string raw)
    {
        try
        {
            using var doc = JsonDocument.Parse(raw);
            var root = doc.RootElement;
            var type = root.GetProperty("type").GetString() ?? "";

            switch (type)
            {
                case "state.init":
                    var plugins = ParsePlugins(root.GetProperty("plugins"));
                    var services = ParseServices(root.GetProperty("services"));
                    OnStateInit?.Invoke(plugins, services);
                    break;

                case "plugin.loaded":
                case "plugin.reloaded":
                    if (root.TryGetProperty("data", out var data))
                    {
                        OnPluginLoaded?.Invoke(new PluginInfo
                        {
                            Id = data.TryGetProperty("id", out var pid) ? pid.GetString() ?? "" : "",
                            Name = data.TryGetProperty("name", out var n) ? n.GetString() ?? "" : "",
                            Version = data.TryGetProperty("version", out var v) ? v.GetString() ?? "" : "",
                        });
                    }
                    break;

                case "plugin.unloaded":
                    if (root.TryGetProperty("data", out var ud))
                    {
                        var uid = ud.TryGetProperty("plugin_id", out var upid) ? upid.GetString() ?? "" : "";
                        OnPluginUnloaded?.Invoke(uid);
                    }
                    break;

                case "log":
                    var level = root.TryGetProperty("level", out var lv) ? lv.GetString() ?? "info" : "info";
                    var source = root.TryGetProperty("source", out var src) ? src.GetString() ?? "" : "";
                    var plugin = root.TryGetProperty("plugin", out var plg) ? plg.GetString() ?? "" : "";
                    var message = root.TryGetProperty("message", out var msg) ? msg.GetString() ?? "" : "";
                    OnLog?.Invoke(level, source, plugin, message);
                    break;
            }
        }
        catch { }
    }

    private static List<PluginInfo> ParsePlugins(JsonElement arr)
    {
        var list = new List<PluginInfo>();
        foreach (var el in arr.EnumerateArray())
        {
            list.Add(new PluginInfo
            {
                Id = el.TryGetProperty("id", out var id) ? id.GetString() ?? "" : "",
                Name = el.TryGetProperty("name", out var name) ? name.GetString() ?? "" : "",
                Type = el.TryGetProperty("type", out var t) ? t.GetString() ?? "" : "",
                Version = el.TryGetProperty("version", out var ver) ? ver.GetString() ?? "" : "",
                Runtime = el.TryGetProperty("runtime", out var rt) ? rt.GetString() ?? "" : "",
            });
        }
        return list;
    }

    private static List<ServiceInfo> ParseServices(JsonElement arr)
    {
        var list = new List<ServiceInfo>();
        foreach (var el in arr.EnumerateArray())
        {
            list.Add(new ServiceInfo
            {
                Capability = el.TryGetProperty("capability", out var c) ? c.GetString() ?? "" : "",
                Provider = el.TryGetProperty("provider", out var p) ? p.GetString() ?? "" : "",
                Healthy = !el.TryGetProperty("healthy", out var h) || h.GetBoolean(),
            });
        }
        return list;
    }

    public void Reset()
    {
        _cts?.Cancel();
        _pipeWriter?.Dispose();
        _pipeReader?.Dispose();
        _pipe?.Dispose();
        _ws?.Dispose();
        _cts?.Dispose();
        _pipeWriter = null;
        _pipeReader = null;
        _pipe = null;
        _ws = null;
        _cts = null;
        _managed = false;
        _serverProcess = null;
    }

    public void Dispose()
    {
        ShutdownServer();
        Reset();
    }
}
