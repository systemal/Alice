#include "pch.h"
#include "Storage/KvStore.h"
#include "Storage/FileStorage.h"

namespace alice {

    KvStore::KvStore( const std::filesystem::path& base_dir )
        : base_dir_( base_dir ) {
        (void)FileStorage::EnsureDirectory( base_dir_ );
    }

    std::filesystem::path KvStore::NsPath( const std::string& ns ) const {
        return base_dir_ / ( ns + ".json" );
    }

    nlohmann::json& KvStore::LoadNamespace( const std::string& ns ) {
        auto it = cache_.find( ns );
        if ( it != cache_.end( ) ) return it->second;

        auto data = FileStorage::ReadJsonOrDefault( NsPath( ns ) );
        cache_[ ns ] = std::move( data );
        return cache_[ ns ];
    }

    VoidResult KvStore::SaveNamespace( const std::string& ns ) {
        auto it = cache_.find( ns );
        if ( it == cache_.end( ) ) return VoidResult{};
        return FileStorage::WriteJson( NsPath( ns ), it->second );
    }

    Result<nlohmann::json> KvStore::Get( const std::string& ns, const std::string& key ) {
        std::lock_guard lock( mutex_ );
        auto& data = LoadNamespace( ns );
        if ( !data.contains( key ) ) {
            return std::unexpected( MakeError( ErrorCode::NotFound,
                std::format( "Key '{}' not found in namespace '{}'", key, ns ) ) );
        }
        return data[ key ];
    }

    VoidResult KvStore::Set( const std::string& ns, const std::string& key, const nlohmann::json& value ) {
        std::lock_guard lock( mutex_ );
        auto& data = LoadNamespace( ns );
        data[ key ] = value;
        return SaveNamespace( ns );
    }

    VoidResult KvStore::Delete( const std::string& ns, const std::string& key ) {
        std::lock_guard lock( mutex_ );
        auto& data = LoadNamespace( ns );
        data.erase( key );
        return SaveNamespace( ns );
    }

    std::vector<std::string> KvStore::Keys( const std::string& ns ) {
        std::lock_guard lock( mutex_ );
        auto& data = LoadNamespace( ns );
        std::vector<std::string> keys;
        for ( auto& [k, _] : data.items( ) ) keys.push_back( k );
        return keys;
    }

} // namespace alice
