#pragma once

#include "Host/IHostAPI.h"

namespace alice {

    class Runtime;

    /// <summary>
    /// IHostAPI 的默认实现 — 每个插件一个实例.
    /// 所有调用委托给 Runtime 内部模块. 插件 ID 用于:
    /// - KV 命名空间隔离 (插件 A 看不到插件 B 的数据)
    /// - 日志前缀 ([plugin_id] msg)
    /// - 服务注册归属 (卸载时自动清理)
    /// - 路由/WS handler 归属
    ///
    /// 继承所有子接口 (IEventAPI, INetAPI...) 是为了 Service Locator 返回 this.
    /// 不够优雅但简单有效, 避免了额外的包装类.
    /// 每个插件拿到一个独立实例, 携带该插件的 ID 和权限.
    /// 所有调用委托给 Runtime 内部的对应模块.
    /// </summary>
    class HostAPIImpl : public IHostAPI,
                        public IEventAPI,
                        public INetAPI,
                        public IStorageAPI,
                        public IServiceAPI,
                        public IPipelineAPI,
                        public IScheduleAPI,
                        public ILogAPI,
                        public IPlatformAPI {
    public:
        HostAPIImpl( const PluginID& plugin_id, Runtime& runtime );

        // IHostAPI (Service Locator)
        IEventAPI* Event( ) override { return this; }
        INetAPI* Net( ) override { return this; }
        IStorageAPI* Storage( ) override { return this; }
        IServiceAPI* Service( ) override { return this; }
        IPipelineAPI* Pipeline( ) override { return this; }
        IScheduleAPI* Schedule( ) override { return this; }
        ILogAPI* Log( ) override { return this; }
        IPlatformAPI* Platform( ) override { return this; }
        PluginID GetPluginId( ) const override { return plugin_id_; }
        std::string GetPluginDataDir( ) const override;

        // IEventAPI
        EventHandle On( const std::string& event_name, std::function<void( const nlohmann::json& )> callback ) override;
        EventHandle OnRaw( const std::string& event_name, std::function<void( const EventData& )> callback ) override;
        void Emit( const std::string& event_name, const nlohmann::json& data, int ttl ) override;
        void EmitAsync( const std::string& event_name, const nlohmann::json& data, int ttl ) override;
        void Off( EventHandle handle ) override;

        // INetAPI
        Result<nlohmann::json> Fetch( const std::string& url, const nlohmann::json& opts ) override;
        Result<int> FetchStream( const std::string& url, const nlohmann::json& opts, SseEventCallback on_event ) override;
        void AddRoute( const std::string& method, const std::string& path, HttpRouteHandler handler ) override;
        void AddWsHandler( const std::string& type, WsMessageHandler handler ) override;
        void AddWsStreamHandler( const std::string& type, WsStreamMessageHandler handler ) override;
        void WsBroadcast( const std::string& session_id, const nlohmann::json& data ) override;

        // IStorageAPI
        Result<std::string> ReadFile( const std::string& path ) override;
        VoidResult WriteFile( const std::string& path, const std::string& content ) override;
        bool FileExists( const std::string& path ) override;
        Result<nlohmann::json> ListDir( const std::string& path ) override;
        VoidResult RemoveFile( const std::string& path ) override;
        Result<nlohmann::json> KvGet( const std::string& key ) override;
        VoidResult KvSet( const std::string& key, const nlohmann::json& value ) override;

        // IServiceAPI
        VoidResult Register( const std::string& capability, std::function<Result<nlohmann::json>( const std::string&, const nlohmann::json& )> handler, const nlohmann::json& schema ) override;
        Result<nlohmann::json> Call( const std::string& capability, const std::string& method, const nlohmann::json& args ) override;
        bool WaitFor( const std::string& capability, std::chrono::milliseconds timeout ) override;
        std::vector<nlohmann::json> List( ) override;

        // IPipelineAPI
        void RegisterStage( const std::string& pipeline, const std::string& stage, int order, std::function<nlohmann::json( const nlohmann::json&, nlohmann::json& )> handler ) override;
        Result<nlohmann::json> Execute( const std::string& pipeline, const nlohmann::json& input ) override;

        // IScheduleAPI
        TimerID SetTimer( const std::string& duration, const std::string& label, const nlohmann::json& data ) override;
        void RemoveTimer( const TimerID& id ) override;
        std::vector<nlohmann::json> ListTimers( ) override;

        // ILogAPI
        void Info( const std::string& msg ) override;
        void Warn( const std::string& msg ) override;
        void Error( const std::string& msg ) override;
        void Debug( const std::string& msg ) override;

        // IPlatformAPI
        std::string Name( ) override;
        std::string DataDir( ) override;
        std::string ExeDir( ) override;

    private:
        PluginID plugin_id_;
        Runtime& runtime_;

        /// <summary>
        /// 解析时间字符串 ("5s", "2m", "1h30m")
        /// </summary>
        static std::chrono::seconds ParseDuration( std::string_view str );
    };

} // namespace alice
