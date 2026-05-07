#include "pch.h"
#include "Platform/Platform.h"
#include "Platform/Windows/WinFsWatcher.h"

#ifdef _WIN32

#include <ShlObj.h>

namespace alice::platform {

    // TODO(Phase 8): 实现 Windows 系统托盘 (Shell_NotifyIconW)
    class WinSystemTray : public ISystemTray {
    public:
        void Show( [[maybe_unused]] const TrayConfig& config ) override {
            ALICE_WARN( "WinSystemTray::Show 尚未实现" );
        }

        void Hide( ) override {}
        void SetTooltip( [[maybe_unused]] std::string_view text ) override {}
    };

    // TODO(Phase 8): 实现 Windows 命名管道 IPC
    class WinIpcServer : public IIpcServer {
    public:
        Task<void> Start( [[maybe_unused]] std::string_view name ) override {
            ALICE_WARN( "WinIpcServer::Start 尚未实现" );
            co_return;
        }

        void OnMessage( [[maybe_unused]] std::function<nlohmann::json( const nlohmann::json& )> handler ) override {}
        void Stop( ) override {}
    };

    // Factory implementations
    std::unique_ptr<IFsWatcher> CreateFsWatcher( ) {
        return std::make_unique<WinFsWatcherImpl>( );
    }

    std::unique_ptr<ISystemTray> CreateSystemTray( ) {
        return std::make_unique<WinSystemTray>( );
    }

    std::unique_ptr<IIpcServer> CreateIpcServer( ) {
        return std::make_unique<WinIpcServer>( );
    }

    std::string GetPlatformName( ) {
        return "Windows";
    }

    std::filesystem::path GetExecutableDir( ) {
        wchar_t buf[MAX_PATH] = {};
        GetModuleFileNameW( nullptr, buf, MAX_PATH );
        return std::filesystem::path( buf ).parent_path( );
    }

    std::filesystem::path GetDataDir( ) {
        return GetExecutableDir( ) / "data";
    }

} // namespace alice::platform

#endif // _WIN32
