#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <mutex>
#include <atomic>

namespace alice {

    inline TraceID GenerateTraceId( ) {
        static std::atomic<uint64_t> counter{ 0 };
        auto now = std::chrono::steady_clock::now( ).time_since_epoch( ).count( );
        return std::format( "t-{:x}-{}", now, counter.fetch_add( 1 ) );
    }

    /// <summary>
    /// 链路追踪 Span — 记录一次操作的起止时间.
    /// 用 steady_clock 计时 (单调递增, 不受系统时间调整影响).
    /// BeginSpan 返回 span_id (不是引用, 避免 vector 扩容导致野指针).
    /// </summary>
    struct Span {
        TraceID trace_id;
        std::string span_id;
        std::string parent_span_id;
        std::string name;
        int64_t start_us = 0;
        int64_t end_us = 0;
        nlohmann::json attributes;
    };

    /// <summary>
    /// 链路追踪日志.
    /// 记录请求全链路: 事件分发 → 管线阶段 → 服务调用.
    /// 内存中保留最近 N 条, Cleanup 定期清理.
    /// TODO: 目前没有自动 Cleanup, 需要外部调用.
    /// TODO: 没有导出到文件或外部系统 (Jaeger/Zipkin).
    /// </summary>
    class TraceLog {
    public:
        std::string BeginSpan( const TraceID& trace_id, const std::string& name,
                               const std::string& parent_span_id = "" );
        void EndSpan( const std::string& span_id );
        std::vector<Span> GetTrace( const TraceID& trace_id ) const;
        void Cleanup( size_t max_traces = 1000 );

    private:
        mutable std::mutex mutex_;
        std::vector<Span> spans_;
        std::atomic<uint64_t> span_counter_{ 0 };
    };

} // namespace alice
