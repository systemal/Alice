#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <string>

namespace alice::stdlib {

    /// <summary>
    /// alice.process.exec — 执行子进程.
    /// 内部用 bee::subprocess (CreateProcess + 管道), 不是 system() 或 _popen().
    /// 输出经过 SanitizeUtf8 清洗, 防止非法 UTF-8 导致 JSON 构造崩溃.
    ///
    /// 命令会被包装为 "cmd /c {command}", 所以支持 cmd 内置命令.
    /// TODO: Linux 需要改成 "/bin/sh -c"., 等待完成, 返回 stdout+stderr+exitcode
    /// </summary>
    /// <param name="command">命令字符串 (如 "ls -la" 或 "powershell -c Get-Date")</param>
    /// <param name="opts">选项: { cwd, timeout_ms, stdin }</param>
    /// <returns>{ exit_code, stdout, stderr }</returns>
    Result<nlohmann::json> ProcessExec( const std::string& command, const nlohmann::json& opts = nlohmann::json::object( ) );

    /// <summary>
    /// alice.path.* — 路径操作 (基于 std::filesystem, 跨平台)
    /// </summary>
    std::string PathJoin( const std::string& a, const std::string& b );
    std::string PathDirname( const std::string& path );
    std::string PathBasename( const std::string& path );
    std::string PathExtension( const std::string& path );
    std::string PathAbsolute( const std::string& path );

    /// <summary>
    /// alice.regex.* — 正则表达式 (std::regex, ECMAScript 语法)
    /// </summary>
    Result<nlohmann::json> RegexMatch( const std::string& text, const std::string& pattern );
    std::string RegexReplace( const std::string& text, const std::string& pattern, const std::string& replacement );
    bool RegexTest( const std::string& text, const std::string& pattern );

    /// <summary>
    /// alice.encoding.* — Base64 + Hex 编码 (手写实现, 无外部依赖)
    /// </summary>
    std::string Base64Encode( const std::string& data );
    std::string Base64Decode( const std::string& data );
    std::string HexEncode( const std::string& data );

    /// <summary>
    /// alice.time.* — 毫秒时间戳 + strftime 格式化
    /// </summary>
    int64_t TimeNow( );
    std::string TimeFormat( int64_t timestamp_ms, const std::string& fmt );

} // namespace alice::stdlib
