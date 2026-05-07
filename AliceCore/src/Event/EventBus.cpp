#include "pch.h"
#include "Event/EventBus.h"
#include "Schedule/ThreadPool.h"
#include "Trace/Trace.h"

namespace alice {

    thread_local int EventBus::current_chain_depth_ = 0;

    EventBus::EventBus( EventGuardConfig guard_config )
        : guard_config_( guard_config ) {
    }

    EventHandle EventBus::Subscribe( const std::string& event_name, EventCallback callback,
                                      const PluginID& subscriber_plugin ) {
        std::lock_guard lock( mutex_ );
        auto handle = next_handle_.fetch_add( 1 );
        subscribers_.push_back( {
            .handle = handle,
            .event_name = event_name,
            .plugin_id = subscriber_plugin,
            .callback = std::move( callback ),
        } );
        return handle;
    }

    void EventBus::Unsubscribe( EventHandle handle ) {
        std::lock_guard lock( mutex_ );
        std::erase_if( subscribers_, [handle]( auto& s ) { return s.handle == handle; } );
    }

    void EventBus::UnsubscribeAll( const PluginID& plugin_id ) {
        std::lock_guard lock( mutex_ );
        std::erase_if( subscribers_, [&plugin_id]( auto& s ) { return s.plugin_id == plugin_id; } );
    }

    void EventBus::Emit( const std::string& name, const nlohmann::json& data,
                          const PluginID& source_plugin,
                          const TraceID& trace_id,
                          int ttl ) {

        EventData event;
        event.name = name;
        event.data = data;
        event.meta.source_plugin = source_plugin;
        event.meta.trace_id = trace_id.empty( ) ? GenerateTraceId( ) : trace_id;
        event.meta.ttl = ( ttl < 0 ) ? 10 : ttl;
        event.meta.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );

        ++total_emitted_;

        // 治理检查
        if ( !GuardCheck( event ) ) {
            ++total_blocked_;
            return;
        }

        // 收集匹配的订阅者 (持有锁的时间最短)
        std::vector<EventCallback> matched;
        {
            std::lock_guard lock( mutex_ );
            for ( auto& sub : subscribers_ ) {
                if ( sub.event_name == name || sub.event_name == "*" ) {
                    matched.push_back( sub.callback );
                }
            }
        }

        // 分发 (不持锁, 回调可能触发新的 Emit)
        struct DepthGuard {
            DepthGuard( ) { ++current_chain_depth_; }
            ~DepthGuard( ) { --current_chain_depth_; }
        } depth_guard;

        for ( auto& cb : matched ) {
            try {
                cb( event );
                ++total_delivered_;
            }
            catch ( const std::exception& e ) {
                ALICE_ERROR( "事件 '{}' 回调异常: {}", name, e.what( ) );
            }
        }
    }

    void EventBus::EmitAsync( const std::string& name, const nlohmann::json& data,
                              const PluginID& source_plugin,
                              const TraceID& trace_id,
                              int ttl ) {
        if ( thread_pool_ ) {
            auto captured = std::make_shared<nlohmann::json>( data );
            thread_pool_->Submit( [this, name, captured, source_plugin, trace_id, ttl]( ) {
                Emit( name, *captured, source_plugin, trace_id, ttl );
            } );
        } else {
            Emit( name, data, source_plugin, trace_id, ttl );
        }
    }

    bool EventBus::GuardCheck( const EventData& event ) {
        // TTL 检查
        if ( event.meta.ttl <= 0 ) {
            ALICE_WARN( "事件 '{}' TTL 耗尽 (trace: {})", event.name, event.meta.trace_id );
            return false;
        }

        // 链路深度检查
        if ( current_chain_depth_ >= guard_config_.max_chain_depth ) {
            ALICE_WARN( "事件 '{}' 链路深度超限 ({}/{})",
                       event.name, current_chain_depth_, guard_config_.max_chain_depth );
            return false;
        }

        return true;
    }

    EventBus::Stats EventBus::GetStats( ) const {
        std::lock_guard lock( mutex_ );
        return {
            .total_emitted = total_emitted_.load( ),
            .total_delivered = total_delivered_.load( ),
            .total_blocked = total_blocked_.load( ),
            .subscriber_count = subscribers_.size( ),
        };
    }

} // namespace alice
