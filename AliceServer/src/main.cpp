#include "pch.h"
#include "Runtime/Runtime.h"
#include "Net/PipeServer.h"
#include "Plugin/IPlugin.h"
#include "Service/ServiceRegistry.h"
#include <spdlog/sinks/base_sink.h>

#include <csignal>
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

// spdlog sink → 管道转发 (带缓冲 + 异步写入)
template<typename Mutex>
class PipeSink : public spdlog::sinks::base_sink<Mutex> {
public:
    void SetPipe( alice::PipeServer* pipe ) { pipe_ = pipe; }

    void FlushBufferToClient( ) {
        std::lock_guard lock( queue_mutex_ );
        if ( !pipe_ ) return;
        for ( auto& line : buffer_ ) pipe_->Send( line );
        buffer_.clear( );
        flushed_ = true;

        // 启动写线程 (如果还没启动)
        if ( !writer_running_ ) {
            writer_running_ = true;
            writer_thread_ = std::thread( [this]( ) { WriterLoop( ); } );
        }
    }

    void StopWriter( ) {
        writer_running_ = false;
        queue_cv_.notify_one( );
        if ( writer_thread_.joinable( ) ) writer_thread_.join( );
    }

protected:
    void sink_it_( const spdlog::details::log_msg& msg ) override {
        if ( !pipe_ ) return;

        auto line = FormatLog( msg );

        std::lock_guard lock( queue_mutex_ );
        if ( !flushed_ ) {
            if ( buffer_.size( ) < 2000 ) buffer_.push_back( std::move( line ) );
        } else {
            queue_.push_back( std::move( line ) );
            queue_cv_.notify_one( );
        }
    }

    void flush_( ) override {}

private:
    alice::PipeServer* pipe_ = nullptr;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::vector<std::string> buffer_;           // 客户端连上前的缓冲
    std::vector<std::string> queue_;            // 客户端连上后的发送队列
    std::atomic<bool> flushed_{ false };
    std::atomic<bool> writer_running_{ false };
    std::thread writer_thread_;

    void WriterLoop( ) {
        while ( writer_running_ ) {
            std::vector<std::string> batch;
            {
                std::unique_lock lock( queue_mutex_ );
                queue_cv_.wait_for( lock, std::chrono::milliseconds( 50 ),
                    [this]( ) { return !queue_.empty( ) || !writer_running_; } );
                if ( queue_.empty( ) ) continue;
                batch.swap( queue_ );
            }
            for ( auto& line : batch ) {
                if ( pipe_ ) pipe_->Send( line );
            }
        }
    }

    static std::string FormatLog( const spdlog::details::log_msg& msg ) {
        // level
        auto lv = spdlog::level::to_string_view( msg.level );
        std::string level( lv.data( ), lv.size( ) );

        // source: 文件名:行号
        std::string source;
        if ( msg.source.filename ) {
            source = msg.source.filename;
            auto slash = source.find_last_of( "/\\" );
            if ( slash != std::string::npos ) source = source.substr( slash + 1 );
            if ( msg.source.line > 0 )
                source += ":" + std::to_string( msg.source.line );
        }

        // payload → 解析 [plugin_id] 前缀
        std::string payload( msg.payload.data( ), msg.payload.size( ) );
        std::string plugin;
        if ( payload.size( ) > 2 && payload[0] == '[' ) {
            auto close = payload.find( ']' );
            if ( close != std::string::npos ) {
                plugin = payload.substr( 1, close - 1 );
                payload = payload.substr( close + 1 );
                if ( !payload.empty( ) && payload[0] == ' ' )
                    payload.erase( 0, 1 );
            }
        }

        return nlohmann::json{
            { "type", "log" },
            { "level", level },
            { "source", source },
            { "plugin", plugin },
            { "message", payload },
        }.dump( );
    }
};

namespace {
    alice::Runtime* g_runtime = nullptr;

    void SignalHandler( int sig ) {
        if ( sig == SIGINT || sig == SIGTERM ) {
            ALICE_INFO( "收到关闭信号 ({})", sig );
            if ( g_runtime ) g_runtime->Shutdown( );
        }
    }
}

static alice::PipeServer* g_pipe = nullptr;

