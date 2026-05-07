#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>

namespace alice {

    /// <summary>
    /// 文件存储工具 — 全静态, 无状态.
    /// 原子写入: 先写 .tmp, 成功后 rename, 防止写一半崩溃.
    /// TODO: 没有文件锁, 多进程写同一文件会冲突. 单进程够用.
    /// </summary>
    class FileStorage {
    public:
        static Result<nlohmann::json> ReadJson( const std::filesystem::path& path );
        static nlohmann::json ReadJsonOrDefault( const std::filesystem::path& path,
                                                  const nlohmann::json& default_value = nlohmann::json::object( ) );
        static VoidResult WriteJson( const std::filesystem::path& path, const nlohmann::json& data, bool pretty = true );
        static Result<std::string> ReadText( const std::filesystem::path& path );
        static VoidResult WriteText( const std::filesystem::path& path, std::string_view content );
        static VoidResult EnsureDirectory( const std::filesystem::path& dir );
        static bool Exists( const std::filesystem::path& path );
        static VoidResult Remove( const std::filesystem::path& path );
    };

} // namespace alice
