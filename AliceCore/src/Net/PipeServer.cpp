#include "pch.h"
#include "Net/PipeServer.h"

#ifdef _WIN32

#include <Windows.h>

namespace alice {

    PipeServer::~PipeServer( ) { Stop( ); }

    VoidResult PipeServer::Start( MessageHandler on_message ) {
        if ( running_ ) return VoidResult{};
        on_message_ = std::move( on_message );
        running_ = true;
        server_thread_ = std::thread( [this]( ) { ServerLoop( ); } );
        ALICE_INFO( "PipeServer: 已启动 ({})", PIPE_NAME );
        return VoidResult{};
    }

    void PipeServer::Stop( ) {
        if ( !running_ ) return;
        running_ = false;

        {
            std::lock_guard lock( write_mutex_ );
            if ( client_handle_ ) {
                CancelIoEx( static_cast<HANDLE>( client_handle_ ), nullptr );
                FlushFileBuffers( static_cast<HANDLE>( client_handle_ ) );
                DisconnectNamedPipe( static_cast<HANDLE>( client_handle_ ) );
                ::CloseHandle( static_cast<HANDLE>( client_handle_ ) );
                client_handle_ = nullptr;
            }
        }

        auto wake = CreateFileA( PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0,
                                  nullptr, OPEN_EXISTING, 0, nullptr );
        if ( wake != INVALID_HANDLE_VALUE ) ::CloseHandle( wake );

        if ( server_thread_.joinable( ) ) server_thread_.join( );
        if ( write_thread_.joinable( ) ) write_thread_.join( );
        if ( read_thread_.joinable( ) ) read_thread_.join( );
        ALICE_INFO( "PipeServer: 已停止" );
    }

    void PipeServer::ServerLoop( ) {
        while ( running_ ) {
            HANDLE pipe = CreateNamedPipeA(
                PIPE_NAME,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,
                65536, 65536, 0, nullptr );

            if ( pipe == INVALID_HANDLE_VALUE ) {
                if ( running_ ) ALICE_ERROR( "PipeServer: CreateNamedPipe 失败: {}", GetLastError( ) );
                break;
            }

            BOOL ok = ConnectNamedPipe( pipe, nullptr );
            if ( !ok && GetLastError( ) != ERROR_PIPE_CONNECTED ) {
                ::CloseHandle( pipe );
                continue;
            }

            if ( !running_ ) {
                DisconnectNamedPipe( pipe );
                ::CloseHandle( pipe );
                break;
            }

            // 设置客户端
            {
                std::lock_guard lock( write_mutex_ );
                client_handle_ = pipe;
            }
            client_connected_ = true;

            if ( OnClientConnected ) OnClientConnected( );

            // 读写分离: 两个独立线程
            read_thread_ = std::thread( [this, pipe]( ) { ReadLoop( pipe ); } );
            write_thread_ = std::thread( [this]( ) { WriteLoop( ); } );

            // 等读线程结束 (客户端断开)
            read_thread_.join( );

            // 停写线程
            write_running_ = false;
            write_cv_.notify_one( );
            write_thread_.join( );

            // 清理
            {
                std::lock_guard lock( write_mutex_ );
                if ( client_handle_ == pipe ) {
                    DisconnectNamedPipe( pipe );
                    ::CloseHandle( pipe );
                    client_handle_ = nullptr;
                }
            }
            client_connected_ = false;
            if ( OnClientDisconnected ) OnClientDisconnected( );
        }
    }

    void PipeServer::ReadLoop( void* handle ) {
        std::string buffer;
        char chunk[4096];

        while ( running_ ) {
            DWORD bytes_read = 0;
            BOOL ok = ReadFile( static_cast<HANDLE>( handle ),
                                 chunk, sizeof( chunk ), &bytes_read, nullptr );
            if ( !ok || bytes_read == 0 ) break;

            buffer.append( chunk, bytes_read );

            size_t pos;
            while ( ( pos = buffer.find( '\n' ) ) != std::string::npos ) {
                auto line = buffer.substr( 0, pos );
                buffer.erase( 0, pos + 1 );
                if ( !line.empty( ) && on_message_ ) on_message_( line );
            }
        }
    }

