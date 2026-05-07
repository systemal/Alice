#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <cstdint>

namespace alice {

    /// <summary>
    /// WebSocket 请求 — 从客户端 JSON 消息解析出来.
    /// 客户端发: {"type":"chat.send", "data":{...}, "request_id":"xxx"}
    /// WsRouter 根据 type 字段分发到对应 handler.
    /// </summary>
    struct WsRequest {
        std::string type;               ///< 路由 key (如 "chat.send")
        std::string request_id;         ///< 客户端生成, 用于关联异步响应
        nlohmann::json data;
        uint64_t conn_id = 0;           ///< WsController 分配的连接 ID
        SessionID session_id;           ///< 客户端 subscribe 时绑定的会话
    };

    /// <summary>
    /// WS 流式发送器.
    /// 流式 handler 用 Send() 逐条推送, Done() 标记结束.
    /// request_id 自动附加到每条消息, 客户端可以关联是哪个请求的响应.
    /// </summary>
    class WsSender {
    public:
        using SendFunc = std::function<void( const std::string& )>;

        explicit WsSender( uint64_t conn_id, const std::string& request_id, SendFunc send_fn )
            : conn_id_( conn_id ), request_id_( request_id ), send_fn_( std::move( send_fn ) ) {}

        void Send( const nlohmann::json& data ) {
            nlohmann::json msg = data;
            if ( !request_id_.empty( ) ) msg["request_id"] = request_id_;
            send_fn_( msg.dump( ) );
        }

        void Done( ) {
            nlohmann::json msg = { { "type", "done" } };
            if ( !request_id_.empty( ) ) msg["request_id"] = request_id_;
            send_fn_( msg.dump( ) );
        }

        uint64_t ConnId( ) const { return conn_id_; }

    private:
        uint64_t conn_id_;
        std::string request_id_;
        SendFunc send_fn_;
    };

    using WsHandler = std::function<nlohmann::json( const WsRequest& )>;        ///< 一次性响应
    using WsStreamHandler = std::function<void( const WsRequest&, WsSender& )>;  ///< 流式响应

} // namespace alice
