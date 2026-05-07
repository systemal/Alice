#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <mutex>

namespace alice {

    struct HttpRequest {
        std::string method;
        std::string path;
        std::string body;
        std::unordered_map<std::string, std::string> headers;
        std::unordered_map<std::string, std::string> params;
    };

    struct HttpResponse {
        int status = 200;
        std::string body;
        std::string content_type = "application/json";
    };

    using RouteHandler = std::function<HttpResponse( const HttpRequest& )>;

    /// <summary>
    /// HTTP 路由注册表.
    /// 插件通过 INetAPI::AddRoute 注册, BindToDrogon 时一次性绑定到 Drogon.
    ///
    /// 线程安全: Add/UnregisterAll 加锁. BindToDrogon 也加锁 (虽然只调一次).
    ///
    /// 局限: Drogon 不支持运行时动态删除路由. UnregisterAll 只清除我们的列表,
    /// Drogon 内部的 handler 仍在 (但 lambda 捕获的 handler 已被清除, 调用会空转).
    /// 这是热重载的已知限制.
    ///
    /// TODO: SSE 路由 (POST /api/chat/stream, GET /api/events) 绕过了 RouteRegistry,
    ///       直接在 BuiltinHttpShell 中用 Drogon API 注册. 不够统一但暂时够用.
    /// </summary>
    class RouteRegistry {
    public:
        void Add( const std::string& method, const std::string& path,
                  RouteHandler handler, const PluginID& plugin_id = "" );
        void UnregisterAll( const PluginID& plugin_id );
        void BindToDrogon( );

    private:
        struct Route {
            std::string method;
            std::string path;
            RouteHandler handler;
            PluginID plugin_id;
        };

        mutable std::mutex mutex_;
        std::vector<Route> routes_;
    };

} // namespace alice
