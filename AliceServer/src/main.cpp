#include "pch.h"
#include "Runtime/Runtime.h"
#include "Net/PipeServer.h"
#include "Plugin/IPlugin.h"
#include "Service/ServiceRegistry.h"

#include <csignal>
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

namespace {
    alice::Runtime* g_runtime = nullptr;
    alice::PipeServer* g_pipe = nullptr;

    void SignalHandler( int sig ) {
        if ( sig == SIGINT || sig == SIGTERM ) {
            ALICE_INFO( "收到关闭信号 ({})", sig );
            if ( g_runtime ) g_runtime->Shutdown( );
        }
    }

    void SendStateInit( alice::Runtime& rt, alice::PipeServer& pipe ) {
        auto plugins = rt.GetPluginRegistry( ).List( );
        auto services = rt.GetServiceRegistry( ).List( );

        nlohmann::json plugins_arr = nlohmann::json::array( );
        for ( auto& p : plugins ) {
            auto* plugin = rt.GetPluginRegistry( ).Get( p.id );
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
    }
}

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

    // 托管模式: 管道只传命令, 日志走 stdout
    alice::PipeServer pipe;

    if ( managed ) {
        g_pipe = &pipe;

        pipe.OnClientConnected = [&runtime, &pipe]( ) {
            SendStateInit( runtime, pipe );
        };

        pipe.OnClientDisconnected = [&runtime]( ) {
            ALICE_INFO( "Manager 管道断开, Server 自动退出" );
            runtime.Shutdown( );
        };

        (void)pipe.Start( [&runtime]( const std::string& line ) {
            try {
                auto msg = nlohmann::json::parse( line );
                auto type = msg.value( "type", "" );

                if ( type == "shutdown" ) {
                    ALICE_INFO( "收到 Manager 关闭命令" );
                    runtime.Shutdown( );

                } else if ( type == "plugin.unload" ) {
                    auto id = msg["data"].value( "id", "" );
                    if ( !id.empty( ) ) {
                        ALICE_INFO( "收到 Manager 卸载命令: {}", id );
                        auto* p = runtime.GetPluginRegistry( ).Get( id );
                        if ( p ) {
                            p->OnUnload( );
                            runtime.GetServiceRegistry( ).UnregisterAll( id );
                            runtime.GetRouteRegistry( ).UnregisterAll( id );
                            runtime.GetWsRouter( ).UnregisterAll( id );
                            runtime.GetPipelineEngine( ).UnregisterAll( id );
                            runtime.GetPluginRegistry( ).Unregister( id );
                            ALICE_INFO( "卸载完成: {}", id );
                        }
                    }

                } else if ( type == "plugin.reload" ) {
                    auto id = msg["data"].value( "id", "" );
                    if ( !id.empty( ) ) {
                        ALICE_INFO( "收到 Manager 重载命令: {}", id );
                        try { runtime.ReloadPlugin( id ); }
                        catch ( const std::exception& e ) { ALICE_ERROR( "重载失败: {}", e.what( ) ); }
                        catch ( ... ) { ALICE_ERROR( "重载失败 (unknown)" ); }
                    }
                }
            } catch ( ... ) {}
        } );

        ALICE_INFO( "托管模式: IPC 管道已就绪 (日志走 stdout)" );
    }

    runtime.Run( );

    g_pipe = nullptr;
    pipe.Stop( );
    return 0;
}
