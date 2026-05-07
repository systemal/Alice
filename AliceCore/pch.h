#pragma once

// C++ 标准库
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <unordered_map>
#include <map>
#include <memory>
#include <functional>
#include <optional>
#include <expected>
#include <variant>
#include <filesystem>
#include <chrono>
#include <format>
#include <ranges>
#include <atomic>
#include <mutex>
#include <stop_token>

// 第三方库
#include <nlohmann/json.hpp>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// 平台宏 (不包含 Windows.h, 各 .cpp 按需包含)
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

// 项目基础
#include "Runtime/Types.h"

// 日志
namespace alice {
    class Logger {
    public:
        static void Init( ) {
            auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>( );
            sink->set_level( spdlog::level::trace );
            auto logger = std::make_shared<spdlog::logger>( "alice", sink );
            logger->set_level( spdlog::level::debug );
            logger->set_pattern( "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v" );
            spdlog::set_default_logger( logger );
        }
    };
}

#define ALICE_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define ALICE_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define ALICE_INFO(...)  SPDLOG_INFO(__VA_ARGS__)
#define ALICE_WARN(...)  SPDLOG_WARN(__VA_ARGS__)
#define ALICE_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
