#pragma once

#include "Runtime/Types.h"
#include <cstdint>

namespace alice {

    class IHostAPI;

    /// <summary>
    /// C ABI 宿主函数桥接 — 跨语言调用的核心.
    ///
    /// 所有脚本语言 (Lua/JS/C#) 通过这个函数表访问 Core 能力.
    /// 参数只用 C 类型: const char*, int, double, void*, uint64_t.
    /// 复杂数据通过 JSON 字符串传递.
    ///
    /// 为什么不直接暴露 IHostAPI?
    /// - IHostAPI 用了 std::string, nlohmann::json, std::function — 这些跨 DLL 不安全
    /// - Lua (sol2) 和 QuickJS (C API) 需要 C 函数指针
    /// - C# 通过 P/Invoke 调用, 只认 C ABI
    /// - 所以 HostBridge 是 IHostAPI 的 "C 投影"
    ///
    /// 内存约定:
    /// - C++ → 脚本: AllocString(new[]) 分配, 脚本侧调 free_string(delete[]) 释放
    /// - C# → C++ (回调返回值): CoTaskMemAlloc 分配, C++ 用 CoTaskMemFree 释放
    /// - Lua/JS → C++ (回调返回值): 由各引擎管理, 不需要手动释放
    ///
    /// TODO: 函数指针数量已经很多 (25+), 未来考虑按模块分组成子结构体.
    /// TODO: 目前没有版本号, ABI 变更会破坏二进制兼容. 考虑加 version 字段.
    /// </summary>
    struct HostBridge {
        void* ctx = nullptr;  ///< 不透明上下文 (内部是 IHostAPI*, 生命周期由 Runtime 管理)

        // ── event ──
        uint64_t ( *event_on )( void* ctx, const char* event_name, void* callback_ctx,
                                 void ( *callback )( void* cb_ctx, const char* data_json ) ) = nullptr;
        void ( *event_emit )( void* ctx, const char* event_name, const char* data_json, int ttl ) = nullptr;
        void ( *event_emit_async )( void* ctx, const char* event_name, const char* data_json, int ttl ) = nullptr;
        void ( *event_off )( void* ctx, uint64_t handle ) = nullptr;

        // ── service ──
        /// <summary>
        /// 注册服务. handler 是一个 C 函数指针, 收到 method + args_json, 返回 result_json.
        /// 返回值内存: C# 用 CoTaskMemAlloc, C++ 用 CoTaskMemFree 释放.
        /// </summary>
        int ( *service_register )( void* ctx, const char* capability, void* handler_ctx,
                                    const char* ( *handler )( void* h_ctx, const char* method, const char* args_json ) ) = nullptr;
        /// <returns>JSON 字符串 (调用方用 free_string 释放), 服务不存在返回 nullptr</returns>
        const char* ( *service_call )( void* ctx, const char* capability, const char* method, const char* args_json ) = nullptr;
        /// <summary>
        /// 阻塞等待服务注册. 用条件变量实现, 不是轮询.
        /// </summary>
        int ( *service_wait_for )( void* ctx, const char* capability, int timeout_ms ) = nullptr;

        // ── storage ──
        const char* ( *storage_read )( void* ctx, const char* path ) = nullptr;   ///< 返回文件内容或 nullptr
        int ( *storage_write )( void* ctx, const char* path, const char* content ) = nullptr;  ///< 0=成功
        int ( *storage_exists )( void* ctx, const char* path ) = nullptr;  ///< 0=不存在, 1=存在

        // ── net ──
        const char* ( *net_fetch )( void* ctx, const char* url, const char* opts_json ) = nullptr;
        /// <summary>
        /// 流式 HTTP + SSE 解析. 每收到一个 SSE event 调用 on_event.
        /// on_event 返回 0 继续, 非 0 中止流.
        /// 调用阻塞直到流结束, 但中间每个 event 同步回调 (同一线程, 无跨线程问题).
        /// </summary>
        int ( *net_fetch_stream )( void* ctx, const char* url, const char* opts_json,
                                    void* callback_ctx,
                                    int ( *on_event )( void* cb_ctx, const char* event_type, const char* event_data ) ) = nullptr;

        // ── kv (per-plugin 命名空间隔离) ──
        const char* ( *kv_get )( void* ctx, const char* key ) = nullptr;
        void ( *kv_set )( void* ctx, const char* key, const char* value_json ) = nullptr;

        // ── schedule ──
        const char* ( *timer_set )( void* ctx, const char* duration, const char* label, const char* data_json ) = nullptr;
        void ( *timer_remove )( void* ctx, const char* id ) = nullptr;
        const char* ( *timer_list )( void* ctx ) = nullptr;

        // ── log (自动加 [plugin_id] 前缀) ──
        void ( *log_info )( void* ctx, const char* msg ) = nullptr;
        void ( *log_warn )( void* ctx, const char* msg ) = nullptr;
        void ( *log_error )( void* ctx, const char* msg ) = nullptr;
        void ( *log_debug )( void* ctx, const char* msg ) = nullptr;

        // ── platform ──
        const char* ( *platform_name )( void* ctx ) = nullptr;      ///< "Windows" — 静态字符串, 不需要 free
        const char* ( *platform_data_dir )( void* ctx ) = nullptr;   ///< 需要 free_string
        const char* ( *platform_exe_dir )( void* ctx ) = nullptr;    ///< 需要 free_string

        // ── script (临时引擎执行) ──
        /// <summary>
        /// 创建临时 Lua/JS 引擎执行代码. 引擎注入完整 alice.* API.
        /// 用于 <run-lua>/<run-js> handler 标签.
        /// 返回 JSON: {"success":bool, "output":"", "error":"", "return_value":""}
        /// </summary>
        const char* ( *script_eval )( void* ctx, const char* lang, const char* code ) = nullptr;

        // ── 内存管理 ──
        void ( *free_string )( const char* str ) = nullptr;  ///< 释放 C++ 侧 AllocString 分配的字符串
    };

    /// <summary>
    /// 从 IHostAPI 创建 HostBridge 函数表.
    /// 每个插件的 HostBridge 共享同一套函数指针, 只有 ctx 不同 (指向各自的 IHostAPI).
    /// </summary>
    HostBridge CreateHostBridge( IHostAPI* host );

} // namespace alice
