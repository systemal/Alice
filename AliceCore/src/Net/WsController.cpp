#include "pch.h"
#include "Net/WsController.h"
#include "Net/WsRouter.h"
#include "Net/WsRegistry.h"
#include "Net/WsTypes.h"
#include "Runtime/Runtime.h"
#include "Plugin/IPlugin.h"
#include "Service/ServiceRegistry.h"
#include <drogon/drogon.h>
#include <drogon/WebSocketController.h>
#include <nlohmann/json.hpp>
#include <atomic>

namespace {

    static std::atomic<uint64_t> g_next_conn_id{ 1 };

    struct WsConnContext {
        uint64_t conn_id;
        std::string session_id = "default";
    };

    class AliceWsController : public drogon::WebSocketController<AliceWsController> {
    public:
        WS_PATH_LIST_BEGIN
        WS_PATH_ADD( "/ws" );
        WS_PATH_LIST_END

        void handleNewConnection(
            [[maybe_unused]] const drogon::HttpRequestPtr& req,
            const drogon::WebSocketConnectionPtr& conn ) override {

            auto ctx = std::make_shared<WsConnContext>( );
            ctx->conn_id = g_next_conn_id.fetch_add( 1 );
            conn->setContext( ctx );

            ALICE_INFO( "WS 连接建立: {} (conn_id={})", conn->peerAddr( ).toIpPort( ), ctx->conn_id );

            // 推送 state.init — Manager 连接后立即获取全量状态
            auto& rt = alice::Runtime::Instance( );
            auto plugins = rt.GetPluginRegistry( ).List( );
            auto services = rt.GetServiceRegistry( ).List( );

            nlohmann::json plugins_arr = nlohmann::json::array( );
            for ( auto& p : plugins ) {
                auto* plugin = rt.GetPluginRegistry( ).Get( p.id );
                auto manifest = plugin ? plugin->Manifest( ) : alice::PluginManifest{};
                plugins_arr.push_back( {
                    { "id", p.id }, { "name", p.name }, { "type", p.type },
                    { "version", p.version }, { "runtime", manifest.runtime },
                } );
            }

            nlohmann::json services_arr = nlohmann::json::array( );
            for ( auto& s : services ) {
                services_arr.push_back( {
                    { "capability", s.capability },
                    { "provider", s.provider_plugin },
                    { "healthy", s.healthy },
                } );
            }

            conn->send( nlohmann::json{
                { "type", "state.init" },
                { "plugins", plugins_arr },
                { "services", services_arr },
            }.dump( ) );
        }

        void handleNewMessage(
            const drogon::WebSocketConnectionPtr& conn,
            std::string&& message,
            const drogon::WebSocketMessageType& type ) override {

            if ( type != drogon::WebSocketMessageType::Text ) return;

            auto ctx = conn->getContext<WsConnContext>( );
            if ( !ctx ) return;

            nlohmann::json msg;
            try { msg = nlohmann::json::parse( message ); }
            catch ( ... ) {
                conn->send( R"({"type":"error","error":"无效的 JSON"})" );
                return;
            }

            // subscribe: 绑定 session_id
            if ( msg.contains( "subscribe" ) ) {
                ctx->session_id = msg[ "subscribe" ].get<std::string>( );
                alice::WsRegistry::Instance( ).Register( ctx->session_id, ctx->conn_id, conn );
                conn->send( nlohmann::json{
                    { "type", "subscribed" },
                    { "session_id", ctx->session_id },
                }.dump( ) );
                ALICE_INFO( "WS 订阅会话: {} (conn_id={})", ctx->session_id, ctx->conn_id );
                return;
            }

            // 普通消息: 走 WsRouter 分发
            auto msg_type = msg.value( "type", "" );
            if ( msg_type.empty( ) ) {
                conn->send( R"({"type":"error","error":"缺少 type 字段"})" );
                return;
            }

            // 如果消息带 session_id, 更新绑定
            if ( msg.contains( "session_id" ) ) {
                auto new_sid = msg[ "session_id" ].get<std::string>( );
                if ( new_sid != ctx->session_id ) {
                    alice::WsRegistry::Instance( ).Unregister( ctx->conn_id );
                    ctx->session_id = new_sid;
                    alice::WsRegistry::Instance( ).Register( ctx->session_id, ctx->conn_id, conn );
                }
            }

            alice::WsRequest ws_req{
                .type = msg_type,
                .request_id = msg.value( "request_id", "" ),
                .data = msg.contains( "data" ) ? msg[ "data" ] : msg,
                .conn_id = ctx->conn_id,
                .session_id = ctx->session_id,
            };

            // 在线程池中处理 (避免阻塞 Drogon IO)
            auto conn_weak = std::weak_ptr( conn );
            alice::WsRouter* router = &alice::Runtime::Instance( ).GetWsRouter( );

            // 直接在当前线程分发 (WsRouter 内部如果需要可以自己丢线程池)
            router->Dispatch( ws_req, [conn_weak]( const std::string& resp ) {
                if ( auto c = conn_weak.lock( ) ) c->send( resp );
            } );
        }

        void handleConnectionClosed( const drogon::WebSocketConnectionPtr& conn ) override {
            auto ctx = conn->getContext<WsConnContext>( );
            if ( ctx ) {
                alice::WsRegistry::Instance( ).Unregister( ctx->conn_id );
                ALICE_INFO( "WS 连接关闭: conn_id={}", ctx->conn_id );
            }
        }
    };

} // anonymous namespace

// 注册函数 (Runtime 调用)
namespace alice {
    void RegisterWsController( ) {
        ALICE_INFO( "WebSocket 路由已注册: /ws" );
    }
}
