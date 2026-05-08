#include "pch.h"
#include "Builtin/BuiltinHttpShell.h"
#include "Host/IHostAPI.h"
#include "Runtime/Runtime.h"
#include <drogon/drogon.h>

namespace alice::builtin {

    PluginManifest BuiltinHttpShell::Manifest( ) const {
        return {
            .id = "builtin-http-shell",
            .name = "HTTP Shell",
            .version = "1.0.0",
            .author = "Alice Core",
            .description = "HTTP API 路由",
            .type = "shell",
        };
    }

    void BuiltinHttpShell::OnLoad( IHostAPI* host ) {
        host_ = host;

        // GET /api/ping
        host_->Net( )->AddRoute( "GET", "/api/ping", [host]( const nlohmann::json& ) -> nlohmann::json {
            return { { "status", "ok" }, { "version", "0.3.0" } };
        } );

        // GET /api/plugins
        host_->Net( )->AddRoute( "GET", "/api/plugins", []( const nlohmann::json& ) -> nlohmann::json {
            auto& registry = Runtime::Instance( ).GetPluginRegistry( );
            auto plugins = registry.List( );
            auto arr = nlohmann::json::array( );
            for ( auto& p : plugins ) {
                auto* plugin = registry.Get( p.id );
                auto manifest = plugin ? plugin->Manifest( ) : PluginManifest{};
                arr.push_back( {
                    { "id", p.id },
                    { "name", p.name },
                    { "type", p.type },
                    { "version", p.version },
                    { "runtime", manifest.runtime },
                    { "path", manifest.path.string( ) },
                } );
            }
            return arr;
        } );

        // GET /api/services
        host_->Net( )->AddRoute( "GET", "/api/services", [host]( const nlohmann::json& ) -> nlohmann::json {
            return nlohmann::json( host->Service( )->List( ) );
        } );

        // POST /api/plugin/reload
        host_->Net( )->AddRoute( "POST", "/api/plugin/reload", [host]( const nlohmann::json& req ) -> nlohmann::json {
            nlohmann::json body;
            try { body = nlohmann::json::parse( req.value( "body", "{}" ) ); }
            catch ( ... ) { return { { "error", "invalid JSON" } }; }

            auto id = body.value( "id", "" );
            if ( id.empty( ) ) return { { "error", "missing id" } };

            auto* plugin = Runtime::Instance( ).GetPluginRegistry( ).Get( id );
            if ( !plugin ) return { { "error", "plugin not found" } };

            host->Event( )->Emit( "plugin.reload.request", { { "plugin_id", id } } );
            return { { "status", "ok" }, { "plugin_id", id } };
        } );

        // POST /api/service/call
        host_->Net( )->AddRoute( "POST", "/api/service/call", [host]( const nlohmann::json& req ) -> nlohmann::json {
            nlohmann::json body;
            try { body = nlohmann::json::parse( req.value( "body", "{}" ) ); }
            catch ( ... ) { return { { "status", 400 }, { "body", R"({"error":"无效的 JSON"})" } }; }

            auto capability = body.value( "capability", "" );
            auto method = body.value( "method", "" );
            auto args = body.value( "args", nlohmann::json::object( ) );
            if ( capability.empty( ) )
                return { { "status", 400 }, { "body", R"({"error":"缺少 capability"})" } };

            auto result = host->Service( )->Call( capability, method, args );
            if ( result.has_value( ) ) return *result;
            return { { "status", 500 }, { "body", nlohmann::json{ { "error", result.error( ).message } }.dump( ) } };
        } );

        // POST /api/chat/send — 同步聊天
        host_->Net( )->AddRoute( "POST", "/api/chat/send", [host]( const nlohmann::json& req ) -> nlohmann::json {
            nlohmann::json body;
            try { body = nlohmann::json::parse( req.value( "body", "{}" ) ); }
            catch ( ... ) { return { { "status", 400 }, { "body", R"({"error":"无效的 JSON"})" } }; }

            auto message = body.value( "message", "" );
            if ( message.empty( ) )
                return { { "status", 400 }, { "body", R"({"error":"缺少 message"})" } };

            auto result = host->Service( )->Call( "chat.send", "send", body );
            if ( result.has_value( ) ) return *result;
            return { { "status", 500 }, { "body", nlohmann::json{ { "error", result.error( ).message } }.dump( ) } };
        } );

        // WS handler: chat.send
        host_->Net( )->AddWsHandler( "chat.send", [host]( const nlohmann::json& req ) -> nlohmann::json {
            auto data = req.value( "data", nlohmann::json::object( ) );
            auto result = host->Service( )->Call( "chat.send", "send", data );
            if ( result.has_value( ) ) {
                auto resp = *result;
                resp["type"] = "chat.response";
                return resp;
            }
            return { { "type", "error" }, { "error", result.error( ).message } };
        } );

        // ── SSE: POST /api/chat/stream ──
        RegisterSseRoutes( host );

        host_->Log( )->Info( "HTTP Shell 已注册路由: /api/ping, /api/chat/send, /api/chat/stream, /api/events" );
    }

