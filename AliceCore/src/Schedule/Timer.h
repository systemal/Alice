#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <mutex>
#include <chrono>
#include <functional>
#include <atomic>
#include <thread>

namespace alice {

    /// <summary>
    /// 定时器条目
    /// </summary>
    struct TimerEntry {
        TimerID id;
        std::string label;
        nlohmann::json data;                                ///< 到期时传给回调的自定义数据
        std::chrono::steady_clock::time_point fire_time;
        PluginID owner_plugin;                              ///< 注册此定时器的插件
    };

    /// <summary>
    /// 定时器到期回调
    /// </summary>
    using TimerCallback = std::function<void( const TimerEntry& )>;

    /// <summary>
    /// 定时器调度器.
    /// 后台线程每秒检查到期, 到期时调用回调.
    /// </summary>
    class TimerScheduler {
    public:
        TimerScheduler( ) = default;
        ~TimerScheduler( );

        /// <summary>
        /// 启动后台检查线程
        /// </summary>
        /// <param name="on_expired">到期回调</param>
        void Start( TimerCallback on_expired );
        void Stop( );

        /// <summary>
        /// 添加定时器
        /// </summary>
        TimerID Add( std::chrono::seconds duration, const std::string& label,
                     const nlohmann::json& data, const PluginID& owner );

        /// <summary>
        /// 移除定时器
        /// </summary>
        bool Remove( const TimerID& id );

        /// <summary>
        /// 列出活跃定时器
        /// </summary>
        std::vector<TimerEntry> ListActive( ) const;

    private:
        mutable std::mutex mutex_;
        std::vector<TimerEntry> timers_;
        std::atomic<int> next_id_{ 1 };
        std::thread thread_;
        std::atomic<bool> running_{ false };
        TimerCallback on_expired_;

        void ScheduleLoop( );
    };

} // namespace alice
