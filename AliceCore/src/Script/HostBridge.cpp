#include "pch.h"
#include "Script/HostBridge.h"
#include "Host/IHostAPI.h"
#include "Event/EventBus.h"
#include "Script/Lua/LuaEngine.h"
#include "Script/QuickJS/QuickJSEngine.h"
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <Windows.h>
#include <objbase.h>
#endif

namespace alice {

    // 辅助: 分配返回字符串 (调用方用 free_string 释放)
    static char* AllocString( const std::string& s ) {
        auto* buf = new char[s.size( ) + 1];
        std::memcpy( buf, s.c_str( ), s.size( ) + 1 );
        return buf;
    }

    static IHostAPI* Host( void* ctx ) { return static_cast<IHostAPI*>( ctx ); }

    HostBridge CreateHostBridge( IHostAPI* host ) {
        HostBridge b{};
        b.ctx = host;

        // ── event ──
        b.event_on = []( void* ctx, const char* name, void* cb_ctx,
                          void ( *cb )( void*, const char* ) ) -> uint64_t {
            return Host( ctx )->Event( )->OnRaw( name, [cb, cb_ctx]( const EventData& evt ) {
                cb( cb_ctx, evt.DataDump( ).c_str( ) );
            } );
        };

        b.event_emit = []( void* ctx, const char* name, const char* data_json, int ttl ) {
            nlohmann::json data;
            if ( data_json && data_json[0] ) {
                try { data = nlohmann::json::parse( data_json ); } catch ( ... ) {}
            }
            Host( ctx )->Event( )->Emit( name, data, ttl );
        };

        b.event_emit_async = []( void* ctx, const char* name, const char* data_json, int ttl ) {
            nlohmann::json data;
            if ( data_json && data_json[0] ) {
                try { data = nlohmann::json::parse( data_json ); } catch ( ... ) {}
            }
            Host( ctx )->Event( )->EmitAsync( name, data, ttl );
        };

        b.event_off = []( void* ctx, uint64_t handle ) {
            Host( ctx )->Event( )->Off( handle );
        };

        // ── service ──
        b.service_register = []( void* ctx, const char* capability, void* handler_ctx,
                                  const char* ( *handler )( void*, const char*, const char* ) ) -> int {
            auto result = Host( ctx )->Service( )->Register( capability,
                [handler_ctx, handler]( const std::string& method, const nlohmann::json& args ) -> Result<nlohmann::json> {
                    auto* result_str = handler( handler_ctx, method.c_str( ), args.dump( ).c_str( ) );
                    if ( !result_str )
                        return std::unexpected( MakeError( ErrorCode::ServiceCallFailed, "handler returned null" ) );
                    std::string s( result_str );
                    CoTaskMemFree( const_cast<char*>( result_str ) );
                    try { return nlohmann::json::parse( s ); }
                    catch ( ... ) { return nlohmann::json( s ); }
                } );
            return result.has_value( ) ? 0 : -1;
        };

        b.service_call = []( void* ctx, const char* cap, const char* method, const char* args_json ) -> const char* {
            nlohmann::json args;
            if ( args_json && args_json[0] ) {
                try { args = nlohmann::json::parse( args_json ); } catch ( ... ) {}
            }
            auto result = Host( ctx )->Service( )->Call( cap, method, args );
            if ( result ) return AllocString( result->dump( ) );
            return nullptr;
        };

        b.service_wait_for = []( void* ctx, const char* capability, int timeout_ms ) -> int {
            return Host( ctx )->Service( )->WaitFor( capability,
                std::chrono::milliseconds( timeout_ms ) ) ? 1 : 0;
        };

        // ── storage ──
        b.storage_read = []( void* ctx, const char* path ) -> const char* {
            auto result = Host( ctx )->Storage( )->ReadFile( path );
            if ( result ) return AllocString( *result );
            return nullptr;
        };

        b.storage_write = []( void* ctx, const char* path, const char* content ) -> int {
            auto result = Host( ctx )->Storage( )->WriteFile( path, content );
            return result.has_value( ) ? 0 : -1;
        };

        b.storage_exists = []( void* ctx, const char* path ) -> int {
            return Host( ctx )->Storage( )->FileExists( path ) ? 1 : 0;
        };

        // ── net ──
        b.net_fetch = []( void* ctx, const char* url, const char* opts_json ) -> const char* {
            nlohmann::json opts = nlohmann::json::object( );
            if ( opts_json && opts_json[0] ) {
                try { opts = nlohmann::json::parse( opts_json ); } catch ( ... ) {}
            }
            auto result = Host( ctx )->Net( )->Fetch( url, opts );
            if ( result ) return AllocString( result->dump( ) );
            return nullptr;
        };

        b.net_fetch_stream = []( void* ctx, const char* url, const char* opts_json,
                                  void* cb_ctx,
                                  int ( *on_event )( void*, const char*, const char* ) ) -> int {
            nlohmann::json opts = nlohmann::json::object( );
            if ( opts_json && opts_json[0] ) {
                try { opts = nlohmann::json::parse( opts_json ); } catch ( ... ) {}
            }
            auto result = Host( ctx )->Net( )->FetchStream( url, opts,
                [cb_ctx, on_event]( const std::string& type, const std::string& data ) -> bool {
                    return on_event( cb_ctx, type.c_str( ), data.c_str( ) ) == 0;
                } );
            if ( result ) return *result;
            return -1;
        };

        // ── kv ──
        b.kv_get = []( void* ctx, const char* key ) -> const char* {
            auto result = Host( ctx )->Storage( )->KvGet( key );
            if ( result ) return AllocString( result->dump( ) );
            return nullptr;
        };

        b.kv_set = []( void* ctx, const char* key, const char* value_json ) {
            nlohmann::json value;
            if ( value_json && value_json[0] ) {
                try { value = nlohmann::json::parse( value_json ); } catch ( ... ) { value = value_json; }
            }
            (void)Host( ctx )->Storage( )->KvSet( key, value );
        };

        // ── schedule ──
        b.timer_set = []( void* ctx, const char* dur, const char* label, const char* data_json ) -> const char* {
            nlohmann::json data;
            if ( data_json && data_json[0] ) {
                try { data = nlohmann::json::parse( data_json ); } catch ( ... ) {}
            }
            auto id = Host( ctx )->Schedule( )->SetTimer( dur, label, data );
            return AllocString( id );
        };

        b.timer_remove = []( void* ctx, const char* id ) {
            Host( ctx )->Schedule( )->RemoveTimer( id );
        };

        b.timer_list = []( void* ctx ) -> const char* {
            auto timers = Host( ctx )->Schedule( )->ListTimers( );
            nlohmann::json arr = nlohmann::json::array( );
            for ( auto& t : timers ) arr.push_back( t );
            return AllocString( arr.dump( ) );
        };

        // ── log ──
        b.log_info = []( void* ctx, const char* msg ) { Host( ctx )->Log( )->Info( msg ); };
        b.log_warn = []( void* ctx, const char* msg ) { Host( ctx )->Log( )->Warn( msg ); };
        b.log_error = []( void* ctx, const char* msg ) { Host( ctx )->Log( )->Error( msg ); };
        b.log_debug = []( void* ctx, const char* msg ) { Host( ctx )->Log( )->Debug( msg ); };

        // ── platform ──
        b.platform_name = []( [[maybe_unused]] void* ctx ) -> const char* { return "Windows"; }; // 静态字符串不需要释放
        b.platform_data_dir = []( void* ctx ) -> const char* {
            return AllocString( Host( ctx )->Platform( )->DataDir( ) );
        };
        b.platform_exe_dir = []( void* ctx ) -> const char* {
            return AllocString( Host( ctx )->Platform( )->ExeDir( ) );
        };

        // ── script ──
        b.script_eval = []( void* ctx, const char* lang, const char* code ) -> const char* {
            auto* host = Host( ctx );
            HostBridge temp_bridge = CreateHostBridge( host );
            std::string lang_str( lang );

            nlohmann::json result_json;

            if ( lang_str == "lua" ) {
                LuaEngine engine( temp_bridge, host );
                auto r = engine.Execute( code );
                result_json = {
                    { "success", r.success },
                    { "output", r.output },
                    { "error", r.error },
                    { "return_value", r.return_value },
                };
            } else if ( lang_str == "js" ) {
                QuickJSEngine engine( temp_bridge, host );
                auto r = engine.Execute( code );
                result_json = {
                    { "success", r.success },
                    { "output", r.output },
                    { "error", r.error },
                    { "return_value", r.return_value },
                };
            } else {
                result_json = { { "success", false }, { "error", "不支持的语言: " + lang_str } };
            }

            return AllocString( result_json.dump( ) );
        };

        // ── 内存 ──
        b.free_string = []( const char* str ) { delete[] str; };

        return b;
    }

} // namespace alice
