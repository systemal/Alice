#pragma once

#include "Plugin/IPlugin.h"
#include "Script/HostBridge.h"

namespace alice {

    /// <summary>
    /// C# 插件适配器 — 把 .NET DLL 桥接为 IPlugin.
    /// 通过 PluginLoader (C# 侧) 在独立 AssemblyLoadContext 中加载插件,
    /// 支持热重载: 卸载旧 ALC → 创建新 ALC → 加载新 DLL.
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
    };

} // namespace alice
