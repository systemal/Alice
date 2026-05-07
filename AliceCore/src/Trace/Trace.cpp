#include "pch.h"
#include "Trace/Trace.h"

namespace alice {

    static int64_t NowMicros( ) {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
    }

    std::string TraceLog::BeginSpan( const TraceID& trace_id, const std::string& name,
                                      const std::string& parent_span_id ) {
        std::lock_guard lock( mutex_ );

        Span span;
        span.trace_id = trace_id;
        span.span_id = std::format( "s-{}", span_counter_.fetch_add( 1 ) );
        span.parent_span_id = parent_span_id;
        span.name = name;
        span.start_us = NowMicros( );

        auto id = span.span_id;
        spans_.push_back( std::move( span ) );
        return id;
    }

    void TraceLog::EndSpan( const std::string& span_id ) {
        std::lock_guard lock( mutex_ );
        for ( auto it = spans_.rbegin( ); it != spans_.rend( ); ++it ) {
            if ( it->span_id == span_id ) {
                it->end_us = NowMicros( );
                return;
            }
        }
    }

    std::vector<Span> TraceLog::GetTrace( const TraceID& trace_id ) const {
        std::lock_guard lock( mutex_ );
        std::vector<Span> result;
        for ( auto& s : spans_ ) {
            if ( s.trace_id == trace_id ) result.push_back( s );
        }
        return result;
    }

    void TraceLog::Cleanup( size_t max_traces ) {
        std::lock_guard lock( mutex_ );
        if ( spans_.size( ) > max_traces * 10 ) {
            spans_.erase( spans_.begin( ), spans_.begin( ) + static_cast<ptrdiff_t>( spans_.size( ) - max_traces * 5 ) );
        }
    }

} // namespace alice
