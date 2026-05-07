#include "pch.h"
#include "Net/HttpServer.h"
#include <drogon/drogon.h>

namespace alice {

    HttpServer::~HttpServer( ) = default;

    void HttpServer::Configure( const HttpServerConfig& config ) {
        config_ = config;

        auto& app = drogon::app( );
        app.addListener( config_.host, config_.port );
        app.setThreadNum( config_.threads );
        app.disableSigtermHandling( );
        app.setLogLevel( trantor::Logger::kWarn );

        ALICE_INFO( "HTTP 服务器已配置: {}:{} ({} 线程)",
                   config_.host, config_.port, config_.threads );
    }

    void HttpServer::Run( ) {
        ALICE_INFO( "HTTP 服务器运行中..." );
        drogon::app( ).run( );
    }

    void HttpServer::Shutdown( ) {
        ALICE_INFO( "HTTP 服务器正在停止..." );
        drogon::app( ).quit( );
    }

} // namespace alice
