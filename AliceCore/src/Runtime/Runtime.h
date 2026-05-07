#pragma once

#include "Runtime/Types.h"
#include "Storage/Config.h"
#include "Storage/KvStore.h"
#include "Schedule/ThreadPool.h"
#include "Schedule/Timer.h"
#include "Trace/Trace.h"
#include "Platform/Platform.h"
#include "Event/EventBus.h"
#include "Service/ServiceRegistry.h"
#include "Pipeline/Pipeline.h"
#include "Plugin/PluginRegistry.h"
#include "Host/HostAPIImpl.h"
#include "Net/HttpServer.h"
#include "Net/RouteRegistry.h"
#include "Net/WsRouter.h"
#include <memory>
#include <filesystem>

namespace alice {

    class RuntimePool;

    /// <summary>
    /// Alice 运行时 — 整个平台的入口.
    /// 持有所有 Core 模块, 管理生命周期.
    ///
    /// 设计原则: Runtime 不包含任何业务逻辑. 它只是一个"容器",
    /// 把各个模块组装起来, 然后让插件去定义一切业务行为.
    /// 验收标准: 删掉所有插件, Runtime 还能启动 (只是什么都不干).
    ///
    /// 初始化顺序:
    /// Config → KvStore → ThreadPool → TimerScheduler → EventBus →
    /// ServiceRegistry → PipelineEngine → BuiltinPlugins → ScriptPlugins → HttpServer
    ///
    /// TODO: 初始化顺序是隐式的 (代码顺序), 未来考虑显式依赖图.
    /// TODO: 单例模式不够优雅, 但目前只有一个 Runtime 实例, 够用.
    /// </summary>
    class Runtime {
    public:
        Runtime( ) = default;
        ~Runtime( );

        Runtime( const Runtime& ) = delete;
        Runtime& operator=( const Runtime& ) = delete;

        /// <summary>
        /// 初始化所有模块, 扫描并加载插件.
        /// 插件按 alice.json 中的 dependencies 字段拓扑排序后加载.
        /// 单个插件 OnLoad 崩溃会被隔离, 不影响其他插件.
        /// </summary>
        VoidResult Initialize( const std::filesystem::path& config_path = {} );

        /// <summary>
        /// 启动 HTTP 服务器, 进入事件循环 (阻塞).
        /// Ctrl+C 或调用 Shutdown() 退出.
        /// </summary>
        void Run( );

        /// <summary>
        /// 优雅关闭: FsWatcher → 所有插件 OnUnload → CLR → ThreadPool → HttpServer.
        /// </summary>
        void Shutdown( );

        /// <summary>
        /// 全局单例. TODO: 不优雅, 但 HostAPIImpl/FsWatcher 回调需要访问 Runtime.
        /// 未来考虑依赖注入或 Context 对象替代.
        /// </summary>
        static Runtime& Instance( );

        // ── 模块访问 (仅 Core 内部使用, 插件通过 IHostAPI) ──
        Config& GetConfig( ) { return config_; }
        KvStore& GetKvStore( ) { return *kv_store_; }
        ThreadPool& GetThreadPool( ) { return *thread_pool_; }
        TimerScheduler& GetTimerScheduler( ) { return timer_scheduler_; }
        TraceLog& GetTraceLog( ) { return trace_log_; }
        EventBus& GetEventBus( ) { return event_bus_; }
        ServiceRegistry& GetServiceRegistry( ) { return service_registry_; }
        PipelineEngine& GetPipelineEngine( ) { return pipeline_engine_; }
        PluginRegistry& GetPluginRegistry( ) { return plugin_registry_; }
        HttpServer& GetHttpServer( ) { return http_server_; }
        RouteRegistry& GetRouteRegistry( ) { return route_registry_; }
        WsRouter& GetWsRouter( ) { return ws_router_; }

        const std::filesystem::path& DataDir( ) const { return data_dir_; }
        const std::filesystem::path& ExeDir( ) const { return exe_dir_; }

    private:
        static Runtime* instance_;

        std::filesystem::path exe_dir_;
        std::filesystem::path data_dir_;

        // 模块 (按依赖顺序声明, 析构时逆序销毁)
        Config config_;
        std::unique_ptr<KvStore> kv_store_;
        std::unique_ptr<ThreadPool> thread_pool_;
        TimerScheduler timer_scheduler_;
        TraceLog trace_log_;
        EventBus event_bus_;
        ServiceRegistry service_registry_;
        PipelineEngine pipeline_engine_;
        PluginRegistry plugin_registry_;
        HttpServer http_server_;
        RouteRegistry route_registry_;
        WsRouter ws_router_;
        std::unique_ptr<platform::IFsWatcher> fs_watcher_;

        /// <summary>
        /// 每个插件一个 HostAPIImpl 实例, 按 plugin_id 索引.
        /// 这样每个插件的 KV 命名空间、日志前缀、权限都是隔离的.
        /// </summary>
        std::unordered_map<PluginID, std::unique_ptr<HostAPIImpl>> host_apis_;

        void LoadBuiltinPlugin( std::unique_ptr<IPlugin> plugin );
        void ReloadPlugin( const PluginID& plugin_id );
        void StartFsWatcher( );
    };

} // namespace alice
