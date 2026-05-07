#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>
#include <concepts>
#include <chrono>

namespace alice {

    struct EventData;

    // ══════════════════════════════════════════════════════════════
    // 子接口 — 每个对应 alice.* 的一个命名空间
    // ══════════════════════════════════════════════════════════════

    /// <summary>
    /// 事件总线 API.
    /// 插件间通信的主要通道. 支持同步和异步分发.
    ///
    /// On() 的 callback 接收 const json& — 适合 C++ 插件直接用.
    /// OnRaw() 的 callback 接收 EventData& — 适合需要懒序列化的场景 (HostBridge 用).
    /// </summary>
    using SseEventCallback = std::function<bool( const std::string& event_type, const std::string& event_data )>;

    class IEventAPI {
    public:
        virtual ~IEventAPI( ) = default;
        virtual EventHandle On( const std::string& event_name, std::function<void( const nlohmann::json& )> callback ) = 0;
        virtual EventHandle OnRaw( const std::string& event_name, std::function<void( const EventData& )> callback ) = 0;
        virtual void Emit( const std::string& event_name, const nlohmann::json& data = {}, int ttl = -1 ) = 0;
        virtual void EmitAsync( const std::string& event_name, const nlohmann::json& data = {}, int ttl = -1 ) = 0;
        virtual void Off( EventHandle handle ) = 0;
    };

    /// <summary>
    /// HTTP/WS 网络 API.
    ///
    /// Fetch/FetchStream 是出站请求 (调 LLM API 等).
    /// AddRoute/AddWsHandler 是入站路由注册 (暴露 HTTP/WS 接口).
    ///
    /// TODO: AddRoute 注册的路由是静态的 (BindToDrogon 时一次性注册).
    ///       热重载时旧路由在 Drogon 内部仍存在, 只是 handler 被清除.
    ///       这是 Drogon 的限制, 不影响功能但不够优雅.
    /// </summary>
    using HttpRouteHandler = std::function<nlohmann::json( const nlohmann::json& request )>;
    using WsMessageHandler = std::function<nlohmann::json( const nlohmann::json& request )>;
    using WsStreamMessageHandler = std::function<void( const nlohmann::json& request, std::function<void( const nlohmann::json& )> send, std::function<void( )> done )>;

    class INetAPI {
    public:
        virtual ~INetAPI( ) = default;
        virtual Result<nlohmann::json> Fetch( const std::string& url, const nlohmann::json& opts = nlohmann::json::object( ) ) = 0;
        virtual Result<int> FetchStream( const std::string& url, const nlohmann::json& opts, SseEventCallback on_event ) = 0;
        virtual void AddRoute( const std::string& method, const std::string& path, HttpRouteHandler handler ) = 0;
        virtual void AddWsHandler( const std::string& type, WsMessageHandler handler ) = 0;
        virtual void AddWsStreamHandler( const std::string& type, WsStreamMessageHandler handler ) = 0;
        virtual void WsBroadcast( const std::string& session_id, const nlohmann::json& data ) = 0;
    };

    /// <summary>
    /// 文件 + KV 存储 API.
    /// 文件操作基于 exe 工作目录 (相对路径) 或绝对路径.
    /// KV 操作按 plugin_id 命名空间隔离 — 插件 A 看不到插件 B 的 KV.
    /// </summary>
    class IStorageAPI {
    public:
        virtual ~IStorageAPI( ) = default;
        virtual Result<std::string> ReadFile( const std::string& path ) = 0;
        virtual VoidResult WriteFile( const std::string& path, const std::string& content ) = 0;
        virtual bool FileExists( const std::string& path ) = 0;
        virtual Result<nlohmann::json> ListDir( const std::string& path ) = 0;
        virtual VoidResult RemoveFile( const std::string& path ) = 0;
        virtual Result<nlohmann::json> KvGet( const std::string& key ) = 0;
        virtual VoidResult KvSet( const std::string& key, const nlohmann::json& value ) = 0;
    };

    /// <summary>
    /// 通用服务注册/调用 API.
    /// 替代业务专用 API — Core 不知道 "AI" "聊天" 这些概念,
    /// 只知道 capability 字符串 (如 "ai.chat", "handler.code-exec").
    ///
    /// WaitFor: 阻塞等待服务注册, 用条件变量实现.
    /// 场景: 插件 A 依赖插件 B 的服务, 但加载顺序不确定.
    /// </summary>
    class IServiceAPI {
    public:
        virtual ~IServiceAPI( ) = default;
        virtual VoidResult Register( const std::string& capability, std::function<Result<nlohmann::json>( const std::string&, const nlohmann::json& )> handler, const nlohmann::json& schema = {} ) = 0;
        virtual Result<nlohmann::json> Call( const std::string& capability, const std::string& method, const nlohmann::json& args = {} ) = 0;
        virtual bool WaitFor( const std::string& capability, std::chrono::milliseconds timeout ) = 0;
        virtual std::vector<nlohmann::json> List( ) = 0;
    };