int main( int argc, char* argv[] ) {
    bool managed = false;
    for ( int i = 1; i < argc; ++i ) {
        if ( std::string( argv[i] ) == "--managed" ) managed = true;
    }
#ifdef _WIN32
    SetConsoleOutputCP( CP_UTF8 );
    SetConsoleCP( CP_UTF8 );
    [[maybe_unused]] HANDLE hMutex = CreateMutexW( nullptr, FALSE, L"Alice_Server_Instance" );
    if ( GetLastError( ) == ERROR_ALREADY_EXISTS ) {
        MessageBoxW( nullptr, L"Alice Server is already running.", L"Alice", MB_OK | MB_ICONINFORMATION );
        return 0;
    }
#else
    int lock_fd = open( "/tmp/alice_server.lock", O_CREAT | O_RDWR, 0666 );
    if ( lock_fd >= 0 && flock( lock_fd, LOCK_EX | LOCK_NB ) != 0 ) {
        fprintf( stderr, "Alice Server is already running.\n" );
        return 0;
    }
#endif

    alice::Logger::Init( );

    // 管道 + PipeSink 必须在一切日志之前就绪, 否则 Initialize() 的日志丢失
    alice::PipeServer pipe;
    auto pipe_sink = std::make_shared<PipeSink<std::mutex>>( );

    // runtime 指针, 供管道回调安全访问 (Initialize 前为 nullptr)
    std::atomic<alice::Runtime*> rt_ptr{ nullptr };

    if ( managed ) {
        g_pipe = &pipe;

        // PipeSink 挂到 spdlog (管道没客户端时 Send 是空操作)
        pipe_sink->SetPipe( &pipe );
        spdlog::default_logger( )->sinks( ).push_back( pipe_sink );

        // 客户端连上 → 推送全量状态 (如果 runtime 已初始化)
        pipe.OnClientConnected = [&rt_ptr, &pipe, &pipe_sink]( ) {
            // 先刷出启动阶段缓冲的日志
            pipe_sink->FlushBufferToClient( );
            auto* rt = rt_ptr.load( );
            if ( !rt ) return;

            auto plugins = rt->GetPluginRegistry( ).List( );
            auto services = rt->GetServiceRegistry( ).List( );

            nlohmann::json plugins_arr = nlohmann::json::array( );
            for ( auto& p : plugins ) {
                auto* plugin = rt->GetPluginRegistry( ).Get( p.id );
                auto manifest = plugin ? plugin->Manifest( ) : alice::PluginManifest{};
                plugins_arr.push_back( {
                    { "id", p.id }, { "name", p.name }, { "type", p.type },
                    { "version", p.version }, { "runtime", manifest.runtime },
                } );
            }

            nlohmann::json services_arr = nlohmann::json::array( );
            for ( auto& s : services ) {
                services_arr.push_back( {
                    { "capability", s.capability },
                    { "provider", s.provider_plugin },
                    { "healthy", s.healthy },
                } );
            }

            pipe.Send( nlohmann::json{
                { "type", "state.init" },
                { "plugins", plugins_arr },
                { "services", services_arr },
            }.dump( ) );
        };

        // 客户端断开 → Server 自动退出
        pipe.OnClientDisconnected = [&rt_ptr]( ) {
            auto* rt = rt_ptr.load( );
            if ( rt ) {
                ALICE_INFO( "Manager 管道断开, Server 自动退出" );
                rt->Shutdown( );
            }
        };

        // 启动管道 (此时已能收发日志)
        (void)pipe.Start( [&rt_ptr]( const std::string& line ) {
            try {
                auto msg = nlohmann::json::parse( line );
                auto type = msg.value( "type", "" );
                if ( type == "shutdown" ) {
                    auto* rt = rt_ptr.load( );
                    if ( rt ) {
                        ALICE_INFO( "收到 Manager 关闭命令" );
                        rt->Shutdown( );
                    }
                } else if ( type == "plugin.unload" ) {
                    auto* rt = rt_ptr.load( );
                    if ( rt && msg.contains( "data" ) ) {
                        auto id = msg["data"].value( "id", "" );
                        if ( !id.empty( ) ) {
                            ALICE_INFO( "收到 Manager 卸载命令: {}", id );
                            auto* p = rt->GetPluginRegistry( ).Get( id );
                            if ( p ) {
                                p->OnUnload( );
                                rt->GetServiceRegistry( ).UnregisterAll( id );
                                rt->GetRouteRegistry( ).UnregisterAll( id );
                                rt->GetWsRouter( ).UnregisterAll( id );
                                rt->GetPipelineEngine( ).UnregisterAll( id );
                                rt->GetPluginRegistry( ).Unregister( id );
                                ALICE_INFO( "卸载完成: {}", id );
                            }
                        }
                    }
                } else if ( type == "plugin.reload" ) {
                    auto* rt = rt_ptr.load( );
                    if ( rt && msg.contains( "data" ) ) {
                        auto id = msg["data"].value( "id", "" );
                        if ( !id.empty( ) ) {
                            ALICE_INFO( "收到 Manager 重载命令: {}", id );
                            try { rt->ReloadPlugin( id ); }
                            catch ( const std::exception& e ) { ALICE_ERROR( "重载失败 (exception): {}", e.what( ) ); }
                            catch ( ... ) { ALICE_ERROR( "重载失败 (unknown)" ); }
                        }
                    }
                }
            } catch ( ... ) {}
        } );
    }

    ALICE_INFO( "Alice Server v0.3.0 (runtime)" );

    alice::Runtime runtime;
    g_runtime = &runtime;

    std::signal( SIGINT, SignalHandler );
    std::signal( SIGTERM, SignalHandler );

    auto result = runtime.Initialize( );
    if ( !result ) {
        ALICE_ERROR( "初始化失败: {}", result.error( ).message );
        return 1;
    }

    // runtime 就绪, 管道回调现在可以安全访问它
    rt_ptr.store( &runtime );

    // 如果 Manager 在 Initialize 期间已经连上, 补推一次 state.init
    if ( managed && pipe.HasClient( ) && pipe.OnClientConnected ) {
        pipe.OnClientConnected( );
    }

    if ( managed ) ALICE_INFO( "托管模式: IPC 管道已就绪" );

    runtime.Run( );

    rt_ptr.store( nullptr );
    pipe_sink->StopWriter( );
    g_pipe = nullptr;
    pipe.Stop( );
    return 0;
}
