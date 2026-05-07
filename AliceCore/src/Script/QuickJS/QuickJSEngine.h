#pragma once

#include "Runtime/Types.h"
#include "Script/HostBridge.h"
#include <string>
#include <filesystem>
#include <memory>

struct JSRuntime;
struct JSContext;

namespace alice {

    class IHostAPI;

    struct JsResult {
        bool success = false;
        std::string output;
        std::string error;
        std::string return_value;
    };

    /// <summary>
    /// QuickJS 脚本引擎 — quickjs-ng C API.
    /// 和 LuaEngine 对称: 同样的接口, 同样的 alice.* API, 同样的 HostBridge + IHostAPI 双通道.
    ///
    /// GC 安全: 回调函数用 JS_DupValue 防止回收, 析构时统一 JS_FreeValue.
    /// 上下文数据: QjsCtxData 通过 JS_SetContextOpaque 存储, 所有 C 回调通过它访问 bridge/host.
    /// </summary>
    class QuickJSEngine {
    public:
        QuickJSEngine( const HostBridge& bridge, IHostAPI* host );
        ~QuickJSEngine( );

        QuickJSEngine( const QuickJSEngine& ) = delete;
        QuickJSEngine& operator=( const QuickJSEngine& ) = delete;

        JsResult Execute( const std::string& code );
        JsResult ExecuteFile( const std::filesystem::path& path );
        JsResult CallFunction( const std::string& func_name, const std::string& args_json = "" );
        bool HasFunction( const std::string& func_name ) const;

    private:
        JSRuntime* rt_ = nullptr;
        JSContext* ctx_ = nullptr;
        HostBridge bridge_;
        IHostAPI* host_;

        void InjectHostAPI( );
    };

} // namespace alice
