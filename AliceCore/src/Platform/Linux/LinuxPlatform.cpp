#include "pch.h"
#include "Platform/Platform.h"

#ifdef __linux__

namespace alice::platform {

    // TODO(Phase 8+): Implement Linux file system watcher (inotify)
    class LinuxFsWatcher : public IFsWatcher {
    public:
        void Watch( const std::filesystem::path& dir, std::function<void( FsEvent )> callback ) override {
            // TODO: inotify implementation
        }

        void Unwatch( const std::filesystem::path& dir ) override {
            // TODO
        }
    };

    // TODO: Linux system tray (libappindicator or similar)
    class LinuxSystemTray : public ISystemTray {
    public:
        void Show( const TrayConfig& config ) override {}
        void Hide( ) override {}
        void SetTooltip( std::string_view text ) override {}
    };

    // TODO: Unix domain socket IPC
    class LinuxIpcServer : public IIpcServer {
    public:
        Task<void> Start( std::string_view name ) override { co_return; }
        void OnMessage( std::function<nlohmann::json( const nlohmann::json& )> handler ) override {}
        void Stop( ) override {}
    };

    std::unique_ptr<IFsWatcher> CreateFsWatcher( ) {
        return std::make_unique<LinuxFsWatcher>( );
    }

    std::unique_ptr<ISystemTray> CreateSystemTray( ) {
        return std::make_unique<LinuxSystemTray>( );
    }

    std::unique_ptr<IIpcServer> CreateIpcServer( ) {
        return std::make_unique<LinuxIpcServer>( );
    }

    std::string GetPlatformName( ) {
        return "Linux";
    }

    std::filesystem::path GetExecutableDir( ) {
        // /proc/self/exe 是当前进程可执行文件的符号链接
        return std::filesystem::read_symlink( "/proc/self/exe" ).parent_path( );
    }

    std::filesystem::path GetDataDir( ) {
        return GetExecutableDir( ) / "data";
    }

} // namespace alice::platform

#endif // __linux__
