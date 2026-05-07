#pragma once

#include "Plugin/IPlugin.h"

namespace alice::builtin {

    /// <summary>
    /// 内置 HTTP Shell 插件 — Alice 的 REST API 入口.
    /// 注册路由: /api/ping, /api/services, /api/service/call, /api/chat/send
    /// SSE: POST /api/chat/stream (聊天流式), GET /api/events (通用推送)
    /// WS: chat.send handler
    ///
    /// SSE 路由绕过 RouteRegistry, 直接用 Drogon API (需要 async stream).
    /// TODO: 路由列表硬编码在这里, 未来考虑让插件自己注册 SSE 端点.
    /// 注册基础 HTTP API 路由.
    /// </summary>
    class BuiltinHttpShell : public IPlugin {
    public:
        PluginManifest Manifest( ) const override;
        void OnLoad( IHostAPI* host ) override;
        void OnUnload( ) override;

    private:
        IHostAPI* host_ = nullptr;
        void RegisterSseRoutes( IHostAPI* host );
    };

} // namespace alice::builtin
