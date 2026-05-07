#pragma once

#include <string>
#include <string_view>
#include <functional>

namespace alice {

    struct SseEvent {
        std::string type;   ///< "event:" 行的值, 很多 LLM API 不设置 (空字符串)
        std::string data;   ///< "data:" 行的值, 多行 data 用 \n 拼接
    };

    /// <summary>
    /// SSE (Server-Sent Events) 增量解析器.
    ///
    /// 网络数据是分块到达的 (TCP 分片), 一个 SSE 事件可能跨多个 chunk.
    /// Feed() 接受任意大小的 chunk, 内部缓冲, 按 \n\n 边界切分事件.
    ///
    /// 各 LLM API 的 SSE 格式:
    /// - OpenAI: data: {"choices":[...]}  最后 data: [DONE]
    /// - Claude: event: content_block_delta\ndata: {"delta":{"text":"..."}}
    /// - Gemini: data: {"candidates":[...]}
    ///
    /// 用法: parser.Feed(chunk, [](const SseEvent& evt) { ... return true; });
    /// </summary>
    class SseParser {
    public:
        using EventCallback = std::function<bool( const SseEvent& )>;

        /// <returns>false 表示回调要求中止</returns>
        bool Feed( std::string_view chunk, const EventCallback& on_event );
        void Reset( );

    private:
        std::string buffer_;
        std::string current_type_;
        std::string current_data_;
    };

} // namespace alice
