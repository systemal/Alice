#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <stop_token>

namespace alice {

    /// <summary>
    /// 管线阶段执行结果
    /// </summary>
    enum class StageResult {
        Continue,       ///< 继续下一阶段
        Break,          ///< 终止管线, 返回当前数据
        Retry,          ///< 重新执行当前阶段
        Error,          ///< 错误, 终止管线
    };

    /// <summary>
    /// 阶段输出
    /// </summary>
    struct StageOutput {
        StageResult result = StageResult::Continue;
        nlohmann::json data;
        std::string error;
    };

    /// <summary>
    /// 管线执行上下文 (阶段间共享)
    /// </summary>
    struct PipelineContext {
        nlohmann::json shared_data = nlohmann::json::object( );
        std::stop_token cancel;
        TraceID trace_id;
        int current_stage = 0;
        int retry_count = 0;
        int max_retries = 3;
    };

    /// <summary>
    /// 阶段处理函数
    /// </summary>
    using StageHandler = std::function<StageOutput( const nlohmann::json& input, PipelineContext& ctx )>;

    /// <summary>
    /// 通用管线引擎 — 不知道业务, 只按 order 执行阶段.
    /// 插件在 OnLoad 时注册: host->Pipeline()->RegisterStage("chat", "ai.call", 200, handler)
    /// 任何事件都可以触发管线执行, 不绑定特定入口.
    ///
    /// 脚本插件通过返回 json 的 "_action" 字段控制流程:
    /// "continue" (默认) / "break" / "retry" / "error"
    ///
    /// TODO: 没有管线级别的超时. 如果某个 stage 死循环, 整个管线挂住.
    /// 不知道具体业务, 只按 order 依次执行注册的阶段.
    /// </summary>
    class PipelineEngine {
    public:
        /// <summary>
        /// 注册管线阶段
        /// </summary>
        /// <param name="pipeline_name">管线名 (如 "chat")</param>
        /// <param name="stage_name">阶段名 (如 "ai.call")</param>
        /// <param name="order">执行顺序 (越小越先)</param>
        /// <param name="handler">处理函数</param>
        /// <param name="plugin_id">注册者插件 ID</param>
        void RegisterStage( const std::string& pipeline_name,
                             const std::string& stage_name,
                             int order,
                             StageHandler handler,
                             const PluginID& plugin_id = "" );

        /// <summary>
        /// 注销指定插件注册的所有阶段
        /// </summary>
        void UnregisterAll( const PluginID& plugin_id );

        /// <summary>
        /// 执行管线
        /// </summary>
        /// <param name="pipeline_name">管线名</param>
        /// <param name="input">初始输入</param>
        /// <param name="ctx">执行上下文</param>
        Result<nlohmann::json> Execute( const std::string& pipeline_name,
                                         const nlohmann::json& input,
                                         PipelineContext& ctx );

        /// <summary>
        /// 列出管线的所有阶段
        /// </summary>
        struct StageInfo {
            std::string pipeline_name;
            std::string stage_name;
            int order;
            PluginID plugin_id;
        };
        std::vector<StageInfo> ListStages( const std::string& pipeline_name ) const;

    private:
        struct Stage {
            std::string stage_name;
            int order;
            PluginID plugin_id;
            StageHandler handler;
        };

        mutable std::mutex mutex_;
        std::unordered_map<std::string, std::vector<Stage>> pipelines_;
    };

} // namespace alice
