#pragma once

#include "Runtime/Types.h"
#include <functional>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <condition_variable>

namespace alice {

    /// <summary>
    /// 跨平台 IPC 管道 (单客户端, 行分隔 JSON 协议).
    /// 读写分离线程, Send() 永不阻塞调用者.
    /// Windows: Named Pipe. Linux/macOS: Unix Domain Socket.
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

        /// 非阻塞: 数据进写队列, 由写线程异步发送
        void Send( const std::string& line );

        std::function<void( )> OnClientConnected;
        std::function<void( )> OnClientDisconnected;

    private:
        std::atomic<bool> running_{ false };
        std::atomic<bool> client_connected_{ false };
        MessageHandler on_message_;

        std::thread server_thread_;
        std::thread read_thread_;
        std::thread write_thread_;
        std::atomic<bool> write_running_{ false };

        void* client_handle_ = nullptr;
        std::mutex write_mutex_;

        // 写队列 (Send 入队, WriteLoop 出队发送)
        std::mutex write_queue_mutex_;
        std::condition_variable write_cv_;
        std::vector<std::string> write_queue_;

        void ServerLoop( );
        void ReadLoop( void* handle );
        void WriteLoop( );

        static constexpr const char* PIPE_NAME =
#ifdef _WIN32
            R"(\\.\pipe\Alice_IPC)";
#else
            "/tmp/alice_ipc.sock";
#endif
    };

} // namespace alice
