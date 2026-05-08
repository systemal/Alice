#pragma once

#include "Runtime/Types.h"
#include <functional>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

namespace alice {

    /// <summary>
    /// 跨平台 IPC 管道 (单客户端, 行分隔 JSON 协议).
    /// Windows: Named Pipe (\\.\pipe\Alice_IPC), 字节流模式.
    /// Linux/macOS: Unix Domain Socket (/tmp/alice_ipc.sock).
    ///
    /// 协议: 每条消息一行 JSON + '\n'. 双向.
    /// </summary>
    class PipeServer {
    public:
        using MessageHandler = std::function<void( const std::string& line )>;

        PipeServer( ) = default;
        ~PipeServer( );

        PipeServer( const PipeServer& ) = delete;
        PipeServer& operator=( const PipeServer& ) = delete;

        VoidResult Start( MessageHandler on_message = nullptr );
        void Stop( );
        bool IsRunning( ) const { return running_.load( ); }
        bool HasClient( ) const { return client_connected_.load( ); }

        void Send( const std::string& line );

        std::function<void( )> OnClientConnected;
        std::function<void( )> OnClientDisconnected;

    private:
        std::atomic<bool> running_{ false };
        std::atomic<bool> client_connected_{ false };
        MessageHandler on_message_;

        std::thread server_thread_;
        void* client_handle_ = nullptr;
        std::mutex write_mutex_;

        void ServerLoop( );
        void HandleClient( void* handle );

        static constexpr const char* PIPE_NAME =
#ifdef _WIN32
            R"(\\.\pipe\Alice_IPC)";
#else
            "/tmp/alice_ipc.sock";
#endif
    };

} // namespace alice
