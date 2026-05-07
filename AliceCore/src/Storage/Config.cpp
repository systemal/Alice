#include "pch.h"
#include "Storage/Config.h"
#include "Storage/FileStorage.h"

namespace alice {

    VoidResult Config::Load( const std::filesystem::path& config_path ) {
        path_ = config_path;

        if ( FileStorage::Exists( path_ ) ) {
            auto result = FileStorage::ReadJson( path_ );
            if ( !result ) {
                ALICE_ERROR( "配置文件读取失败: {}", result.error( ).message );
                return std::unexpected( result.error( ) );
            }
            data_ = std::move( *result );
            ALICE_INFO( "已加载配置: {}", path_.string( ) );
        }
        else {
            data_ = DefaultConfig( );
            auto save_result = Save( );
            if ( !save_result ) return save_result;
            ALICE_INFO( "已创建默认配置: {}", path_.string( ) );
        }

        return VoidResult{};
    }

    VoidResult Config::Save( ) {
        std::lock_guard lock( mutex_ );
        return SaveLocked( );
    }

    VoidResult Config::SaveLocked( ) {
        return FileStorage::WriteJson( path_, data_ );
    }

    nlohmann::json Config::DefaultConfig( ) {
        return {
            { "runtime", {
                { "thread_pool", 0 },
            }},
            { "server", {
                { "port", 645 },
                { "host", "0.0.0.0" },
            }},
        };
    }

    std::string Config::DotToSlash( std::string_view dotted ) {
        std::string result( dotted );
        for ( auto& c : result ) {
            if ( c == '.' ) c = '/';
        }
        return result;
    }

} // namespace alice