    void PipeServer::WriteLoop( ) {
        write_running_ = true;
        while ( write_running_ ) {
            std::vector<std::string> batch;
            {
                std::unique_lock lock( write_queue_mutex_ );
                write_cv_.wait_for( lock, std::chrono::milliseconds( 20 ),
                    [this]( ) { return !write_queue_.empty( ) || !write_running_; } );
                if ( write_queue_.empty( ) ) continue;
                batch.swap( write_queue_ );
            }

            std::lock_guard lock( write_mutex_ );
            if ( !client_handle_ ) continue;

            for ( auto& data : batch ) {
                DWORD written = 0;
                WriteFile( static_cast<HANDLE>( client_handle_ ),
                           data.data( ), static_cast<DWORD>( data.size( ) ), &written, nullptr );
            }
        }
    }

    void PipeServer::Send( const std::string& line ) {
        std::string data = line;
        if ( data.empty( ) || data.back( ) != '\n' ) data += '\n';

        std::lock_guard lock( write_queue_mutex_ );
        write_queue_.push_back( std::move( data ) );
        write_cv_.notify_one( );
    }

} // namespace alice

#else

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace alice {

    PipeServer::~PipeServer( ) { Stop( ); }

    VoidResult PipeServer::Start( MessageHandler on_message ) {
        if ( running_ ) return VoidResult{};
        on_message_ = std::move( on_message );

        ::unlink( PIPE_NAME );

        int fd = ::socket( AF_UNIX, SOCK_STREAM, 0 );
        if ( fd < 0 )
            return std::unexpected( MakeError( ErrorCode::NetRequestFailed, "socket() 失败" ) );

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy( addr.sun_path, PIPE_NAME, sizeof( addr.sun_path ) - 1 );

        if ( ::bind( fd, reinterpret_cast<struct sockaddr*>( &addr ), sizeof( addr ) ) < 0 ||
             ::listen( fd, 1 ) < 0 ) {
            ::close( fd );
            return std::unexpected( MakeError( ErrorCode::NetRequestFailed, "bind/listen 失败" ) );
        }

        running_ = true;
        server_thread_ = std::thread( [this, fd]( ) {
            while ( running_ ) {
                int cfd = ::accept( fd, nullptr, nullptr );
                if ( cfd < 0 ) { if ( running_ ) continue; else break; }

                {
                    std::lock_guard lock( write_mutex_ );
                    client_handle_ = reinterpret_cast<void*>( static_cast<intptr_t>( cfd ) );
                }
                client_connected_ = true;
                if ( OnClientConnected ) OnClientConnected( );

                // 读循环 (写通过 Send 异步)
                std::string buffer;
                char chunk[4096];
                while ( running_ ) {
                    ssize_t n = ::read( cfd, chunk, sizeof( chunk ) );
                    if ( n <= 0 ) break;
                    buffer.append( chunk, static_cast<size_t>( n ) );
                    size_t pos;
                    while ( ( pos = buffer.find( '\n' ) ) != std::string::npos ) {
                        auto line = buffer.substr( 0, pos );
                        buffer.erase( 0, pos + 1 );
                        if ( !line.empty( ) && on_message_ ) on_message_( line );
                    }
                }

                ::close( cfd );
                {
                    std::lock_guard lock( write_mutex_ );
                    client_handle_ = nullptr;
                }
                client_connected_ = false;
                if ( OnClientDisconnected ) OnClientDisconnected( );
            }
            ::close( fd );
            ::unlink( PIPE_NAME );
        } );

        ALICE_INFO( "PipeServer: 已启动 ({})", PIPE_NAME );
        return VoidResult{};
    }

    void PipeServer::Stop( ) {
        if ( !running_ ) return;
        running_ = false;
        // TODO: shutdown socket to unblock accept/read
        if ( server_thread_.joinable( ) ) server_thread_.join( );
        ALICE_INFO( "PipeServer: 已停止" );
    }

    void PipeServer::Send( const std::string& line ) {
        std::lock_guard lock( write_mutex_ );
        auto raw = reinterpret_cast<intptr_t>( client_handle_ );
        if ( raw <= 0 ) return;

        std::string data = line;
        if ( data.empty( ) || data.back( ) != '\n' ) data += '\n';
        ::write( static_cast<int>( raw ), data.data( ), data.size( ) );
    }

} // namespace alice

#endif
