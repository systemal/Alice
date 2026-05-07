#pragma once

#include "Runtime/Types.h"
#include <string>

namespace alice {

    struct HttpServerConfig {
        std::string host = "0.0.0.0";
        uint16_t port = 645;       ///< 默认端口. 645 = "Alice" 的某种数字映射, 没什么深意
        int threads = 4;           ///< Drogon IO 线程数. TODO: 改为 0 = auto (CPU 核心数)
    };

    /// <summary>
    /// HTTP/WS 服务器 — Drogon 框架的薄封装.
    ///
    /// Core 只负责启动/关闭. 路由注册通过 RouteRegistry, WS 通过 WsRouter.
    /// Drogon 的路由是在 Run() 前一次性绑定的 (BindToDrogon),
    /// 热重载新增的路由无法动态注册到 Drogon — 这是框架限制.
    ///
    /// TODO: 考虑换成更轻量的 HTTP 库 (如 cpp-httplib) 以支持动态路由.
    ///       但 Drogon 的 WebSocket + 异步 IO 目前不可替代.
    /// </summary>
    class HttpServer {
    public:
        HttpServer( ) = default;
        ~HttpServer( );

        void Configure( const HttpServerConfig& config );
        void Run( );       ///< 阻塞, 进入 Drogon 事件循环
        void Shutdown( );  ///< 调用 drogon::app().quit()

    private:
        HttpServerConfig config_;
    };

} // namespace alice
