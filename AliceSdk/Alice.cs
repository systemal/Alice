using System.Runtime.InteropServices;
using System.Text;

namespace Alice.SDK;

/// <summary>
/// Alice 平台 API 高级封装.
/// C# 插件通过此类访问所有 alice.* 能力.
/// </summary>
public static unsafe class Alice
{
    private static NativeBridge _b;
    private static readonly List<GCHandle> _pinnedDelegates = [];

    public static void Initialize(nint bridgePtr)
    {
        _b = Marshal.PtrToStructure<NativeBridge>(bridgePtr);
    }

    public static void Cleanup()
    {
        foreach (var h in _pinnedDelegates) h.Free();
        _pinnedDelegates.Clear();
    }

    // ── 内部工具 ──

    internal static byte* ToUtf8(string s)
    {
        var bytes = Encoding.UTF8.GetBytes(s + '\0');
        var ptr = (byte*)NativeMemory.Alloc((nuint)bytes.Length);
        Marshal.Copy(bytes, 0, (nint)ptr, bytes.Length);
        return ptr;
    }

    internal static string FromUtf8(byte* ptr)
    {
        if (ptr == null) return "";
        return Marshal.PtrToStringUTF8((nint)ptr) ?? "";
    }

    internal static string FromUtf8AndFree(byte* ptr)
    {
        if (ptr == null) return "";
        var s = Marshal.PtrToStringUTF8((nint)ptr) ?? "";
        _b.free_string(ptr);
        return s;
    }

    internal static void FreeUtf8(byte* ptr)
    {
        if (ptr != null) NativeMemory.Free(ptr);
    }

    // ── alice.log ──

    public static class Log
    {
        public static void Info(string msg) { var p = ToUtf8(msg); _b.log_info(_b.ctx, p); FreeUtf8(p); }
        public static void Warn(string msg) { var p = ToUtf8(msg); _b.log_warn(_b.ctx, p); FreeUtf8(p); }
        public static void Error(string msg) { var p = ToUtf8(msg); _b.log_error(_b.ctx, p); FreeUtf8(p); }
        public static void Debug(string msg) { var p = ToUtf8(msg); _b.log_debug(_b.ctx, p); FreeUtf8(p); }
    }

    // ── alice.fs ──

    public static class Fs
    {
        public static string? Read(string path)
        {
            var p = ToUtf8(path);
            var result = _b.storage_read(_b.ctx, p);
            FreeUtf8(p);
            if (result == null) return null;
            return FromUtf8AndFree(result);
        }

        public static bool Write(string path, string content)
        {
            var pp = ToUtf8(path);
            var pc = ToUtf8(content);
            var r = _b.storage_write(_b.ctx, pp, pc);
            FreeUtf8(pp); FreeUtf8(pc);
            return r == 0;
        }

        public static bool Exists(string path)
        {
            var p = ToUtf8(path);
            var r = _b.storage_exists(_b.ctx, p);
            FreeUtf8(p);
            return r != 0;
        }
    }

    // ── alice.kv ──

    public static class Kv
    {
        public static string? Get(string key)
        {
            var p = ToUtf8(key);
            var result = _b.kv_get(_b.ctx, p);
            FreeUtf8(p);
            if (result == null) return null;
            return FromUtf8AndFree(result);
        }

        public static void Set(string key, string valueJson)
        {
            var pk = ToUtf8(key);
            var pv = ToUtf8(valueJson);
            _b.kv_set(_b.ctx, pk, pv);
            FreeUtf8(pk); FreeUtf8(pv);
        }
    }

    // ── alice.service ──

    public static class Service
    {
        public static string Call(string capability, string method, string argsJson = "{}")
        {
            var pc = ToUtf8(capability);
            var pm = ToUtf8(method);
            var pa = ToUtf8(argsJson);
            var result = _b.service_call(_b.ctx, pc, pm, pa);
            FreeUtf8(pc); FreeUtf8(pm); FreeUtf8(pa);
            if (result == null) return "null";
            return FromUtf8AndFree(result);
        }

        public static void Register(string capability, Func<string, string, string> handler)
        {
            // 创建 unmanaged callback wrapper
            var wrapper = new ServiceCallbackWrapper(handler);
            var gcHandle = GCHandle.Alloc(wrapper);
            _pinnedDelegates.Add(gcHandle);

            var pc = ToUtf8(capability);
            _b.service_register(_b.ctx, pc, (nint)gcHandle, &ServiceCallbackTrampoline);
            FreeUtf8(pc);
        }

