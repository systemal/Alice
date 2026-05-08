#include "pch.h"
#include "Runtime/Runtime.h"
#include "Platform/Platform.h"
#include "Storage/FileStorage.h"
#include "Builtin/BuiltinHello.h"
#include "Builtin/BuiltinHttpShell.h"
#include "Script/ScriptPluginAdapter.h"
#include "Script/CLR/ClrHost.h"
#include "Script/CLR/ClrPluginAdapter.h"
#include "Net/WsController.h"
#include <iostream>
#include <queue>

namespace alice {

    Runtime* Runtime::instance_ = nullptr;

    Runtime& Runtime::Instance( ) { return *instance_; }

    Runtime::~Runtime( ) = default;

    VoidResult Runtime::Initialize( const std::filesystem::path& config_path ) {
        instance_ = this;

        // 基准路径
        exe_dir_ = platform::GetExecutableDir( );
        data_dir_ = platform::GetDataDir( );
        (void)FileStorage::EnsureDirectory( data_dir_ );

        ALICE_INFO( "Alice Runtime 初始化" );
        ALICE_INFO( "  可执行文件目录: {}", exe_dir_.string( ) );
        ALICE_INFO( "  数据目录: {}", data_dir_.string( ) );

        // 配置
        auto cfg_path = config_path.empty( ) ? ( data_dir_ / "config.json" ) : config_path;
        auto config_result = config_.Load( cfg_path );
        if ( !config_result ) return config_result;

        // KvStore
        kv_store_ = std::make_unique<KvStore>( data_dir_ / "kv" );

        // 线程池
        auto pool_size = config_.Get<int>( "runtime.thread_pool", 0 );
        thread_pool_ = std::make_unique<ThreadPool>( static_cast<size_t>( pool_size ) );
        ALICE_INFO( "  线程池: {} 个工作线程", thread_pool_->ThreadCount( ) );

        // 定时器到期 → 通过 EventBus 发布事件
        timer_scheduler_.Start( [this]( const TimerEntry& timer ) {
            ALICE_INFO( "定时器到期: [{}] {}", timer.id, timer.label );
            event_bus_.Emit( "timer.expired", {
                { "timer_id", timer.id },
                { "label", timer.label },
                { "data", timer.data },
                { "owner_plugin", timer.owner_plugin },
            }, "runtime" );
        } );

        event_bus_.SetThreadPool( thread_pool_.get( ) );
        ALICE_INFO( "  事件总线: 已就绪 (async 可用)" );
        ALICE_INFO( "  服务注册表: 已就绪" );

        // 文件监控
        fs_watcher_ = platform::CreateFsWatcher( );

        // 加载内置插件
        ALICE_INFO( "加载内置插件..." );
        LoadBuiltinPlugin( std::make_unique<builtin::BuiltinHello>( ) );
        LoadBuiltinPlugin( std::make_unique<builtin::BuiltinHttpShell>( ) );
        ALICE_INFO( "  已加载 {} 个插件", plugin_registry_.Count( ) );

        // 扫描外部插件 (mods/ 目录, 递归) — 先收集, 后拓扑排序加载
        auto mods_dir = exe_dir_ / "mods";
        std::vector<PluginManifest> manifests;

        if ( std::filesystem::exists( mods_dir ) ) {
            for ( auto& entry : std::filesystem::recursive_directory_iterator( mods_dir ) ) {
                if ( entry.path( ).filename( ) != "alice.json" ) continue;

                auto plugin_dir = entry.path( ).parent_path( );
                auto json = FileStorage::ReadJson( entry.path( ) );
                if ( !json ) continue;

                PluginManifest manifest;
                manifest.id = json->value( "id", plugin_dir.filename( ).string( ) );
                manifest.name = json->value( "name", manifest.id );
                manifest.version = json->value( "version", "0.0.0" );
                manifest.type = json->value( "type", "plugin" );
                manifest.runtime = json->value( "runtime", "" );
                manifest.clr_entry_type = json->value( "entry_type", "" );
                manifest.path = plugin_dir;

                if ( json->contains( "dependencies" ) && ( *json )["dependencies"].is_array( ) ) {
                    for ( auto& dep : ( *json )["dependencies"] )
                        manifest.dependencies.push_back( dep.get<std::string>( ) );
                }

                if ( manifest.runtime == "dotnet" ) {
                    manifest.entry_file = json->value( "entry_file", manifest.id + ".dll" );
                } else if ( FileStorage::Exists( plugin_dir / "main.lua" ) ) {
                    manifest.entry_file = "main.lua";
                } else if ( FileStorage::Exists( plugin_dir / "main.mjs" ) ) {
                    manifest.entry_file = "main.mjs";
                } else if ( FileStorage::Exists( plugin_dir / "main.js" ) ) {
                    manifest.entry_file = "main.js";
                } else {
                    continue;
                }

                manifests.push_back( std::move( manifest ) );
            }
        }

        // 拓扑排序: 被依赖的先加载
        {
            std::unordered_map<std::string, size_t> id_to_idx;
            for ( size_t i = 0; i < manifests.size( ); ++i ) id_to_idx[manifests[i].id] = i;

            std::vector<int> in_degree( manifests.size( ), 0 );
            std::vector<std::vector<size_t>> adj( manifests.size( ) );

            for ( size_t i = 0; i < manifests.size( ); ++i ) {
                for ( auto& dep : manifests[i].dependencies ) {
                    auto it = id_to_idx.find( dep );
                    if ( it != id_to_idx.end( ) ) {
                        adj[it->second].push_back( i );
                        ++in_degree[i];
                    }
                }
            }

            std::vector<PluginManifest> sorted;
            std::queue<size_t> q;
            for ( size_t i = 0; i < manifests.size( ); ++i )
                if ( in_degree[i] == 0 ) q.push( i );

            while ( !q.empty( ) ) {
                auto idx = q.front( ); q.pop( );
                sorted.push_back( std::move( manifests[idx] ) );
                for ( auto next : adj[idx] )
                    if ( --in_degree[next] == 0 ) q.push( next );
            }

            // 环依赖的插件追加到末尾 (带警告)
            for ( size_t i = 0; i < manifests.size( ); ++i ) {
                if ( in_degree[i] > 0 ) {
                    ALICE_WARN( "插件 '{}' 存在循环依赖, 延迟加载", manifests[i].id );
                    sorted.push_back( std::move( manifests[i] ) );
                }
            }

            manifests = std::move( sorted );
        }

        // 按排序顺序加载 (单个插件崩溃不阻止其他插件)
        for ( auto& manifest : manifests ) {
            try {
                if ( manifest.runtime == "dotnet" ) {
                    if ( !ClrHost::IsInitialized( ) ) {
                        auto clr_result = ClrHost::Initialize( exe_dir_ );
                        if ( !clr_result ) {
                            ALICE_ERROR( "CLR 初始化失败: {}", clr_result.error( ).message );
                            continue;
                        }
                    }
                    LoadBuiltinPlugin( std::make_unique<ClrPluginAdapter>( manifest ) );
                } else {
                    LoadBuiltinPlugin( std::make_unique<ScriptPluginAdapter>( manifest ) );
                }
            } catch ( const std::exception& e ) {
                ALICE_ERROR( "插件 '{}' 加载崩溃 (已隔离): {}", manifest.id, e.what( ) );
            } catch ( ... ) {
                ALICE_ERROR( "插件 '{}' 加载崩溃 (未知异常, 已隔离)", manifest.id );
            }
        }

        // 配置 HTTP 服务器 + 绑定插件注册的路由
        HttpServerConfig http_config;
        http_config.host = config_.Get<std::string>( "server.host", "0.0.0.0" );
        http_config.port = static_cast<uint16_t>( config_.Get<int>( "server.port", 645 ) );
        http_server_.Configure( http_config );
        route_registry_.BindToDrogon( );
        RegisterWsController( );

        // 启动 FsWatcher 热重载
        StartFsWatcher( );

        // 发布 runtime.ready 事件
        event_bus_.Emit( "runtime.ready", {}, "runtime" );

        ALICE_INFO( "Alice Runtime 初始化完成" );
        return VoidResult{};
    }

