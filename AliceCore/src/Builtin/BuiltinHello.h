#pragma once

#include "Plugin/IPlugin.h"

namespace alice::builtin {

    /// <summary>
    /// 内置测试插件 — 验证 IPlugin + IHostAPI + EventBus + ServiceRegistry 全链路.
    /// 注册 "hello.greet" 服务 + 监听 "hello.ping" 事件.
    /// 生产环境可以删掉, 不影响任何功能.
    /// 验证整个插件架构: IPlugin + IHostAPI + EventBus + ServiceRegistry.
    /// </summary>
    class BuiltinHello : public IPlugin {
    public:
        PluginManifest Manifest( ) const override;
        void OnLoad( IHostAPI* host ) override;
        void OnUnload( ) override;

    private:
        IHostAPI* host_ = nullptr;
        EventHandle event_handle_ = 0;
    };

} // namespace alice::builtin
