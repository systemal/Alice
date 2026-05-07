#include "pch.h"

#ifdef _WIN32

#include "Platform/Windows/WinFsWatcher.h"

namespace alice::platform {

    WinFsWatcherImpl::WinFsWatcherImpl( ) = default;

    WinFsWatcherImpl::~WinFsWatcherImpl( ) {
        std::lock_guard lock( mutex_ );
        for ( auto& [key, entry] : watches_ ) {
            entry->running = false;
            if ( entry->dir_handle != INVALID_HANDLE_VALUE ) {
                CancelIoEx( entry->dir_handle, nullptr );
            }
            if ( entry->thread.joinable( ) ) {
                entry->thread.join( );
            }
            if ( entry->dir_handle != INVALID_HANDLE_VALUE ) {
                CloseHandle( entry->dir_handle );
            }
        }
    }

    void WinFsWatcherImpl::Watch(
        const std::filesystem::path& dir,
        std::function<void( FsEvent )> callback ) {

        auto key = dir.string( );

        std::lock_guard lock( mutex_ );

        // 已在监控则先停止
        if ( watches_.contains( key ) ) {
            Unwatch( dir );
        }

        auto handle = CreateFileW(
            dir.c_str( ),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr );

        if ( handle == INVALID_HANDLE_VALUE ) {
            ALICE_ERROR( "FsWatcher: 无法打开目录 {}", dir.string( ) );
            return;
        }

        auto entry = std::make_unique<WatchEntry>( );
        entry->dir = dir;
        entry->callback = std::move( callback );
        entry->dir_handle = handle;

        auto* raw = entry.get( );
        entry->thread = std::thread( WatchThread, raw );

        watches_[ key ] = std::move( entry );
        ALICE_INFO( "FsWatcher: 开始监控 {}", dir.string( ) );
    }

    void WinFsWatcherImpl::Unwatch( const std::filesystem::path& dir ) {
        auto key = dir.string( );

        auto it = watches_.find( key );
        if ( it == watches_.end( ) ) return;

        auto& entry = it->second;
        entry->running = false;
        CancelIoEx( entry->dir_handle, nullptr );

        if ( entry->thread.joinable( ) ) {
            entry->thread.join( );
        }

        CloseHandle( entry->dir_handle );
        watches_.erase( it );
        ALICE_INFO( "FsWatcher: 停止监控 {}", dir.string( ) );
    }

    void WinFsWatcherImpl::WatchThread( WatchEntry* entry ) {
        alignas( DWORD ) char buffer[4096];
        OVERLAPPED overlapped = {};
        overlapped.hEvent = CreateEvent( nullptr, TRUE, FALSE, nullptr );

        while ( entry->running ) {
            DWORD bytes_returned = 0;

            BOOL success = ReadDirectoryChangesW(
                entry->dir_handle,
                buffer,
                sizeof( buffer ),
                TRUE,  // 递归监控子目录
                FILE_NOTIFY_CHANGE_FILE_NAME |
                FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE |
                FILE_NOTIFY_CHANGE_CREATION,
                nullptr,
                &overlapped,
                nullptr );

            if ( !success ) break;

            DWORD wait_result = WaitForSingleObject( overlapped.hEvent, 1000 );
            if ( wait_result == WAIT_TIMEOUT ) continue;
            if ( wait_result != WAIT_OBJECT_0 ) break;

            if ( !GetOverlappedResult( entry->dir_handle, &overlapped, &bytes_returned, FALSE ) ) {
                break;
            }

            if ( bytes_returned == 0 ) continue;

            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>( buffer );
            while ( true ) {
                std::wstring filename( info->FileName, info->FileNameLength / sizeof( wchar_t ) );
                auto path = entry->dir / filename;

                FsEvent::Action action;
                switch ( info->Action ) {
                case FILE_ACTION_ADDED:    action = FsEvent::Action::Created; break;
                case FILE_ACTION_REMOVED:  action = FsEvent::Action::Deleted; break;
                case FILE_ACTION_MODIFIED: action = FsEvent::Action::Modified; break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                case FILE_ACTION_RENAMED_NEW_NAME:
                    action = FsEvent::Action::Renamed; break;
                default: action = FsEvent::Action::Modified; break;
                }

                entry->callback( FsEvent{ .action = action, .path = path } );

                if ( info->NextEntryOffset == 0 ) break;
                info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<char*>( info ) + info->NextEntryOffset );
            }

            ResetEvent( overlapped.hEvent );
        }

        CloseHandle( overlapped.hEvent );
    }

} // namespace alice::platform

#endif
