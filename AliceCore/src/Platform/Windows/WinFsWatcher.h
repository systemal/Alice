#pragma once

#include "Platform/Platform.h"
#include <thread>
#include <atomic>
#include <unordered_map>

#ifdef _WIN32

namespace alice::platform {

    /// <summary>
    /// Windows 文件系统监控器 (ReadDirectoryChangesW).
    /// 在独立线程中监控目录变更, 变更时调用回调.
    /// </summary>
    class WinFsWatcherImpl : public IFsWatcher {
    public:
        WinFsWatcherImpl( );
        ~WinFsWatcherImpl( ) override;

        void Watch( const std::filesystem::path& dir, std::function<void( FsEvent )> callback ) override;
        void Unwatch( const std::filesystem::path& dir ) override;

    private:
        struct WatchEntry {
            std::filesystem::path dir;
            std::function<void( FsEvent )> callback;
            HANDLE dir_handle = INVALID_HANDLE_VALUE;
            std::thread thread;
            std::atomic<bool> running{ true };
        };

        std::unordered_map<std::string, std::unique_ptr<WatchEntry>> watches_;
        std::mutex mutex_;

        static void WatchThread( WatchEntry* entry );
    };

} // namespace alice::platform

#endif