    void Runtime::Run( ) {
        ALICE_INFO( "Alice Runtime 运行于 http://{}:{}",
                   config_.Get<std::string>( "server.host", "0.0.0.0" ),
                   config_.Get<int>( "server.port", 645 ) );
        http_server_.Run( );
    }

    void Runtime::LoadBuiltinPlugin( std::unique_ptr<IPlugin> plugin ) {
        auto manifest = plugin->Manifest( );
        auto id = manifest.id;

        // 为插件创建专属 HostAPI
        auto host = std::make_unique<HostAPIImpl>( id, *this );
        auto* host_ptr = host.get( );
        host_apis_[ id ] = std::move( host );

        // 调用 OnLoad
        plugin->OnLoad( host_ptr );

        // 注册到 PluginRegistry
        PluginHandle handle( plugin.release( ), []( IPlugin* p ) { delete p; } );
        handle.manifest = manifest;
        plugin_registry_.Register( std::move( handle ) );
    }

    void Runtime::ReloadPlugin( const PluginID& plugin_id ) {
        auto* plugin = plugin_registry_.Get( plugin_id );
        if ( !plugin ) {
            ALICE_WARN( "热重载: 插件 {} 不存在", plugin_id );
            return;
        }

        auto manifest = plugin->Manifest( );
        ALICE_INFO( "热重载: {} ({})...", plugin_id, manifest.entry_file );

        // 1. 卸载旧插件
        plugin->OnUnload( );
        service_registry_.UnregisterAll( plugin_id );
        route_registry_.UnregisterAll( plugin_id );
        ws_router_.UnregisterAll( plugin_id );
        pipeline_engine_.UnregisterAll( plugin_id );
        plugin_registry_.Unregister( plugin_id );
        host_apis_.erase( plugin_id );

        // 2. 按 runtime 类型重建适配器
        std::unique_ptr<IPlugin> new_plugin;
        if ( manifest.runtime == "dotnet" ) {
            new_plugin = std::make_unique<ClrPluginAdapter>( manifest );
        } else {
            new_plugin = std::make_unique<ScriptPluginAdapter>( manifest );
        }
        LoadBuiltinPlugin( std::move( new_plugin ) );

        ALICE_INFO( "热重载: {} 完成", plugin_id );
        event_bus_.Emit( "plugin.reloaded", { { "plugin_id", plugin_id } }, "runtime" );
    }

