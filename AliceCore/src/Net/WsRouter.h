#pragma once

#include "Net/WsTypes.h"
#include <unordered_map>
#include <mutex>

namespace alice {

    /// <summary>
    /// WS 消息路由器 — 根据 type 字段分发到对应 handler.
    ///
    /// 支持两种 handler:
    /// - 一次性 (WsHandler): 收请求 → 返回一个 JSON → 发给客户端
    /// - 流式 (WsStreamHandler): 收请求 → 通过 WsSender 逐条推送 → Done 结束
    ///
    /// 同一个 type 只能注册一个 handler (后者覆盖前者).
    /// TODO: 考虑 "先注册者优先" 策略, 避免热重载时覆盖意外.
    /// </summary>
    class WsRouter {
    public:
        void Handle( const std::string& type, WsHandler handler, const PluginID& plugin_id = "" );
        void HandleStream( const std::string& type, WsStreamHandler handler, const PluginID& plugin_id = "" );
        void UnregisterAll( const PluginID& plugin_id );

        /// <summary>
        /// 分发消息. 由 WsController 调用.
        /// 找不到 handler 时发回 {"type":"error","error":"未知消息类型"}.
        /// </summary>
        void Dispatch( const WsRequest& request, WsSender::SendFunc send_fn );

    private:
        struct HandlerEntry {
            PluginID plugin_id;
            WsHandler handler;
            WsStreamHandler stream_handler;
            bool is_stream = false;
        };

        mutable std::mutex mutex_;
        std::unordered_map<std::string, HandlerEntry> handlers_;
    };

} // namespace alice
