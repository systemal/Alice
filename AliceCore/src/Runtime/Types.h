#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <source_location>

namespace alice {

    /// <summary>
    /// 全局 ID 类型.
    /// 全部用 string 是为了脚本语言兼容 — Lua/JS 没有强类型 ID.
    /// TODO: 考虑用 strong typedef (tagged_type<Tag, string>) 防止混用,
    ///       但会增加跨语言序列化复杂度, 暂不值得.
    /// </summary>
    using PluginID = std::string;
    using ServiceID = std::string;
    using SessionID = std::string;
    using TraceID = std::string;
    using TimerID = std::string;
    using EventHandle = uint64_t;

    /// <summary>
    /// 时间戳. 用 system_clock 因为需要和外部世界同步 (日志, 定时器到期时间).
    /// steady_clock 用于链路追踪的耗时计算 (见 Trace.h).
    /// </summary>
    using Timestamp = std::chrono::system_clock::time_point;

    /// <summary>
    /// 错误码. 按模块分段:
    /// - 0-99:   通用
    /// - 100-199: 插件
    /// - 200-299: 管线
    /// - 300-399: 服务
    /// - 400-499: 事件
    /// - 500-599: 脚本
    /// - 600+:    网络/存储/进程
    ///
    /// TODO: 考虑改成 uint16_t 节省空间, 但 uint32_t 对齐更好, 暂不改.
    /// </summary>
    enum class ErrorCode : uint32_t {
        Ok = 0,
        Unknown = 1,
        NotFound = 2,
        InvalidArgument = 3,
        PermissionDenied = 4,
        Timeout = 5,
        AlreadyExists = 6,

        PluginLoadFailed = 100,
        PluginNotFound = 101,
        PluginPermissionDenied = 102,

        PipelineAborted = 200,
        PipelineMaxRetries = 201,
        PipelineStageFailed = 202,

        ServiceNotFound = 300,
        ServiceCallFailed = 301,

        EventGuardBlocked = 400,
        EventTTLExceeded = 401,

        ScriptError = 500,
        ScriptTimeout = 501,

        NetRequestFailed = 600,
        StorageFailed = 700,
        ProcessFailed = 800,
    };

    /// <summary>
    /// 结构化错误, 携带源码位置.
    /// MakeError() 自动捕获 source_location, 方便调试.
    ///
    /// 设计取舍: 不用 std::exception, 因为错误需要跨模块边界传递 (C ABI/Lua/JS/C#),
    /// 异常不适合跨 DLL/语言边界. std::expected 是更好的选择.
    /// </summary>
    struct Error {
        ErrorCode code = ErrorCode::Unknown;
        std::string message;
        std::source_location location;

        Error( ) = default;
        Error( ErrorCode c, std::string msg, std::source_location loc = std::source_location::current( ) )
            : code( c ), message( std::move( msg ) ), location( loc ) {}
    };

    /// <summary>
    /// Result 类型 — 整个 Core 的错误处理基石.
    /// 所有可能失败的操作返回 Result<T> 或 VoidResult.
    /// 调用方必须检查, 编译器会对 [[nodiscard]] 发出警告.
    /// </summary>
    template<typename T>
    using Result = std::expected<T, Error>;

    using VoidResult = std::expected<void, Error>;

    /// <summary>
    /// 构造错误的便捷函数. source_location 默认参数会自动捕获调用位置.
    /// 用法: return std::unexpected( MakeError( ErrorCode::NotFound, "xxx" ) );
    /// </summary>
    inline Error MakeError( ErrorCode code, std::string message,
                             std::source_location loc = std::source_location::current( ) ) {
        return { code, std::move( message ), loc };
    }

} // namespace alice
