#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>

namespace alice {

    /// <summary>
    /// 服务处理函数. 接收 method + args, 返回 Result JSON.
    /// 用法: handler("chat", {"messages":[...]}) → {"reply":"..."}
    /// </summary>
    using ServiceHandler = std::function<Result<nlohmann::json>(
        const std::string& method,
        const nlohmann::json& args )>;

    /// <summary>
    /// 已注册服务的信息.
    /// healthy + consecutive_failures 用于健康跟踪:
    /// 连续 5 次异常 → 标记 unhealthy. 成功一次 → 恢复.
    /// TODO: unhealthy 状态目前只做标记, 没有自动熔断或告警.
    /// </summary>
    struct ServiceInfo {
        std::string capability;
        PluginID provider_plugin;
        nlohmann::json schema;
        bool healthy = true;
        int consecutive_failures = 0;
    };

    /// <summary>
    /// 透明哈希 — 让 unordered_map 支持 string_view 查找.
    /// 避免每次 Call() 时把 string_view 转成 string 再查表.
    /// C++23 特性: 需要 is_transparent + equal_to<> 配合.
    /// </summary>
    struct TransparentHash {
        using is_transparent = void;
        size_t operator( )( std::string_view sv ) const { return std::hash<std::string_view>{}( sv ); }
    };

    /// <summary>
    /// 通用服务注册表.
    /// Core 不知道 "AI" "聊天" 这些业务概念, 只知道 capability 字符串.
    /// 插件自己约定: "ai.chat" 是 AI 服务, "handler.code-exec" 是代码执行.
    ///
    /// 线程安全: 所有方法加锁. Call 在调用 handler 时释放锁 (handler 可能耗时且可能调其他服务).
    ///
    /// 覆盖注册: 同一插件可以重复 Register 同一个 capability (热重载场景).
    ///           不同插件注册同名 capability 会返回 AlreadyExists.
    ///
    /// WaitFor: 条件变量等待, 不是轮询. Register 后 notify_all 唤醒所有等待者.
    /// </summary>
    class ServiceRegistry {
    public:
        VoidResult Register( std::string_view capability,
                              const PluginID& plugin_id,
                              ServiceHandler handler,
                              const nlohmann::json& schema = {} );

        void UnregisterAll( const PluginID& plugin_id );

        Result<nlohmann::json> Call( std::string_view capability,
                                      std::string_view method,
                                      const nlohmann::json& args = {} );

        bool Has( std::string_view capability ) const;

        /// <summary>
        /// 阻塞等待指定服务注册. 超时返回 false.
        /// 场景: chat-pipeline 在 onLoad 时等 handler 服务就绪.
        /// </summary>
        bool WaitFor( std::string_view capability, std::chrono::milliseconds timeout );

        std::vector<ServiceInfo> List( ) const;
        std::vector<ServiceInfo> ListByPrefix( std::string_view prefix ) const;

    private:
        struct Entry {
            ServiceInfo info;
            ServiceHandler handler;
        };

        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::unordered_map<std::string, Entry, TransparentHash, std::equal_to<>> services_;
    };

} // namespace alice
