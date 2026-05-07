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
    /// TODO: 不支持卸载单个 C# 插件 (需要 AssemblyLoadContext.Unload).
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

    private:
        static void* hostfxr_lib_;
        static void* runtime_context_;
        static void* load_assembly_fn_;

        static VoidResult LoadHostfxr( );
        static VoidResult EnsureRuntimeConfig( const std::filesystem::path& exe_dir );
    };

} // namespace alice
