#include "pch.h"
#include "Service/ServiceRegistry.h"

namespace alice {

    VoidResult ServiceRegistry::Register( std::string_view capability,
                                           const PluginID& plugin_id,
                                           ServiceHandler handler,
                                           const nlohmann::json& schema ) {
        std::lock_guard lock( mutex_ );

        auto it = services_.find( capability );
        if ( it != services_.end( ) ) {
            if ( it->second.info.provider_plugin == plugin_id ) {
                it->second.handler = std::move( handler );
                it->second.info.schema = schema;
                ALICE_INFO( "服务已覆盖: {} (插件: {})", capability, plugin_id );
                return VoidResult{};
            }
            return std::unexpected( MakeError( ErrorCode::AlreadyExists,
                std::format( "服务 '{}' 已被插件 '{}' 注册",
                            capability, it->second.info.provider_plugin ) ) );
        }

        services_.emplace( std::string( capability ), Entry{
            .info = {
                .capability = std::string( capability ),
                .provider_plugin = plugin_id,
                .schema = schema,
            },
            .handler = std::move( handler ),
        } );

        ALICE_INFO( "服务已注册: {} (插件: {})", capability, plugin_id );
        cv_.notify_all( );
        return VoidResult{};
    }

    void ServiceRegistry::UnregisterAll( const PluginID& plugin_id ) {
        std::lock_guard lock( mutex_ );
        std::erase_if( services_, [&]( auto& pair ) {
            return pair.second.info.provider_plugin == plugin_id;
        } );
    }

    Result<nlohmann::json> ServiceRegistry::Call( std::string_view capability,
                                                   std::string_view method,
                                                   const nlohmann::json& args ) {
        ServiceHandler handler;
        std::string cap_str;
        {
            std::lock_guard lock( mutex_ );
            auto it = services_.find( capability );
            if ( it == services_.end( ) ) {
                return std::unexpected( MakeError( ErrorCode::ServiceNotFound,
                    std::format( "服务 '{}' 未注册", capability ) ) );
            }
            handler = it->second.handler;
            cap_str = it->first;
        }

        try {
            auto result = handler( std::string( method ), args );
            if ( result ) {
                std::lock_guard lock( mutex_ );
                auto it = services_.find( cap_str );
                if ( it != services_.end( ) ) {
                    it->second.info.consecutive_failures = 0;
                    it->second.info.healthy = true;
                }
            }
            return result;
        }
        catch ( const std::exception& e ) {
            {
                std::lock_guard lock( mutex_ );
                auto it = services_.find( cap_str );
                if ( it != services_.end( ) ) {
                    ++it->second.info.consecutive_failures;
                    if ( it->second.info.consecutive_failures >= 5 )
                        it->second.info.healthy = false;
                }
            }
            return std::unexpected( MakeError( ErrorCode::ServiceCallFailed,
                std::format( "服务 '{}' 调用异常: {}", capability, e.what( ) ) ) );
        }
    }

    bool ServiceRegistry::WaitFor( std::string_view capability, std::chrono::milliseconds timeout ) {
        std::unique_lock lock( mutex_ );
        return cv_.wait_for( lock, timeout, [&]( ) { return services_.contains( capability ); } );
    }

    bool ServiceRegistry::Has( std::string_view capability ) const {
        std::lock_guard lock( mutex_ );
        return services_.contains( capability );
    }

    std::vector<ServiceInfo> ServiceRegistry::List( ) const {
        std::lock_guard lock( mutex_ );
        std::vector<ServiceInfo> result;
        result.reserve( services_.size( ) );
        for ( auto& [_, entry] : services_ ) {
            result.push_back( entry.info );
        }
        return result;
    }

    std::vector<ServiceInfo> ServiceRegistry::ListByPrefix( std::string_view prefix ) const {
        std::lock_guard lock( mutex_ );
        std::vector<ServiceInfo> result;
        for ( auto& [cap, entry] : services_ ) {
            if ( cap.starts_with( prefix ) ) {
                result.push_back( entry.info );
            }
        }
        return result;
    }

} // namespace alice
