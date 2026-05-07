#include "pch.h"
#include "Schedule/Timer.h"

namespace alice {

    TimerScheduler::~TimerScheduler( ) { Stop( ); }

    void TimerScheduler::Start( TimerCallback on_expired ) {
        on_expired_ = std::move( on_expired );
        running_ = true;
        thread_ = std::thread( &TimerScheduler::ScheduleLoop, this );
    }

    void TimerScheduler::Stop( ) {
        running_ = false;
        if ( thread_.joinable( ) ) thread_.join( );
    }

    TimerID TimerScheduler::Add( std::chrono::seconds duration, const std::string& label,
                                  const nlohmann::json& data, const PluginID& owner ) {
        std::lock_guard lock( mutex_ );
        TimerEntry entry;
        entry.id = std::format( "timer-{}", next_id_.fetch_add( 1 ) );
        entry.label = label;
        entry.data = data;
        entry.fire_time = std::chrono::steady_clock::now( ) + duration;
        entry.owner_plugin = owner;
        timers_.push_back( std::move( entry ) );
        return timers_.back( ).id;
    }

    bool TimerScheduler::Remove( const TimerID& id ) {
        std::lock_guard lock( mutex_ );
        auto it = std::ranges::find_if( timers_, [&]( auto& t ) { return t.id == id; } );
        if ( it == timers_.end( ) ) return false;
        timers_.erase( it );
        return true;
    }

    std::vector<TimerEntry> TimerScheduler::ListActive( ) const {
        std::lock_guard lock( mutex_ );
        auto now = std::chrono::steady_clock::now( );
        std::vector<TimerEntry> result;
        for ( auto& t : timers_ ) {
            if ( t.fire_time > now ) result.push_back( t );
        }
        return result;
    }

    void TimerScheduler::ScheduleLoop( ) {
        while ( running_ ) {
            std::vector<TimerEntry> expired;
            {
                std::lock_guard lock( mutex_ );
                auto now = std::chrono::steady_clock::now( );
                std::erase_if( timers_, [&]( auto& t ) {
                    if ( t.fire_time <= now ) { expired.push_back( t ); return true; }
                    return false;
                } );
            }

            for ( auto& timer : expired ) {
                if ( on_expired_ ) on_expired_( timer );
            }

            std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
        }
    }

} // namespace alice
