#include "pch.h"
#include "Net/WsRegistry.h"

namespace alice {

    WsRegistry& WsRegistry::Instance( ) {
        static WsRegistry instance;
        return instance;
    }

    void WsRegistry::Register( const SessionID& session_id, uint64_t conn_id,
                                const drogon::WebSocketConnectionPtr& conn ) {
        std::lock_guard lock( mutex_ );
        entries_.push_back( { session_id, conn_id, conn } );
    }

    void WsRegistry::Unregister( uint64_t conn_id ) {
        std::lock_guard lock( mutex_ );
        std::erase_if( entries_, [conn_id]( auto& e ) { return e.conn_id == conn_id; } );
    }

    void WsRegistry::Broadcast( const SessionID& session_id, const std::string& message ) {
        std::lock_guard lock( mutex_ );
        std::erase_if( entries_, [&]( auto& e ) {
            auto locked = e.conn.lock( );
            if ( !locked ) return true;
            if ( e.session_id == session_id ) locked->send( message );
            return false;
        } );
    }

    void WsRegistry::Send( uint64_t conn_id, const std::string& message ) {
        std::lock_guard lock( mutex_ );
        for ( auto& e : entries_ ) {
            if ( e.conn_id == conn_id ) {
                if ( auto locked = e.conn.lock( ) ) locked->send( message );
                break;
            }
        }
    }

    size_t WsRegistry::ClientCount( const SessionID& session_id ) const {
        std::lock_guard lock( mutex_ );
        size_t count = 0;
        for ( auto& e : entries_ ) {
            if ( e.session_id == session_id && e.conn.lock( ) ) ++count;
        }
        return count;
    }

} // namespace alice
