#include "pch.h"
#include "Plugin/PluginRegistry.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace alice {

    // ── PluginHandle RAII ──

    PluginHandle::~PluginHandle( ) {
        plugin.reset( );
#ifdef _WIN32
        if ( dll_handle ) FreeLibrary( static_cast<HMODULE>( dll_handle ) );
#endif
    }

    PluginHandle::PluginHandle( PluginHandle&& other ) noexcept
        : plugin( std::move( other.plugin ) )
        , dll_handle( std::exchange( other.dll_handle, nullptr ) )
        , manifest( std::move( other.manifest ) ) {}

    PluginHandle& PluginHandle::operator=( PluginHandle&& other ) noexcept {
        if ( this != &other ) {
            plugin = std::move( other.plugin );
            dll_handle = std::exchange( other.dll_handle, nullptr );
            manifest = std::move( other.manifest );
        }
        return *this;
    }

    // ── PluginRegistry ──

    void PluginRegistry::Register( PluginHandle handle ) {
        std::lock_guard lock( mutex_ );
        auto id = handle.manifest.id;
        ALICE_INFO( "插件已注册: {} [{}] v{}", handle.manifest.name, id, handle.manifest.version );
        plugins_[ id ] = std::move( handle );
    }

    void PluginRegistry::Unregister( const PluginID& id ) {
        std::lock_guard lock( mutex_ );
        auto it = plugins_.find( id );
        if ( it != plugins_.end( ) ) {
            ALICE_INFO( "插件已卸载: {}", id );
            plugins_.erase( it );
        }
    }

    void PluginRegistry::UnregisterAll( ) {
        std::lock_guard lock( mutex_ );
        ALICE_INFO( "卸载所有插件 ({} 个)", plugins_.size( ) );
        plugins_.clear( );
    }

    IPlugin* PluginRegistry::Get( const PluginID& id ) const {
        std::lock_guard lock( mutex_ );
        auto it = plugins_.find( id );
        return it != plugins_.end( ) ? it->second.get( ) : nullptr;
    }

    std::vector<PluginRegistry::PluginInfo> PluginRegistry::List( ) const {
        std::lock_guard lock( mutex_ );
        std::vector<PluginInfo> result;
        for ( auto& [id, handle] : plugins_ ) {
            result.push_back( {
                .id = id,
                .name = handle.manifest.name,
                .type = handle.manifest.type,
                .version = handle.manifest.version,
            } );
        }
        return result;
    }

    std::vector<PluginRegistry::PluginInfo> PluginRegistry::ListByType( const std::string& type ) const {
        std::lock_guard lock( mutex_ );
        std::vector<PluginInfo> result;
        for ( auto& [id, handle] : plugins_ ) {
            if ( handle.manifest.type == type ) {
                result.push_back( { .id = id, .name = handle.manifest.name, .type = type, .version = handle.manifest.version } );
            }
        }
        return result;
    }

    size_t PluginRegistry::Count( ) const {
        std::lock_guard lock( mutex_ );
        return plugins_.size( );
    }

} // namespace alice
