#pragma once

#include "Runtime/Types.h"
#include "Script/HostBridge.h"
#include <string>
#include <filesystem>
#include <memory>

namespace sol { class state; }

namespace alice {

    class IHostAPI;

    struct LuaResult {
        bool success = false;
        std::string output;
        std::string error;
        std::string return_value;
    };

    /// <summary>
    /// Lua 脚本引擎 (sol2).
    /// 简单调用走 HostBridge (C ABI), 回调注册直接走 IHostAPI.
    /// </summary>
    /// <summary>
    /// Lua 脚本引擎 — sol2 封装.
    /// 简单调用 (fetch/read/write) 走 HostBridge C ABI.
    /// 回调注册 (service.register/event.on/addRoute) 直接走 IHostAPI (需要 closure).
    ///
    /// alice.* 全局表在构造时注入, 覆盖 17 个命名空间.
    /// print() 被捕获到 output_buffer_, 不输出到 stdout.
    /// </summary>
    class LuaEngine {
    public:
        /// <param name="bridge">C ABI 桥接 (fetch/read/write 等)</param>
        /// <param name="host">IHostAPI (register/route 等回调类)</param>
        LuaEngine( const HostBridge& bridge, IHostAPI* host );
        ~LuaEngine( );

        LuaEngine( const LuaEngine& ) = delete;
        LuaEngine& operator=( const LuaEngine& ) = delete;

        LuaResult Execute( const std::string& code );
        LuaResult ExecuteFile( const std::filesystem::path& path );
        LuaResult CallFunction( const std::string& func_name, const std::string& args_json = "" );
        bool HasFunction( const std::string& func_name ) const;

    private:
        std::unique_ptr<sol::state> lua_;
        HostBridge bridge_;
        IHostAPI* host_;
        std::string output_buffer_;

        void InjectHostAPI( );
    };

} // namespace alice
