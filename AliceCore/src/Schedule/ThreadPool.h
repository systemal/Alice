#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

namespace alice {

    /// <summary>
    /// 固定大小线程池.
    /// num_threads=0 时自动取 max(2, CPU核心数).
    /// Submit 返回 std::future, 调用方可以异步等待结果.
    /// WaitAll 阻塞直到所有已提交任务完成.
    /// TODO: WaitAll 期间如果有新 Submit, 也会等待. 关闭前应先禁止新提交.
    /// </summary>
    class ThreadPool {
    public:
        explicit ThreadPool( size_t num_threads = 0 );
        ~ThreadPool( );

        ThreadPool( const ThreadPool& ) = delete;
        ThreadPool& operator=( const ThreadPool& ) = delete;

        template<typename F>
        auto Submit( F&& task ) -> std::future<decltype( task( ) )> {
            using ReturnType = decltype( task( ) );
            auto packaged = std::make_shared<std::packaged_task<ReturnType( )>>( std::forward<F>( task ) );
            auto future = packaged->get_future( );
            {
                std::lock_guard lock( mutex_ );
                if ( stopping_ ) return future;
                tasks_.emplace( [packaged]( ) { ( *packaged )( ); } );
            }
            cv_.notify_one( );
            ++pending_;
            return future;
        }

        void WaitAll( );
        size_t PendingCount( ) const { return pending_.load( ); }
        size_t ThreadCount( ) const { return workers_.size( ); }
        void Shutdown( );

    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void( )>> tasks_;
        std::mutex mutex_;
        std::condition_variable cv_;
        std::condition_variable done_cv_;
        std::atomic<bool> stopping_{ false };
        std::atomic<size_t> pending_{ 0 };

        void WorkerLoop( );
    };

} // namespace alice
