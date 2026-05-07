#pragma once

#include "Runtime/Types.h"
#include <drogon/WebSocketConnection.h>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace alice {

    /// <summary>
    /// WS 连接注册表 — session_id → 连接列表.
    /// 用于主动推送 (Broadcast/Send).
    ///
    /// 弱引用: 存 weak_ptr, 连接断开后 lock() 返回 nullptr, 自动清理.
    ///
    /// 全局单例, 因为 WsController (Drogon 框架管理) 和 BuiltinHttpShell 都要用.
    /// TODO: 单例不优雅, 未来移到 Runtime 持有, 通过 IHostAPI 访问.
    /// </summary>
    class WsRegistry {
    public:
        void Register( const SessionID& session_id, uint64_t conn_id,
                        const drogon::WebSocketConnectionPtr& conn );
        void Unregister( uint64_t conn_id );
        void Broadcast( const SessionID& session_id, const std::string& message );
        void Send( uint64_t conn_id, const std::string& message );
        size_t ClientCount( const SessionID& session_id ) const;

        static WsRegistry& Instance( );

    private:
        struct Entry {
            SessionID session_id;
            uint64_t conn_id;
            std::weak_ptr<drogon::WebSocketConnection> conn;
        };

        mutable std::mutex mutex_;
        std::vector<Entry> entries_;

        WsRegistry( ) = default;
    };

} // namespace alice
