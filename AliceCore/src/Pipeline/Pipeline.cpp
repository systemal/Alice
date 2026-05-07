#include "pch.h"
#include "Pipeline/Pipeline.h"

namespace alice {

    void PipelineEngine::RegisterStage( const std::string& pipeline_name,
                                         const std::string& stage_name,
                                         int order,
                                         StageHandler handler,
                                         const PluginID& plugin_id ) {
        std::lock_guard lock( mutex_ );

        auto& stages = pipelines_[ pipeline_name ];
        stages.push_back( {
            .stage_name = stage_name,
            .order = order,
            .plugin_id = plugin_id,
            .handler = std::move( handler ),
        } );

        // 按 order 排序
        std::ranges::sort( stages, []( auto& a, auto& b ) { return a.order < b.order; } );

        ALICE_DEBUG( "管线 '{}' 注册阶段: {} (order={}, plugin={})",
                    pipeline_name, stage_name, order, plugin_id );
    }

    void PipelineEngine::UnregisterAll( const PluginID& plugin_id ) {
        std::lock_guard lock( mutex_ );
        for ( auto& [name, stages] : pipelines_ ) {
            std::erase_if( stages, [&]( auto& s ) { return s.plugin_id == plugin_id; } );
        }
    }

    Result<nlohmann::json> PipelineEngine::Execute( const std::string& pipeline_name,
                                                     const nlohmann::json& input,
                                                     PipelineContext& ctx ) {
        // 复制阶段列表 (不持锁执行)
        std::vector<Stage> stages;
        {
            std::lock_guard lock( mutex_ );
            auto it = pipelines_.find( pipeline_name );
            if ( it == pipelines_.end( ) || it->second.empty( ) ) {
                return std::unexpected( MakeError( ErrorCode::NotFound,
                    std::format( "管线 '{}' 未注册或没有阶段", pipeline_name ) ) );
            }
            stages = it->second;
        }

        nlohmann::json current_data = input;

        for ( size_t i = 0; i < stages.size( ); ++i ) {
            auto& stage = stages[ i ];
            ctx.current_stage = static_cast<int>( i );
            ctx.retry_count = 0;

            while ( true ) {
                // 取消检查
                if ( ctx.cancel.stop_requested( ) ) {
                    return std::unexpected( MakeError( ErrorCode::PipelineAborted, "管线被取消" ) );
                }

                ALICE_DEBUG( "管线 '{}' 执行阶段: {} ({}/{})",
                            pipeline_name, stage.stage_name, i + 1, stages.size( ) );

                StageOutput output;
                try {
                    output = stage.handler( current_data, ctx );
                }
                catch ( const std::exception& e ) {
                    return std::unexpected( MakeError( ErrorCode::PipelineStageFailed,
                        std::format( "管线 '{}' 阶段 '{}' 异常: {}",
                                    pipeline_name, stage.stage_name, e.what( ) ) ) );
                }

                switch ( output.result ) {
                case StageResult::Continue:
                    current_data = std::move( output.data );
                    break; // 继续下一阶段

                case StageResult::Break:
                    return output.data; // 正常终止

                case StageResult::Retry:
                    ++ctx.retry_count;
                    if ( ctx.retry_count > ctx.max_retries ) {
                        return std::unexpected( MakeError( ErrorCode::PipelineMaxRetries,
                            std::format( "管线 '{}' 阶段 '{}' 超过最大重试次数 ({})",
                                        pipeline_name, stage.stage_name, ctx.max_retries ) ) );
                    }
                    ALICE_DEBUG( "管线 '{}' 阶段 '{}' 重试 ({}/{})",
                                pipeline_name, stage.stage_name, ctx.retry_count, ctx.max_retries );
                    continue; // 重新执行当前阶段

                case StageResult::Error:
                    return std::unexpected( MakeError( ErrorCode::PipelineStageFailed,
                        std::format( "管线 '{}' 阶段 '{}' 错误: {}",
                                    pipeline_name, stage.stage_name, output.error ) ) );
                }

                break; // Continue → 跳出 retry while, 进下一个 stage
            }
        }

        return current_data;
    }

    std::vector<PipelineEngine::StageInfo> PipelineEngine::ListStages( const std::string& pipeline_name ) const {
        std::lock_guard lock( mutex_ );
        std::vector<StageInfo> result;
        auto it = pipelines_.find( pipeline_name );
        if ( it == pipelines_.end( ) ) return result;

        for ( auto& s : it->second ) {
            result.push_back( {
                .pipeline_name = pipeline_name,
                .stage_name = s.stage_name,
                .order = s.order,
                .plugin_id = s.plugin_id,
            } );
        }
        return result;
    }

} // namespace alice
