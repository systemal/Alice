#include "pch.h"
#include "Platform/Platform.h"

#ifdef _WIN32

#include <Windows.h>
#include <ShlObj.h>

namespace alice::platform {

    std::string GetPlatformName( ) { return "Windows"; }

    std::filesystem::path GetExecutableDir( ) {
        wchar_t buf[MAX_PATH] = {};
        GetModuleFileNameW( nullptr, buf, MAX_PATH );
        return std::filesystem::path( buf ).parent_path( );
    }

    std::filesystem::path GetDataDir( ) {
        return GetExecutableDir( ) / "data";
    }

    // ── WinFsWatcher ──

    class WinFsWatcher : public IFsWatcher {
    public:
        ~WinFsWatcher( ) override {
            for ( auto& [dir, ctx] : watches_ ) {
                ctx.running = false;
                CancelIoEx( ctx.dir_handle, nullptr );
                if ( ctx.thread.joinable( ) ) ctx.thread.join( );
                CloseHandle( ctx.dir_handle );
            }
        }

        void Watch( const std::filesystem::path& dir, std::function<void( FsEvent )> callback ) override {
            auto abs_dir = std::filesystem::absolute( dir );
            auto key = abs_dir.string( );

            if ( watches_.contains( key ) ) return;

            HANDLE h = CreateFileW(
                abs_dir.wstring( ).c_str( ),
                FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                nullptr );

            if ( h == INVALID_HANDLE_VALUE ) {
                ALICE_ERROR( "FsWatcher: 无法打开目录 {}", abs_dir.string( ) );
                return;
            }

            auto& ctx = watches_[key];
            ctx.dir_handle = h;
            ctx.dir_path = abs_dir;
            ctx.callback = std::move( callback );
            ctx.running = true;

            ctx.thread = std::thread( [&ctx]( ) {
                alignas( DWORD ) char buffer[4096];
                OVERLAPPED overlapped = {};
                overlapped.hEvent = CreateEvent( nullptr, TRUE, FALSE, nullptr );

                while ( ctx.running ) {
                    DWORD bytes_returned = 0;
                    ResetEvent( overlapped.hEvent );

                    BOOL ok = ReadDirectoryChangesW(
                        ctx.dir_handle, buffer, sizeof( buffer ), TRUE,
                        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
                        nullptr, &overlapped, nullptr );

                    if ( !ok ) break;

                    DWORD wait = WaitForSingleObject( overlapped.hEvent, 500 );
                    if ( !ctx.running ) break;
                    if ( wait != WAIT_OBJECT_0 ) continue;

                    if ( !GetOverlappedResult( ctx.dir_handle, &overlapped, &bytes_returned, FALSE ) ) continue;
                    if ( bytes_returned == 0 ) continue;

                    auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>( buffer );
                    while ( true ) {
                        std::wstring wname( info->FileName, info->FileNameLength / sizeof( wchar_t ) );
                        auto file_path = ctx.dir_path / wname;

                        FsEvent::Action action;
                        switch ( info->Action ) {
                            case FILE_ACTION_ADDED:            action = FsEvent::Action::Created; break;
                            case FILE_ACTION_REMOVED:          action = FsEvent::Action::Deleted; break;
                            case FILE_ACTION_MODIFIED:         action = FsEvent::Action::Modified; break;
                            case FILE_ACTION_RENAMED_OLD_NAME: action = FsEvent::Action::Renamed; break;
                            case FILE_ACTION_RENAMED_NEW_NAME: action = FsEvent::Action::Renamed; break;
                            default: action = FsEvent::Action::Modified; break;
                        }

                        ctx.callback( FsEvent{ action, file_path } );

                        if ( info->NextEntryOffset == 0 ) break;
                        info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                            reinterpret_cast<char*>( info ) + info->NextEntryOffset );
                    }
                }

                CloseHandle( overlapped.hEvent );
            } );
        }

        void Unwatch( const std::filesystem::path& dir ) override {
            auto key = std::filesystem::absolute( dir ).string( );
            auto it = watches_.find( key );
            if ( it == watches_.end( ) ) return;

            it->second.running = false;
            CancelIoEx( it->second.dir_handle, nullptr );
            if ( it->second.thread.joinable( ) ) it->second.thread.join( );
            CloseHandle( it->second.dir_handle );
            watches_.erase( it );
        }

    private:
        struct WatchCtx {
            HANDLE dir_handle = INVALID_HANDLE_VALUE;
            std::filesystem::path dir_path;
            std::function<void( FsEvent )> callback;
            std::atomic<bool> running{ false };
            std::thread thread;
        };

        std::unordered_map<std::string, WatchCtx> watches_;
    };

    std::unique_ptr<IFsWatcher> CreateFsWatcher( ) {
        return std::make_unique<WinFsWatcher>( );
    }

    Result<ProcessResult> Exec( [[maybe_unused]] const std::string& command, [[maybe_unused]] const ExecOptions& opts ) {
        return std::unexpected( MakeError( ErrorCode::ProcessFailed, "Process::Exec 尚未实现" ) );
    }

} // namespace alice::platform

#endif
