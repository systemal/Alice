#include "pch.h"
#include "Net/RouteRegistry.h"
#include <drogon/drogon.h>

namespace alice {

    void RouteRegistry::Add( const std::string& method, const std::string& path,
                              RouteHandler handler, const PluginID& plugin_id ) {
        std::lock_guard lock( mutex_ );
        routes_.push_back( { method, path, std::move( handler ), plugin_id } );
        ALICE_DEBUG( "路由已注册: {} {} (插件: {})", method, path, plugin_id );
    }

    void RouteRegistry::UnregisterAll( const PluginID& plugin_id ) {
        std::lock_guard lock( mutex_ );
        std::erase_if( routes_, [&]( auto& r ) { return r.plugin_id == plugin_id; } );
    }

    void RouteRegistry::BindToDrogon( ) {
        std::lock_guard lock( mutex_ );
        auto& app = drogon::app( );

        for ( auto& route : routes_ ) {
            drogon::HttpMethod method = drogon::Get;
            if ( route.method == "GET" ) method = drogon::Get;
            else if ( route.method == "POST" ) method = drogon::Post;
            else if ( route.method == "PUT" ) method = drogon::Put;
            else if ( route.method == "DELETE" ) method = drogon::Delete;

            auto handler = route.handler;

            app.registerHandler( route.path,
                [handler]( const drogon::HttpRequestPtr& req,
                           std::function<void( const drogon::HttpResponsePtr& )>&& callback ) {

                    // 转换 Drogon 请求 → 我们的 HttpRequest
                    HttpRequest our_req;
                    our_req.method = req->methodString( );
                    our_req.path = req->path( );
                    our_req.body = std::string( req->body( ) );

                    // 调用插件注册的 handler
                    HttpResponse our_resp;
                    try {
                        our_resp = handler( our_req );
                    }
                    catch ( const std::exception& e ) {
                        our_resp.status = 500;
                        our_resp.body = nlohmann::json{ { "error", e.what( ) } }.dump( );
                    }

                    // 转换回 Drogon 响应
                    auto resp = drogon::HttpResponse::newHttpResponse( );
                    resp->setStatusCode( static_cast<drogon::HttpStatusCode>( our_resp.status ) );
                    resp->setContentTypeString( our_resp.content_type );
                    resp->setBody( our_resp.body );
                    callback( resp );
                },
                { method } );
        }

        ALICE_INFO( "已绑定 {} 条路由到 Drogon", routes_.size( ) );
    }

} // namespace alice