    void BuiltinHttpShell::RegisterSseRoutes( IHostAPI* host ) {
        auto& app = drogon::app( );

        // POST /api/chat/stream — 聊天流式 SSE
        app.registerHandler( "/api/chat/stream",
            [host]( const drogon::HttpRequestPtr& req,
                     std::function<void( const drogon::HttpResponsePtr& )>&& callback ) {

                nlohmann::json body;
                try { body = nlohmann::json::parse( std::string( req->body( ) ) ); }
                catch ( ... ) {
                    auto resp = drogon::HttpResponse::newHttpResponse( );
                    resp->setStatusCode( drogon::k400BadRequest );
                    resp->setBody( R"({"error":"无效的 JSON"})" );
                    callback( resp );
                    return;
                }

                if ( !body.contains( "message" ) || body["message"].get<std::string>( ).empty( ) ) {
                    auto resp = drogon::HttpResponse::newHttpResponse( );
                    resp->setStatusCode( drogon::k400BadRequest );
                    resp->setBody( R"({"error":"缺少 message"})" );
                    callback( resp );
                    return;
                }

                body["stream"] = true;
                auto session_id = body.value( "session_id", "sse-" + std::to_string(
                    std::chrono::steady_clock::now( ).time_since_epoch( ).count( ) ) );
                body["session_id"] = session_id;

                auto resp = drogon::HttpResponse::newAsyncStreamResponse(
                    [host, body, session_id]( drogon::ResponseStreamPtr stream ) {

                        auto sp = std::shared_ptr<drogon::ResponseStream>( stream.release( ) );
                        auto alive = std::make_shared<std::atomic<bool>>( true );
                        auto handle = std::make_shared<EventHandle>( 0 );

                        *handle = host->Event( )->On( "chat.token",
                            [sp, session_id, alive]( const nlohmann::json& data ) {
                                if ( !*alive ) return;
                                auto sid = data.value( "session_id", "" );
                                if ( sid != session_id ) return;
                                if ( data.contains( "done" ) && data["done"].get<bool>( ) ) return;

                                auto token = data.value( "token", "" );
                                if ( !token.empty( ) ) {
                                    auto sse = "event: token\ndata: " +
                                        nlohmann::json{ { "token", token } }.dump( ) + "\n\n";
                                    if ( !sp->send( sse ) ) *alive = false;
                                }
                            } );

                        std::thread( [host, body, sp, alive, handle]( ) {
                            auto result = host->Service( )->Call( "chat.send", "send", body );
                            host->Event( )->Off( *handle );
                            *alive = false;

                            if ( result ) {
                                sp->send( "event: done\ndata: " + result->dump( ) + "\n\n" );
                            } else {
                                sp->send( "event: error\ndata: " +
                                    nlohmann::json{ { "error", result.error( ).message } }.dump( ) + "\n\n" );
                            }
                            sp->close( );
                        } ).detach( );
                    },
                    true );

                resp->setContentTypeString( "text/event-stream" );
                resp->addHeader( "Cache-Control", "no-cache" );
                resp->addHeader( "X-Accel-Buffering", "no" );
                resp->addHeader( "Access-Control-Allow-Origin", "*" );
                callback( resp );
            },
            { drogon::Post } );

        // GET /api/events?session_id=xxx — 通用事件推送 SSE
        app.registerHandler( "/api/events",
            [host]( const drogon::HttpRequestPtr& req,
                     std::function<void( const drogon::HttpResponsePtr& )>&& callback ) {

                auto session_id = req->getParameter( "session_id" );

                auto resp = drogon::HttpResponse::newAsyncStreamResponse(
                    [host, session_id]( drogon::ResponseStreamPtr stream ) {

                        auto sp = std::shared_ptr<drogon::ResponseStream>( stream.release( ) );
                        auto alive = std::make_shared<std::atomic<bool>>( true );
                        auto handles = std::make_shared<std::vector<EventHandle>>( );

                        auto subscribe = [&]( const std::string& event_name ) {
                            auto h = host->Event( )->On( event_name,
                                [sp, alive, event_name, session_id]( const nlohmann::json& data ) {
                                    if ( !*alive ) return;
                                    if ( !session_id.empty( ) ) {
                                        auto sid = data.value( "session_id", "" );
                                        if ( !sid.empty( ) && sid != session_id ) return;
                                    }
                                    auto sse = "event: " + event_name + "\ndata: " + data.dump( ) + "\n\n";
                                    if ( !sp->send( sse ) ) *alive = false;
                                } );
                            handles->push_back( h );
                        };

                        subscribe( "chat.token" );
                        subscribe( "timer.expired" );
                        subscribe( "plugin.reloaded" );
                        subscribe( "runtime.ready" );
                        subscribe( "runtime.shutdown" );

                        sp->send( "event: connected\ndata: {\"session_id\":\"" + session_id + "\"}\n\n" );

                        // 保活: 每 15 秒发心跳, 检测断连
                        std::thread( [sp, alive, handles, host]( ) {
                            while ( *alive ) {
                                std::this_thread::sleep_for( std::chrono::seconds( 15 ) );
                                if ( !*alive ) break;
                                if ( !sp->send( ": heartbeat\n\n" ) ) {
                                    *alive = false;
                                    break;
                                }
                            }
                            for ( auto h : *handles ) host->Event( )->Off( h );
                            sp->close( );
                        } ).detach( );
                    },
                    true );

                resp->setContentTypeString( "text/event-stream" );
                resp->addHeader( "Cache-Control", "no-cache" );
                resp->addHeader( "X-Accel-Buffering", "no" );
                resp->addHeader( "Access-Control-Allow-Origin", "*" );
                callback( resp );
            },
            { drogon::Get } );
    }

    void BuiltinHttpShell::OnUnload( ) {
        if ( host_ ) host_->Log( )->Info( "HTTP Shell 已卸载" );
        host_ = nullptr;
    }

} // namespace alice::builtin
