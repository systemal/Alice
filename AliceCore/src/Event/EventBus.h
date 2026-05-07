#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <vector>
#include <mutex>
#include <atomic>

namespace alice {

    /// <summary>
    /// 事件元数据 — 每次 Emit 自动附带.
    /// trace_id 用于链路追踪, ttl 防止无限递归.
    /// </summary>
    struct EventMeta {
        PluginID source_plugin;
        TraceID trace_id;
        int ttl = 10;               ///< 每次分发减 1, 到 0 丢弃 + 告警
        int64_t timestamp = 0;      ///< 微秒时间戳 (steady_clock)
    };

    /// <summary>
    /// 事件数据包.
    /// DataDump() 提供懒序列化 — 多个脚本订阅者只 dump 一次.
    /// C++ 订阅者直接用 data (const json&), 不触发序列化.
    /// </summary>
    struct EventData {
        std::string name;
        nlohmann::json data;
        EventMeta meta;

        const std::string& DataDump( ) const {
            if ( cached_dump_.empty( ) && !data.is_null( ) )
                cached_dump_ = data.dump( );
            return cached_dump_;
        }
    private:
        mutable std::string cached_dump_;
    };

    using EventCallback = std::function<void( const EventData& )>;

    /// <summary>
    /// 事件治理配置.
    /// TODO: max_events_per_sec 和 dedup 声明了但未实现.
    ///       频率限制需要滑动窗口计数器, 去重需要 trace_id + event_name 的 LRU 缓存.
    ///       目前只有 TTL 和链路深度生效.
    /// </summary>
    struct EventGuardConfig {
        int max_chain_depth = 32;
        int max_events_per_sec = 1000;  ///< TODO: 未实现
        bool dedup = true;              ///< TODO: 未实现
    };

    class ThreadPool;

    /// <summary>
    /// 事件总线 — 插件间通信的主要通道.
    ///
    /// 设计:
    /// - 分发时不持锁 (先收集匹配的 callback, 释放锁, 再逐个调用)
    /// - 回调可能触发新的 Emit (递归), 用 thread_local 深度计数 + TTL 防止爆栈
    /// - EmitAsync 投递到线程池, 适合不需要同步结果的通知类事件
    ///
    /// TODO: 目前只支持精确匹配事件名, 未来可加通配符 ("chat.*")
    /// TODO: 订阅者没有优先级, 按注册顺序调用
    /// </summary>
    class EventBus {
    public:
        explicit EventBus( EventGuardConfig guard_config = {} );
        void SetThreadPool( ThreadPool* pool ) { thread_pool_ = pool; }

        EventHandle Subscribe( const std::string& event_name, EventCallback callback,
                                const PluginID& subscriber_plugin = "" );
        void Unsubscribe( EventHandle handle );
        void UnsubscribeAll( const PluginID& plugin_id );

        /// <summary>
        /// 同步分发 — 在调用者线程中执行所有 callback.
        /// ttl = -1 使用默认值 10.
        /// </summary>
        void Emit( const std::string& name, const nlohmann::json& data = {},
                   const PluginID& source_plugin = "",
                   const TraceID& trace_id = "",
                   int ttl = -1 );

        /// <summary>
        /// 异步分发 — 投递到线程池, 立即返回.
        /// 注意: 回调在线程池线程中执行, 不能安全访问调用者的栈变量.
        /// 如果 thread_pool_ 未设置, 退化为同步 Emit.
        /// </summary>
        void EmitAsync( const std::string& name, const nlohmann::json& data = {},
                        const PluginID& source_plugin = "",
                        const TraceID& trace_id = "",
                        int ttl = -1 );

        struct Stats {
            uint64_t total_emitted = 0;
            uint64_t total_delivered = 0;
            uint64_t total_blocked = 0;
            size_t subscriber_count = 0;
        };
        Stats GetStats( ) const;

    private:
        struct Subscriber {
            EventHandle handle;
            std::string event_name;
            PluginID plugin_id;
            EventCallback callback;
        };

        EventGuardConfig guard_config_;
        std::vector<Subscriber> subscribers_;
        mutable std::mutex mutex_;
        std::atomic<EventHandle> next_handle_{ 1 };

        std::atomic<uint64_t> total_emitted_{ 0 };
        std::atomic<uint64_t> total_delivered_{ 0 };
        std::atomic<uint64_t> total_blocked_{ 0 };
        static thread_local int current_chain_depth_;
        ThreadPool* thread_pool_ = nullptr;

        bool GuardCheck( const EventData& event );
    };

} // namespace alice
