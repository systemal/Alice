#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <unordered_map>

namespace alice {

    /// <summary>
    /// 出站 HTTP 客户端 — 调 LLM API、webhook 等外部服务.
    ///
    /// 平台实现:
    /// - Windows: WinHTTP API (同步 + 流式都有)
    /// - Linux: TODO (需要 libcurl)
    ///
    /// 为什么不用 Drogon 的 HttpClient?
    /// 因为 Drogon 的客户端是异步的, 和我们的同步 HostBridge 模型不匹配.
    /// WinHTTP 的同步模式正好适合: 阻塞等待完整响应, 或边读边回调.
    ///
    /// 句柄管理: 用 WinHttpHandle RAII 包装, 任何分支退出都能正确释放.
    /// </summary>
    class HttpClient {
    public:
        struct Request {
            std::string url;
            std::string method = "GET";
            std::string body;
            std::unordered_map<std::string, std::string> headers;
        };

        struct Response {
            int status_code = 0;
            std::string body;
        };

        /// <summary>
        /// 流式回调. 每收到一块数据调一次. 返回 false 中止请求.
        /// chunk 可能不完整 (TCP 分片), 调用方需要自己拼接.
        /// </summary>
        using StreamCallback = std::function<bool( std::string_view chunk )>;

        static Result<int> StreamRequest( const Request& request, StreamCallback on_data );
        static Result<Response> Send( const Request& request );

        /// <summary>
        /// 便捷 JSON Fetch. opts 格式: {"method":"POST","headers":{...},"body":"..."}
        /// 返回: {"status":200,"body":"..."}
        /// </summary>
        static Result<nlohmann::json> Fetch( const std::string& url, const nlohmann::json& opts = nlohmann::json::object( ) );
    };

} // namespace alice
