using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Alice.SDK;

/// <summary>
/// 插件加载器 — 用独立 AssemblyLoadContext 实现 C# 插件热重载.
///
/// 每个插件一个 collectible ALC, 类型隔离, 可独立卸载.
/// Alice.SDK.dll 在每个 ALC 中独立加载 (不共享默认 ALC 的副本),
/// 保证各插件的 Alice 静态状态 (_b, _pinnedDelegates) 互不干扰.
///
/// C++ 侧通过 hostfxr 获取 LoadPlugin/UnloadPlugin 的函数指针后直接调用.
/// </summary>
public static unsafe class PluginLoader
{
    private static readonly Dictionary<string, PluginContext> _loaded = new();
    private static string? _sdkDir;

    private record PluginContext(
        PluginAssemblyLoadContext Alc,
        WeakReference AlcRef,
        Action? ShutdownFn);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int LoadPlugin(byte* pluginIdPtr, byte* dllPathPtr, byte* entryTypePtr, nint bridgePtr)
    {
        try
        {
            var pluginId = Marshal.PtrToStringUTF8((nint)pluginIdPtr) ?? "";
            var dllPath = Marshal.PtrToStringUTF8((nint)dllPathPtr) ?? "";
            var entryType = Marshal.PtrToStringUTF8((nint)entryTypePtr) ?? "";

            if (string.IsNullOrEmpty(pluginId) || string.IsNullOrEmpty(dllPath))
                return -1;

            if (_sdkDir == null)
            {
                var sdkAssembly = typeof(PluginLoader).Assembly.Location;
                _sdkDir = Path.GetDirectoryName(sdkAssembly) ?? "";
            }

            if (_loaded.ContainsKey(pluginId))
                UnloadPluginInternal(pluginId);

            // 复制 DLL 到临时目录, 避免文件锁阻止重新编译
            var tempDir = Path.Combine(Path.GetTempPath(), "alice-plugins", pluginId, Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(tempDir);

            var sourceDir = Path.GetDirectoryName(dllPath)!;
            var dllName = Path.GetFileName(dllPath);
            var tempDll = Path.Combine(tempDir, dllName);

            // 复制插件 DLL 及其依赖 (同目录下的所有 .dll 和 .pdb)
            foreach (var file in Directory.GetFiles(sourceDir))
            {
                var ext = Path.GetExtension(file).ToLowerInvariant();
                if (ext is ".dll" or ".pdb")
                {
                    var destFile = Path.Combine(tempDir, Path.GetFileName(file));
                    File.Copy(file, destFile, overwrite: true);
                }
            }

            // 复制 SDK DLL 到临时目录 (确保 ALC 加载独立副本)
            if (!string.IsNullOrEmpty(_sdkDir))
            {
                var sdkDll = Path.Combine(_sdkDir, "Alice.SDK.dll");
                if (File.Exists(sdkDll))
                {
                    var destSdk = Path.Combine(tempDir, "Alice.SDK.dll");
                    if (!File.Exists(destSdk))
                        File.Copy(sdkDll, destSdk, overwrite: true);
                }
            }

            var alc = new PluginAssemblyLoadContext(pluginId, tempDir);
            var alcRef = new WeakReference(alc);

            var assembly = alc.LoadFromAssemblyPath(tempDll);

            // 解析入口类型: "Namespace.Class, AssemblyName"
            var typeName = entryType;
            var commaIdx = entryType.IndexOf(',');
            if (commaIdx > 0)
                typeName = entryType[..commaIdx].Trim();

            var type = assembly.GetType(typeName);
            if (type == null)
                return -2;

            // 获取 Initialize(nint) 函数指针并直接调用
            // 不能用反射 Invoke — UnmanagedCallersOnly 方法禁止从 managed 代码调用
            var initMethod = type.GetMethod("Initialize", BindingFlags.Public | BindingFlags.Static);
            if (initMethod == null)
                return -3;

            RuntimeHelpers.PrepareMethod(initMethod.MethodHandle);
            var initPtr = initMethod.MethodHandle.GetFunctionPointer();
            ((delegate* unmanaged[Cdecl]<nint, void>)initPtr)(bridgePtr);

            // 获取 Shutdown() 函数指针 (可选)
            Action? shutdownFn = null;
            var shutdownMethod = type.GetMethod("Shutdown", BindingFlags.Public | BindingFlags.Static);
            if (shutdownMethod != null)
            {
                RuntimeHelpers.PrepareMethod(shutdownMethod.MethodHandle);
                var shutdownPtr = shutdownMethod.MethodHandle.GetFunctionPointer();
                shutdownFn = () => ((delegate* unmanaged[Cdecl]<void>)shutdownPtr)();
            }

            _loaded[pluginId] = new PluginContext(alc, alcRef, shutdownFn);
            return 0;
        }
        catch
        {
            return -99;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int UnloadPlugin(byte* pluginIdPtr)
    {
        try
        {
            var pluginId = Marshal.PtrToStringUTF8((nint)pluginIdPtr) ?? "";
            if (string.IsNullOrEmpty(pluginId))
                return -1;

            return UnloadPluginInternal(pluginId) ? 0 : -2;
        }
        catch
        {
            return -99;
        }
    }

    private static bool UnloadPluginInternal(string pluginId)
    {
        if (!_loaded.TryGetValue(pluginId, out var ctx))
            return false;

        // 调用插件的 Shutdown
        try { ctx.ShutdownFn?.Invoke(); } catch { /* 插件 Shutdown 异常不阻止卸载 */ }

        var tempDir = ctx.Alc.TempDir;

        ctx.Alc.Unload();
        _loaded.Remove(pluginId);

        // 促进 GC 回收 ALC
        for (int i = 0; i < 3 && ctx.AlcRef.IsAlive; i++)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }

        // 清理临时目录
        try
        {
            if (!string.IsNullOrEmpty(tempDir) && Directory.Exists(tempDir))
                Directory.Delete(tempDir, recursive: true);
        }
        catch { /* 文件可能还被锁, 忽略 */ }

        return true;
    }

    /// <summary>
    /// 可回收的插件 ALC.
    /// 优先从临时目录加载 DLL (含独立的 Alice.SDK.dll 副本),
    /// 未找到时 fallback 到默认 ALC.
    /// </summary>
    private class PluginAssemblyLoadContext : AssemblyLoadContext
    {
        public string TempDir { get; }

        public PluginAssemblyLoadContext(string name, string tempDir)
            : base(name, isCollectible: true)
        {
            TempDir = tempDir;
        }

        protected override Assembly? Load(AssemblyName assemblyName)
        {
            var candidate = Path.Combine(TempDir, assemblyName.Name + ".dll");
            if (File.Exists(candidate))
                return LoadFromAssemblyPath(candidate);

            return null;
        }
    }
}
