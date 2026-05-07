#pragma once

#include <string>
#include <filesystem>
#include <memory>
#include <functional>
#include "Runtime/Types.h"

namespace alice::platform {

    /// <summary>
    /// 文件系统变更事件
    /// </summary>
    /// <summary>
    /// 文件系统变更事件.
    /// FsWatcher 在后台线程中检测到变更后, 通过回调通知.
    /// </summary>
    struct FsEvent {
        enum class Action { Created, Modified, Deleted, Renamed };
        Action action;
        std::filesystem::path path;
        std::filesystem::path old_path;
    };

    /// <summary>
    /// 文件系统监控接口
    /// </summary>
    /// <summary>
    /// 文件系统监控接口.
    /// Windows: ReadDirectoryChangesW + overlapped IO.
    /// Linux: TODO (inotify).
    /// </summary>
    class IFsWatcher {
    public:
        virtual ~IFsWatcher( ) = default;
        virtual void Watch( const std::filesystem::path& dir, std::function<void( FsEvent )> callback ) = 0;
        virtual void Unwatch( const std::filesystem::path& dir ) = 0;
    };

    /// <summary>
    /// 子进程执行结果
    /// </summary>
    struct ProcessResult {
        int exit_code = -1;
        std::string stdout_output;
        std::string stderr_output;
    };

    /// <summary>
    /// 子进程执行选项
    /// </summary>
    struct ExecOptions {
        std::filesystem::path working_dir;
        std::chrono::seconds timeout{ 30 };
    };

    // 平台工厂
    /// <summary>
    /// 平台工厂函数.
    /// CreateFsWatcher: Windows 返回 WinFsWatcher, Linux TODO.
    /// Exec: TODO, 目前子进程执行走 StdLib::ProcessExec (bee::subprocess).
    /// </summary>
    std::string GetPlatformName( );
    std::filesystem::path GetExecutableDir( );
    std::filesystem::path GetDataDir( );
    std::unique_ptr<IFsWatcher> CreateFsWatcher( );
    Result<ProcessResult> Exec( const std::string& command, const ExecOptions& opts = {} );

} // namespace alice::platform