    /// <summary>
    /// 管线 API.
    /// 插件注册阶段 (stage), Core 按 order 顺序执行.
    /// 脚本插件可通过返回 json 的 "_action" 字段控制流程:
    /// "continue" (默认) / "break" / "retry" / "error"
    /// </summary>
    class IPipelineAPI {
    public:
        virtual ~IPipelineAPI( ) = default;
        virtual void RegisterStage( const std::string& pipeline, const std::string& stage, int order, std::function<nlohmann::json( const nlohmann::json&, nlohmann::json& )> handler ) = 0;
        virtual Result<nlohmann::json> Execute( const std::string& pipeline, const nlohmann::json& input ) = 0;
    };

    /// <summary>
    /// 定时器 API.
    /// duration 格式: "5s", "2m", "1h30m", "1d" — 由 HostAPIImpl::ParseDuration 解析.
    /// 到期时通过 EventBus 发布 "timer.expired" 事件.
    /// </summary>
    class IScheduleAPI {
    public:
        virtual ~IScheduleAPI( ) = default;
        virtual TimerID SetTimer( const std::string& duration, const std::string& label, const nlohmann::json& data = {} ) = 0;
        virtual void RemoveTimer( const TimerID& id ) = 0;
        virtual std::vector<nlohmann::json> ListTimers( ) = 0;
    };

    /// <summary>
    /// 日志 API. 自动加 [plugin_id] 前缀.
    /// 底层走 spdlog, 输出到 stdout.
    /// </summary>
    class ILogAPI {
    public:
        virtual ~ILogAPI( ) = default;
        virtual void Info( const std::string& msg ) = 0;
        virtual void Warn( const std::string& msg ) = 0;
        virtual void Error( const std::string& msg ) = 0;
        virtual void Debug( const std::string& msg ) = 0;
    };

    /// <summary>
    /// 平台信息 API.
    /// TODO: 缺少 Exec() — 子进程执行走的是 StdLib::ProcessExec, 不在这个接口里.
    ///       不够优雅, 未来考虑合并进来.
    /// </summary>
    class IPlatformAPI {
    public:
        virtual ~IPlatformAPI( ) = default;
        virtual std::string Name( ) = 0;     ///< "Windows" / "Linux" (TODO: Linux)
        virtual std::string DataDir( ) = 0;   ///< exe 同级 data/ 目录
        virtual std::string ExeDir( ) = 0;    ///< exe 所在目录
    };

    // ══════════════════════════════════════════════════════════════
    // 主接口 — Service Locator 模式
    // ══════════════════════════════════════════════════════════════

    /// <summary>
    /// 宿主 API 主接口 — 插件访问 Core 能力的唯一入口.
    ///
    /// 设计: Service Locator, 不是 God Interface.
    /// 每个子接口 (Event/Net/Storage/...) 独立定义, 按需获取.
    /// 这样 Lua/JS 绑定可以按命名空间映射: alice.event → IEventAPI, alice.net → INetAPI.
    ///
    /// API_VERSION: 用于未来 ABI 兼容性检查.
    /// 插件启动时可以检查: if (host->GetAPIVersion() < 2) { ... }
    /// </summary>
    class IHostAPI {
    public:
        static constexpr int API_VERSION = 1;

        virtual ~IHostAPI( ) = default;

        virtual int GetAPIVersion( ) const { return API_VERSION; }

        virtual IEventAPI* Event( ) = 0;
        virtual INetAPI* Net( ) = 0;
        virtual IStorageAPI* Storage( ) = 0;
        virtual IServiceAPI* Service( ) = 0;
        virtual IPipelineAPI* Pipeline( ) = 0;
        virtual IScheduleAPI* Schedule( ) = 0;
        virtual ILogAPI* Log( ) = 0;
        virtual IPlatformAPI* Platform( ) = 0;

        virtual PluginID GetPluginId( ) const = 0;
        virtual std::string GetPluginDataDir( ) const = 0;
    };

    /// <summary>
    /// 编译期约束: 可作为 ServiceHandler 的 callable.
    /// </summary>
    template<typename F>
    concept ServiceCallable = std::invocable<F, const std::string&, const nlohmann::json&>
        && std::same_as<std::invoke_result_t<F, const std::string&, const nlohmann::json&>, Result<nlohmann::json>>;

} // namespace alice
