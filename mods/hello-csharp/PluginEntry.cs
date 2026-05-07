using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using Alice.SDK;

namespace HelloCSharp;

public static class PluginEntry
{
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void Initialize(nint hostBridgePtr)
    {
        Alice.SDK.Alice.Initialize(hostBridgePtr);

        Alice.SDK.Alice.Log.Info("========================================");
        Alice.SDK.Alice.Log.Info("  C# 插件测试");
        Alice.SDK.Alice.Log.Info("========================================");

        int passed = 0;
        int failed = 0;

        void Ok(string name) { passed++; Alice.SDK.Alice.Log.Info($"  ✅ {name}"); }
        void Fail(string name, string reason) { failed++; Alice.SDK.Alice.Log.Error($"  ❌ {name}: {reason}"); }
        void Test(string name, Action fn)
        {
            try { fn(); }
            catch (Exception e) { Fail(name, e.Message); }
        }

        // 1. log
        Alice.SDK.Alice.Log.Info("");
        Alice.SDK.Alice.Log.Info("🔹 1. alice.log");
        Test("log", () =>
        {
            Alice.SDK.Alice.Log.Info("  log.info from C#");
            Alice.SDK.Alice.Log.Warn("  log.warn from C#");
            Alice.SDK.Alice.Log.Debug("  log.debug from C#");
            Ok("log (info/warn/debug)");
        });

        // 2. fs
        Alice.SDK.Alice.Log.Info("");
        Alice.SDK.Alice.Log.Info("🔹 2. alice.fs");
        Test("fs.write+read", () =>
        {
            Alice.SDK.Alice.Fs.Write("_test_csharp.txt", "Hello from C#!");
            var content = Alice.SDK.Alice.Fs.Read("_test_csharp.txt");
            if (content != "Hello from C#!") throw new Exception($"read mismatch: {content}");
            Ok($"fs.write+read → {content}");
        });
        Test("fs.exists", () =>
        {
            if (!Alice.SDK.Alice.Fs.Exists("_test_csharp.txt")) throw new Exception("should exist");
            if (Alice.SDK.Alice.Fs.Exists("_no_such_99999.txt")) throw new Exception("should not exist");
            Ok("fs.exists");
        });

        // 3. kv
        Alice.SDK.Alice.Log.Info("");
        Alice.SDK.Alice.Log.Info("🔹 3. alice.kv");
        Test("kv.set+get", () =>
        {
            Alice.SDK.Alice.Kv.Set("cs_test", "\"hello_csharp\"");
            var v = Alice.SDK.Alice.Kv.Get("cs_test");
            if (v == null || !v.Contains("hello_csharp")) throw new Exception($"kv mismatch: {v}");
            Ok($"kv.set+get → {v}");
        });

        // 4. service
        Alice.SDK.Alice.Log.Info("");
        Alice.SDK.Alice.Log.Info("🔹 4. alice.service");
        Test("service.register+call", () =>
        {
            Alice.SDK.Alice.Service.Register("test.csharp.echo", (method, argsJson) =>
            {
                return JsonSerializer.Serialize(new { echo = method, args = argsJson });
            });
            var result = Alice.SDK.Alice.Service.Call("test.csharp.echo", "ping", """{"data":"hello"}""");
            if (!result.Contains("ping")) throw new Exception($"call mismatch: {result}");
            Ok($"service.register+call → {result}");
        });
        Test("service.call builtin", () =>
        {
            var result = Alice.SDK.Alice.Service.Call("hello.greet", "say", """{"name":"C#"}""");
            if (!result.Contains("Hello")) throw new Exception($"builtin fail: {result}");
            Ok($"service.call(builtin) → {result}");
        });

        // 5. event
        Alice.SDK.Alice.Log.Info("");
        Alice.SDK.Alice.Log.Info("🔹 5. alice.event");
        Test("event.emit+on+off", () =>
        {
            bool received = false;
            var handle = Alice.SDK.Alice.Event.On("test.csharp.event", _ => { received = true; });
            Alice.SDK.Alice.Event.Emit("test.csharp.event", """{"test":true}""");
            if (!received) throw new Exception("event not received");
            Alice.SDK.Alice.Event.Off(handle);
            Ok("event.emit+on+off");
        });

        // 6. platform
        Alice.SDK.Alice.Log.Info("");
        Alice.SDK.Alice.Log.Info("🔹 6. alice.platform");
        Test("platform", () =>
        {
            var name = Alice.SDK.Alice.Platform.Name();
            if (name != "Windows") throw new Exception($"not Windows: {name}");
            var dataDir = Alice.SDK.Alice.Platform.DataDir();
            if (string.IsNullOrEmpty(dataDir)) throw new Exception("dataDir empty");
            Ok($"platform.name={name}, dataDir={dataDir}");
        });

        // 7. timer
        Alice.SDK.Alice.Log.Info("");
        Alice.SDK.Alice.Log.Info("🔹 7. alice.timer");
        Test("timer.set+remove", () =>
        {
            var id = Alice.SDK.Alice.Timer.Set("10s", "C# timer test", """{"test":true}""");
            if (string.IsNullOrEmpty(id)) throw new Exception("timer id empty");
            Alice.SDK.Alice.Timer.Remove(id);
            Ok($"timer.set → {id} → remove");
        });

        // 8. net
        Alice.SDK.Alice.Log.Info("");
        Alice.SDK.Alice.Log.Info("🔹 8. alice.net");
        Test("net.fetch", () =>
        {
            var resp = Alice.SDK.Alice.Net.Fetch("https://httpbin.org/get");
            if (resp == null) throw new Exception("fetch returned null");
            if (!resp.Contains("200")) throw new Exception($"status not 200: {resp[..100]}");
            Ok("net.fetch → ok");
        });

        // summary
        Alice.SDK.Alice.Log.Info("");
        Alice.SDK.Alice.Log.Info("========================================");
        Alice.SDK.Alice.Log.Info($"  C# 测试结果: {passed} 通过, {failed} 失败");
        Alice.SDK.Alice.Log.Info("========================================");
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void Shutdown()
    {
        Alice.SDK.Alice.Log.Info("Hello C# 已卸载");
        Alice.SDK.Alice.Cleanup();
    }
}