        [UnmanagedCallersOnly(CallConvs = [typeof(System.Runtime.CompilerServices.CallConvCdecl)])]
        private static byte* ServiceCallbackTrampoline(nint handlerCtx, byte* method, byte* argsJson)
        {
            var wrapper = (ServiceCallbackWrapper)GCHandle.FromIntPtr(handlerCtx).Target!;
            var m = FromUtf8(method);
            var a = FromUtf8(argsJson);
            var result = wrapper.Handler(m, a);
            return (byte*)Marshal.StringToCoTaskMemUTF8(result);
        }

        public static bool WaitFor(string capability, int timeoutMs = 5000)
        {
            var pc = ToUtf8(capability);
            var result = _b.service_wait_for(_b.ctx, pc, timeoutMs);
            FreeUtf8(pc);
            return result != 0;
        }

        public static string List()
        {
            return "[]";
        }
    }

    private class ServiceCallbackWrapper(Func<string, string, string> handler)
    {
        public Func<string, string, string> Handler { get; } = handler;
    }

    // ── alice.event ──

    public static class Event
    {
        public static ulong On(string eventName, Action<string> callback)
        {
            var wrapper = new EventCallbackWrapper(callback);
            var gcHandle = GCHandle.Alloc(wrapper);
            _pinnedDelegates.Add(gcHandle);

            var pn = ToUtf8(eventName);
            var handle = _b.event_on(_b.ctx, pn, (nint)gcHandle, &EventCallbackTrampoline);
            FreeUtf8(pn);
            return handle;
        }

        [UnmanagedCallersOnly(CallConvs = [typeof(System.Runtime.CompilerServices.CallConvCdecl)])]
        private static void EventCallbackTrampoline(nint callbackCtx, byte* dataJson)
        {
            var wrapper = (EventCallbackWrapper)GCHandle.FromIntPtr(callbackCtx).Target!;
            wrapper.Callback(FromUtf8(dataJson));
        }

        public static void Emit(string eventName, string dataJson = "{}", int ttl = -1)
        {
            var pn = ToUtf8(eventName);
            var pd = ToUtf8(dataJson);
            _b.event_emit(_b.ctx, pn, pd, ttl);
            FreeUtf8(pn); FreeUtf8(pd);
        }

        public static void EmitAsync(string eventName, string dataJson = "{}", int ttl = -1)
        {
            var pn = ToUtf8(eventName);
            var pd = ToUtf8(dataJson);
            _b.event_emit_async(_b.ctx, pn, pd, ttl);
            FreeUtf8(pn); FreeUtf8(pd);
        }

        public static void Off(ulong handle)
        {
            _b.event_off(_b.ctx, handle);
        }
    }

    private class EventCallbackWrapper(Action<string> callback)
    {
        public Action<string> Callback { get; } = callback;
    }

    // ── alice.net ──

    public static class Net
    {
        public static string? Fetch(string url, string optsJson = "{}")
        {
            var pu = ToUtf8(url);
            var po = ToUtf8(optsJson);
            var result = _b.net_fetch(_b.ctx, pu, po);
            FreeUtf8(pu); FreeUtf8(po);
            if (result == null) return null;
            return FromUtf8AndFree(result);
        }
    }

    // ── alice.timer ──

    public static class Timer
    {
        public static string Set(string duration, string label, string dataJson = "{}")
        {
            var pd = ToUtf8(duration);
            var pl = ToUtf8(label);
            var pj = ToUtf8(dataJson);
            var result = _b.timer_set(_b.ctx, pd, pl, pj);
            FreeUtf8(pd); FreeUtf8(pl); FreeUtf8(pj);
            if (result == null) return "";
            return FromUtf8AndFree(result);
        }

        public static void Remove(string id)
        {
            var p = ToUtf8(id);
            _b.timer_remove(_b.ctx, p);
            FreeUtf8(p);
        }

        public static string List()
        {
            var result = _b.timer_list(_b.ctx);
            if (result == null) return "[]";
            return FromUtf8AndFree(result);
        }
    }

    // ── alice.platform ──

    public static class Platform
    {
        public static string Name()
        {
            var result = _b.platform_name(_b.ctx);
            return FromUtf8(result);
        }

        public static string DataDir()
        {
            var result = _b.platform_data_dir(_b.ctx);
            if (result == null) return "";
            return FromUtf8AndFree(result);
        }

        public static string ExeDir()
        {
            var result = _b.platform_exe_dir(_b.ctx);
            if (result == null) return "";
            return FromUtf8AndFree(result);
        }
    }

    // ── alice.script ──

    public static class Script
    {
        public static string Eval(string lang, string code)
        {
            var pl = ToUtf8(lang);
            var pc = ToUtf8(code);
            var result = _b.script_eval(_b.ctx, pl, pc);
            FreeUtf8(pl); FreeUtf8(pc);
            if (result == null) return """{"success":false,"error":"eval failed"}""";
            return FromUtf8AndFree(result);
        }
    }
}
