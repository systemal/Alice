#include "pch.h"
#include "Script/ScriptPluginAdapter.h"
#include "Host/IHostAPI.h"
#include "Storage/FileStorage.h"

namespace alice {

    ScriptPluginAdapter::ScriptPluginAdapter( const PluginManifest& manifest )
        : manifest_( manifest ) {
    }

    void ScriptPluginAdapter::OnLoad( IHostAPI* host ) {
        bridge_ = CreateHostBridge( host );

        auto script_path = manifest_.path / manifest_.entry_file;
        auto ext = script_path.extension( ).string( );

        if ( ext == ".lua" ) {
            lua_engine_ = std::make_unique<LuaEngine>( bridge_, host );
            auto result = lua_engine_->ExecuteFile( script_path );
            if ( !result.success ) {
                host->Log( )->Error( "Lua 脚本加载失败: " + result.error );
                return;
            }
            if ( lua_engine_->HasFunction( "onLoad" ) ) {
                auto r = lua_engine_->CallFunction( "onLoad" );
                if ( !r.success ) host->Log( )->Error( "onLoad() 失败: " + r.error );
            }

        } else if ( ext == ".mjs" || ext == ".js" ) {
            js_engine_ = std::make_unique<QuickJSEngine>( bridge_, host );
            auto result = js_engine_->ExecuteFile( script_path );
            if ( !result.success ) {
                host->Log( )->Error( "JS 脚本加载失败: " + result.error );
                return;
            }
            if ( js_engine_->HasFunction( "onLoad" ) ) {
                auto r = js_engine_->CallFunction( "onLoad" );
                if ( !r.success ) host->Log( )->Error( "onLoad() 失败: " + r.error );
            }

        } else {
            host->Log( )->Error( "不支持的脚本类型: " + ext );
        }
    }

    void ScriptPluginAdapter::OnUnload( ) {
        if ( lua_engine_ ) {
            if ( lua_engine_->HasFunction( "onUnload" ) ) lua_engine_->CallFunction( "onUnload" );
            lua_engine_.reset( );
        }
        if ( js_engine_ ) {
            if ( js_engine_->HasFunction( "onUnload" ) ) js_engine_->CallFunction( "onUnload" );
            js_engine_.reset( );
        }
    }

} // namespace alice
