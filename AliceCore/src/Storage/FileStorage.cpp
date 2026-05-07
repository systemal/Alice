#include "pch.h"
#include "Storage/FileStorage.h"
#include <fstream>

namespace alice {

    Result<nlohmann::json> FileStorage::ReadJson( const std::filesystem::path& path ) {
        if ( !std::filesystem::exists( path ) )
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "文件不存在: {}", path.string( ) ) ) );

        std::ifstream file( path );
        if ( !file.is_open( ) )
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "无法打开文件: {}", path.string( ) ) ) );

        try {
            return nlohmann::json::parse( file );
        }
        catch ( const nlohmann::json::parse_error& e ) {
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "JSON 解析失败: {} ({})", path.string( ), e.what( ) ) ) );
        }
    }

    nlohmann::json FileStorage::ReadJsonOrDefault(
        const std::filesystem::path& path, const nlohmann::json& default_value ) {
        auto result = ReadJson( path );
        return result.has_value( ) ? std::move( *result ) : default_value;
    }

    VoidResult FileStorage::WriteJson(
        const std::filesystem::path& path, const nlohmann::json& data, bool pretty ) {
        auto dir_result = EnsureDirectory( path.parent_path( ) );
        if ( !dir_result ) return dir_result;

        auto temp_path = path; temp_path += ".tmp";
        std::ofstream file( temp_path, std::ios::trunc );
        if ( !file.is_open( ) )
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "无法创建临时文件: {}", temp_path.string( ) ) ) );

        file << ( pretty ? data.dump( 4 ) : data.dump( ) );
        file.close( );
        if ( file.fail( ) ) {
            std::filesystem::remove( temp_path );
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "写入失败: {}", path.string( ) ) ) );
        }

        std::error_code ec;
        std::filesystem::rename( temp_path, path, ec );
        if ( ec ) {
            std::filesystem::remove( temp_path );
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "重命名失败: {} ({})", path.string( ), ec.message( ) ) ) );
        }
        return VoidResult{};
    }

    Result<std::string> FileStorage::ReadText( const std::filesystem::path& path ) {
        if ( !std::filesystem::exists( path ) )
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "文件不存在: {}", path.string( ) ) ) );

        std::ifstream file( path, std::ios::binary );
        if ( !file.is_open( ) )
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "无法打开文件: {}", path.string( ) ) ) );

        return std::string(
            ( std::istreambuf_iterator<char>( file ) ),
            std::istreambuf_iterator<char>( ) );
    }

    VoidResult FileStorage::WriteText( const std::filesystem::path& path, std::string_view content ) {
        auto dir_result = EnsureDirectory( path.parent_path( ) );
        if ( !dir_result ) return dir_result;

        auto temp_path = path; temp_path += ".tmp";
        std::ofstream file( temp_path, std::ios::binary | std::ios::trunc );
        if ( !file.is_open( ) )
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "无法创建临时文件: {}", temp_path.string( ) ) ) );

        file.write( content.data( ), static_cast<std::streamsize>( content.size( ) ) );
        file.close( );
        if ( file.fail( ) ) {
            std::filesystem::remove( temp_path );
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "写入失败: {}", path.string( ) ) ) );
        }

        std::error_code ec;
        std::filesystem::rename( temp_path, path, ec );
        if ( ec ) {
            std::filesystem::remove( temp_path );
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "重命名失败: {} ({})", path.string( ), ec.message( ) ) ) );
        }
        return VoidResult{};
    }

    VoidResult FileStorage::EnsureDirectory( const std::filesystem::path& dir ) {
        if ( dir.empty( ) || std::filesystem::exists( dir ) ) return VoidResult{};
        std::error_code ec;
        std::filesystem::create_directories( dir, ec );
        if ( ec )
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "创建目录失败: {} ({})", dir.string( ), ec.message( ) ) ) );
        return VoidResult{};
    }

    bool FileStorage::Exists( const std::filesystem::path& path ) {
        return std::filesystem::exists( path );
    }

    VoidResult FileStorage::Remove( const std::filesystem::path& path ) {
        std::error_code ec;
        std::filesystem::remove_all( path, ec );
        if ( ec )
            return std::unexpected( MakeError( ErrorCode::StorageFailed,
                std::format( "删除失败: {} ({})", path.string( ), ec.message( ) ) ) );
        return VoidResult{};
    }

} // namespace alice
