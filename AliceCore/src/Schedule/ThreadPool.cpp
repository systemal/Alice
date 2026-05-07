#include "pch.h"
#include "Schedule/ThreadPool.h"

namespace alice {

    ThreadPool::ThreadPool( size_t num_threads ) {
        if ( num_threads == 0 )
            num_threads = std::max( 2u, std::thread::hardware_concurrency( ) );

        workers_.reserve( num_threads );
        for ( size_t i = 0; i < num_threads; ++i )
            workers_.emplace_back( &ThreadPool::WorkerLoop, this );
    }

    ThreadPool::~ThreadPool( ) { Shutdown( ); }

    void ThreadPool::WorkerLoop( ) {
        while ( true ) {
            std::function<void( )> task;
            {
                std::unique_lock lock( mutex_ );
                cv_.wait( lock, [this] { return stopping_ || !tasks_.empty( ); } );
                if ( stopping_ && tasks_.empty( ) ) return;
                task = std::move( tasks_.front( ) );
                tasks_.pop( );
            }
            task( );
            if ( --pending_ == 0 ) done_cv_.notify_all( );
        }
    }

    void ThreadPool::WaitAll( ) {
        std::unique_lock lock( mutex_ );
        done_cv_.wait( lock, [this] { return pending_ == 0; } );
    }

    void ThreadPool::Shutdown( ) {
        { std::lock_guard lock( mutex_ ); if ( stopping_ ) return; stopping_ = true; }
        cv_.notify_all( );
        for ( auto& w : workers_ ) if ( w.joinable( ) ) w.join( );
    }

} // namespace alice
