#pragma once

#include "Runtime/Types.h"
#include <filesystem>

namespace alice {

    /// <summary>
    /// CLR 宿主 — 在 C++ 进程中托管 .NET Runtime.
    /// 使用 hostfxr API (微软官方 C 托管接口).
    ///
    /// 流程: LoadHostfxr → Initialize (生成 runtimeconfig + 初始化 runtime) →
    ///       LoadAssemblyAndGetEntryPoint (加载 C# DLL, 获取方法指针)
    ///
    /// SDK 发现: alice.runtimeconfig.json 配置 additionalProbingPaths 指向 sdk/,
    ///           但实际用 TPA (Trusted Platform Assemblies) 注入 Alice.SDK.dll.
    ///
    /// 全局单例: .NET Runtime 只初始化一次, 所有 C# 插件共享.
    /// 通过 PluginLoader (C# 侧) 管理 AssemblyLoadContext, 实现插件热重载.
    /// 每个 C# 插件在独立的 collectible ALC 中加载, 可独立卸载.
    /// </summary>
    class ClrHost {
    public:
        static VoidResult Initialize( const std::filesystem::path& exe_dir );
        static void Shutdown( );
        static bool IsInitialized( );

        static Result<void*> LoadAssemblyAndGetEntryPoint(
            const std::filesystem::path& assembly_path,
            const std::wstring& type_name,
            const std::wstring& method_name );

        /// 通过 PluginLoader 在独立 ALC 中加载 C# 插件
        static VoidResult LoadPlugin( const std::string& plugin_id,
                                       const std::filesystem::path& dll_path,
                                       const std::string& entry_type,
                                       void* bridge_ptr );

        /// 通过 PluginLoader 卸载 C# 插件 (ALC.Unload)
        static VoidResult UnloadPlugin( const std::string& plugin_id );

    private:
        static void* hostfxr_lib_;
        static void* runtime_context_;
        static void* load_assembly_fn_;

        using LoadPluginFn = int( __cdecl* )( const char*, const char*, const char*, void* );
        using UnloadPluginFn = int( __cdecl* )( const char* );
        static LoadPluginFn load_plugin_fn_;
        static UnloadPluginFn unload_plugin_fn_;

        static VoidResult LoadHostfxr( );
        static VoidResult EnsureRuntimeConfig( const std::filesystem::path& exe_dir );
    };

} // namespace alice
