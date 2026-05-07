#include "pch.h"
#include "Net/WsRouter.h"

namespace alice {

    void WsRouter::Handle( const std::string& type, WsHandler handler, const PluginID& plugin_id ) {
        std::lock_guard lock( mutex_ );
        handlers_[type] = { .plugin_id = plugin_id, .handler = std::move( handler ), .is_stream = false };
        ALICE_DEBUG( "WS handler 已注册: {} (插件: {})", type, plugin_id );
    }

    void WsRouter::HandleStream( const std::string& type, WsStreamHandler handler, const PluginID& plugin_id ) {
        std::lock_guard lock( mutex_ );
        handlers_[type] = { .plugin_id = plugin_id, .stream_handler = std::move( handler ), .is_stream = true };
        ALICE_DEBUG( "WS stream handler 已注册: {} (插件: {})", type, plugin_id );
    }

    void WsRouter::UnregisterAll( const PluginID& plugin_id ) {
        std::lock_guard lock( mutex_ );
        std::erase_if( handlers_, [&]( auto& pair ) { return pair.second.plugin_id == plugin_id; } );
    }

    void WsRouter::Dispatch( const WsRequest& request, WsSender::SendFunc send_fn ) {
        HandlerEntry entry;
        {
            std::lock_guard lock( mutex_ );
            auto it = handlers_.find( request.type );
            if ( it == handlers_.end( ) ) {
                nlohmann::json err = { { "type", "error" }, { "error", "未知消息类型: " + request.type } };
                if ( !request.request_id.empty( ) ) err["request_id"] = request.request_id;
                send_fn( err.dump( ) );
                return;
            }
            entry = it->second;
        }

        try {
            if ( entry.is_stream ) {
                WsSender sender( request.conn_id, request.request_id, send_fn );
                entry.stream_handler( request, sender );
            }
            else {
                auto result = entry.handler( request );
                if ( !request.request_id.empty( ) ) result["request_id"] = request.request_id;
                send_fn( result.dump( ) );
            }
        }
        catch ( const std::exception& e ) {
            nlohmann::json err = { { "type", "error" }, { "error", e.what( ) } };
            if ( !request.request_id.empty( ) ) err["request_id"] = request.request_id;
            send_fn( err.dump( ) );
        }
    }

} // namespace alice