    void Runtime::StartFsWatcher( ) {
        if ( !fs_watcher_ ) return;

        auto mods_dir = exe_dir_ / "mods";
        if ( !std::filesystem::exists( mods_dir ) ) return;

        // 构建文件路径 → plugin_id 映射
        // 防抖: 记录上次重载时间, 500ms 内不重复
        struct WatchState {
            std::mutex mutex;
            std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_reload;
        };
        auto state = std::make_shared<WatchState>( );

        fs_watcher_->Watch( mods_dir, [this, mods_dir, state]( platform::FsEvent evt ) {
            auto ext = evt.path.extension( ).string( );
            if ( ext != ".lua" && ext != ".mjs" && ext != ".js" && ext != ".dll" ) return;

            // 从文件路径推断 plugin_id
            auto rel = std::filesystem::relative( evt.path, mods_dir );
            auto plugin_dir = mods_dir / rel.begin( )->string( );

            // 可能是嵌套目录 (如 handlers/code-exec/main.lua)
            // 需要找到包含 alice.json 的目录
            auto search_dir = evt.path.parent_path( );
            while ( search_dir != mods_dir && search_dir.has_parent_path( ) ) {
                if ( std::filesystem::exists( search_dir / "alice.json" ) ) {
                    plugin_dir = search_dir;
                    break;
                }
                search_dir = search_dir.parent_path( );
            }

            if ( !std::filesystem::exists( plugin_dir / "alice.json" ) ) return;

            auto json = FileStorage::ReadJson( plugin_dir / "alice.json" );
            if ( !json ) return;
            auto plugin_id = json->value( "id", plugin_dir.filename( ).string( ) );

            // 防抖
            {
                std::lock_guard lock( state->mutex );
                auto now = std::chrono::steady_clock::now( );
                auto& last = state->last_reload[plugin_id];
                if ( now - last < std::chrono::milliseconds( 500 ) ) return;
                last = now;
            }

            ALICE_INFO( "FsWatcher: 检测到变更 {} → 重载 {}", evt.path.filename( ).string( ), plugin_id );

            // 在线程池中异步重载 (避免阻塞 FsWatcher 线程)
            thread_pool_->Submit( [this, plugin_id]( ) {
                try { ReloadPlugin( plugin_id ); }
                catch ( const std::exception& e ) { ALICE_ERROR( "热重载失败: {}", e.what( ) ); }
            } );
        } );

        ALICE_INFO( "FsWatcher: 监控 {}", mods_dir.string( ) );
    }

    void Runtime::Shutdown( ) {
        ALICE_INFO( "Alice Runtime 关闭中..." );

        // 停止文件监控
        fs_watcher_.reset( );

        // 卸载所有插件 (调 OnUnload)
        plugin_registry_.UnregisterAll( );
        host_apis_.clear( );

        timer_scheduler_.Stop( );
        if ( thread_pool_ ) {
            ALICE_INFO( "  等待 {} 个任务完成...", thread_pool_->PendingCount( ) );
            thread_pool_->Shutdown( );
        }
        if ( ClrHost::IsInitialized( ) ) ClrHost::Shutdown( );
        http_server_.Shutdown( );
        ALICE_INFO( "Alice Runtime 已退出" );
    }

} // namespace alice
