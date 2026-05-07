#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <mutex>

namespace alice {

    /// <summary>
    /// 全局配置 (data/config.json).
    /// 支持点号路径: Get("server.port", 645) → json_pointer("/server/port").
    /// Set 会立即保存到文件 (锁内调 SaveLocked, 无并发竞态).
    /// TODO: 不支持热重载配置. 修改 config.json 后需要重启.
    /// </summary>
    class Config {
    public:
        VoidResult Load( const std::filesystem::path& config_path );
        VoidResult Save( );

        template<typename T>
        T Get( std::string_view key, const T& default_value = T{} ) const {
            std::lock_guard lock( mutex_ );
            try {
                auto ptr = nlohmann::json::json_pointer( "/" + DotToSlash( key ) );
                if ( data_.contains( ptr ) ) return data_.at( ptr ).get<T>( );
            }
            catch ( ... ) {}
            return default_value;
        }

        template<typename T>
        void Set( std::string_view key, const T& value ) {
            std::lock_guard lock( mutex_ );
            auto ptr = nlohmann::json::json_pointer( "/" + DotToSlash( key ) );
            data_[ ptr ] = value;
            SaveLocked( );
        }

        const nlohmann::json& Raw( ) const { return data_; }

    private:
        nlohmann::json data_;
        std::filesystem::path path_;
        mutable std::mutex mutex_;

        VoidResult SaveLocked( );
        static nlohmann::json DefaultConfig( );
        static std::string DotToSlash( std::string_view dotted );
    };

} // namespace alice
