#pragma once

#include "Plugin/IPlugin.h"
#include "Script/Lua/LuaEngine.h"
#include "Script/QuickJS/QuickJSEngine.h"
#include "Script/HostBridge.h"
#include <memory>

namespace alice {

    /// <summary>
    /// 脚本插件适配器 — 把 Lua/JS 脚本桥接为 IPlugin.
    /// 根据入口文件后缀选择引擎: .lua → LuaEngine, .mjs/.js → QuickJSEngine.
    /// 脚本中定义 onLoad() 和 onUnload() 全局函数, 适配器自动调用.
    ///
    /// 双重访问: 简单调用走 HostBridge (C ABI), 回调注册走 IHostAPI (需要 closure).
    /// 这是因为 C ABI 不支持闭包, 但 sol2/QuickJS 的函数对象需要.
    /// </summary>
    class ScriptPluginAdapter : public IPlugin {
    public:
        ScriptPluginAdapter( const PluginManifest& manifest );

        PluginManifest Manifest( ) const override { return manifest_; }
        void OnLoad( IHostAPI* host ) override;
        void OnUnload( ) override;

    private:
        PluginManifest manifest_;
        std::unique_ptr<LuaEngine> lua_engine_;
        std::unique_ptr<QuickJSEngine> js_engine_;
        HostBridge bridge_{};
    };

} // namespace alice
