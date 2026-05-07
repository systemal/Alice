#pragma once

#include "Plugin/IPlugin.h"
#include "Script/HostBridge.h"

namespace alice {

    /// <summary>
    /// C# 插件适配器 — 把 .NET DLL 桥接为 IPlugin.
    /// OnLoad: 通过 ClrHost 获取 C# Initialize 方法指针, 传入 HostBridge 地址.
    /// C# 侧通过 P/Invoke 解析 HostBridge, 调用 alice.* API.
    ///
    /// 内存约定: C# 回调返回值用 CoTaskMemAlloc, C++ 用 CoTaskMemFree 释放.
    /// </summary>
    class ClrPluginAdapter : public IPlugin {
    public:
        ClrPluginAdapter( const PluginManifest& manifest );

        PluginManifest Manifest( ) const override { return manifest_; }
        void OnLoad( IHostAPI* host ) override;
        void OnUnload( ) override;

    private:
        PluginManifest manifest_;
        HostBridge bridge_{};

        using InitializeFn = void( __cdecl* )( void* );
        using ShutdownFn = void( __cdecl* )( );

        InitializeFn init_fn_ = nullptr;
        ShutdownFn shutdown_fn_ = nullptr;
    };

} // namespace alice
