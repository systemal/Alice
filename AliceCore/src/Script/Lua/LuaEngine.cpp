#include "pch.h"
#include "Script/Lua/LuaEngine.h"
#include "Host/IHostAPI.h"
#include "Script/StdLib.h"

#pragma warning(push)
#pragma warning(disable: 4702 5321)
#include <sol/sol.hpp>
#pragma warning(pop)

namespace alice {

    LuaEngine::LuaEngine( const HostBridge& bridge, IHostAPI* host )
        : bridge_( bridge ), host_( host ) {

        lua_ = std::make_unique<sol::state>( );
        lua_->open_libraries( sol::lib::base, sol::lib::string, sol::lib::table,
                              sol::lib::math, sol::lib::os );
        InjectHostAPI( );
    }

    LuaEngine::~LuaEngine( ) = default;

    void LuaEngine::InjectHostAPI( ) {
        auto& lua = *lua_;
        auto* b = &bridge_;
        auto* host = host_;

        auto alice = lua.create_named_table( "alice" );

        // ── alice.event (简单调用走 bridge, on 走 IHostAPI) ──
        auto event = alice.create_named( "event" );
        event["emit"] = [b]( const std::string& name, sol::optional<sol::table> data ) {
            std::string json_str = "{}";
            if ( data ) {
                nlohmann::json j;
                for ( auto& [k, v] : *data ) {
                    auto key = k.as<std::string>( );
                    if ( v.is<std::string>( ) ) j[key] = v.as<std::string>( );
                    else if ( v.is<int>( ) ) j[key] = v.as<int>( );
                    else if ( v.is<double>( ) ) j[key] = v.as<double>( );
                    else if ( v.is<bool>( ) ) j[key] = v.as<bool>( );
                }
                json_str = j.dump( );
            }
            b->event_emit( b->ctx, name.c_str( ), json_str.c_str( ), -1 );
        };
        event["on"] = [host]( const std::string& name, sol::protected_function callback ) -> uint64_t {
            return host->Event( )->On( name, [callback]( const nlohmann::json& data ) {
                auto json_str = data.dump( );
                callback( json_str );
            } );
        };
        event["emitAsync"] = [b]( const std::string& name, sol::optional<sol::table> data ) {
            std::string json_str = "{}";
            if ( data ) {
                nlohmann::json j;
                for ( auto& [k, v] : *data ) {
                    auto key = k.as<std::string>( );
                    if ( v.is<std::string>( ) ) j[key] = v.as<std::string>( );
                    else if ( v.is<int>( ) ) j[key] = v.as<int>( );
                    else if ( v.is<double>( ) ) j[key] = v.as<double>( );
                    else if ( v.is<bool>( ) ) j[key] = v.as<bool>( );
                }
                json_str = j.dump( );
            }
            b->event_emit_async( b->ctx, name.c_str( ), json_str.c_str( ), -1 );
        };
        event["off"] = [host]( uint64_t handle ) { host->Event( )->Off( handle ); };

        // ── alice.service ──
        auto service = alice.create_named( "service" );
        service["call"] = [b]( const std::string& cap, const std::string& method,
                                sol::optional<std::string> args_json ) -> std::string {
            auto* result = b->service_call( b->ctx, cap.c_str( ), method.c_str( ),
                                             args_json.value_or( "{}" ).c_str( ) );
            if ( result ) { std::string s( result ); b->free_string( result ); return s; }
            return "null";
        };
        service["register"] = [host]( const std::string& capability, sol::protected_function handler ) {
            (void)host->Service( )->Register( capability,
                [handler]( const std::string& method, const nlohmann::json& args ) -> Result<nlohmann::json> {
                    auto result = handler( method, args.dump( ) );
                    if ( !result.valid( ) ) {
                        sol::error err = result;
                        return std::unexpected( MakeError( ErrorCode::ServiceCallFailed, err.what( ) ) );
                    }
                    std::string ret = result;
                    try { return nlohmann::json::parse( ret ); }
                    catch ( ... ) { return nlohmann::json( ret ); }
                } );
        };
        service["waitFor"] = [host]( const std::string& capability, sol::optional<int> timeout_ms ) -> bool {
            return host->Service( )->WaitFor( capability,
                std::chrono::milliseconds( timeout_ms.value_or( 5000 ) ) );
        };
        service["list"] = [host]( ) -> std::string {
            auto list = host->Service( )->List( );
            return nlohmann::json( list ).dump( );
        };

        // ── alice.log ──
        auto log = alice.create_named( "log" );
        log["info"] = [b]( const std::string& msg ) { b->log_info( b->ctx, msg.c_str( ) ); };
        log["warn"] = [b]( const std::string& msg ) { b->log_warn( b->ctx, msg.c_str( ) ); };
        log["error"] = [b]( const std::string& msg ) { b->log_error( b->ctx, msg.c_str( ) ); };
        log["debug"] = [b]( const std::string& msg ) { b->log_debug( b->ctx, msg.c_str( ) ); };

        // ── alice.fs ──
        auto fs = alice.create_named( "fs" );
        fs["read"] = [b]( const std::string& path ) -> sol::optional<std::string> {
            auto* result = b->storage_read( b->ctx, path.c_str( ) );
            if ( result ) { std::string s( result ); b->free_string( result ); return s; }
            return sol::nullopt;
        };
        fs["write"] = [b]( const std::string& path, const std::string& content ) -> bool {
            return b->storage_write( b->ctx, path.c_str( ), content.c_str( ) ) == 0;
        };
        fs["exists"] = [b]( const std::string& path ) -> bool {
            return b->storage_exists( b->ctx, path.c_str( ) ) != 0;
        };

        // ── alice.net ──
        auto net = alice.create_named( "net" );
        net["fetch"] = [b]( const std::string& url, sol::optional<std::string> opts_json ) -> sol::optional<std::string> {
            auto* result = b->net_fetch( b->ctx, url.c_str( ), opts_json.value_or( "{}" ).c_str( ) );
            if ( result ) { std::string s( result ); b->free_string( result ); return s; }
            return sol::nullopt;
        };
        net["fetch_stream"] = [b]( const std::string& url, const std::string& opts_json,
                                     sol::protected_function on_event ) -> int {
            struct Ctx { sol::protected_function* fn; };
            Ctx ctx{ &on_event };
            return b->net_fetch_stream( b->ctx, url.c_str( ), opts_json.c_str( ),
                &ctx, []( void* cb_ctx, const char* event_type, const char* event_data ) -> int {
                    auto* c = static_cast<Ctx*>( cb_ctx );
                    auto result = ( *c->fn )( std::string( event_type ), std::string( event_data ) );
                    if ( !result.valid( ) ) return -1;
                    if ( result.get_type( ) == sol::type::boolean && result.get<bool>( ) == false ) return -1;
                    return 0;
                } );
        };

        net["addRoute"] = [host]( const std::string& method, const std::string& path, sol::protected_function handler ) {
            host->Net( )->AddRoute( method, path,
                [handler]( const nlohmann::json& req ) -> nlohmann::json {
                    auto result = handler( req.dump( ) );
                    if ( !result.valid( ) ) return nlohmann::json{ { "status", 500 }, { "error", "Lua handler 错误" } };
                    std::string ret = result;
                    try { return nlohmann::json::parse( ret ); }
                    catch ( ... ) { return nlohmann::json{ { "body", ret } }; }
                } );
        };

        // ── alice.ws ──
        auto ws = alice.create_named( "ws" );
        ws["handle"] = [host]( const std::string& type, sol::protected_function handler ) {
            host->Net( )->AddWsHandler( type,
                [handler]( const nlohmann::json& req ) -> nlohmann::json {
                    auto result = handler( req.dump( ) );
                    if ( !result.valid( ) ) return nlohmann::json{ { "error", "Lua handler 错误" } };
                    std::string ret = result;
                    try { return nlohmann::json::parse( ret ); }
                    catch ( ... ) { return nlohmann::json{ { "result", ret } }; }
                } );
        };
        ws["broadcast"] = [host]( const std::string& session_id, sol::optional<sol::table> data ) {
            nlohmann::json j;
            if ( data ) {
                for ( auto& [k, v] : *data ) {
                    auto key = k.as<std::string>( );
                    if ( v.is<std::string>( ) ) j[key] = v.as<std::string>( );
                    else if ( v.is<int>( ) ) j[key] = v.as<int>( );
                    else if ( v.is<double>( ) ) j[key] = v.as<double>( );
                    else if ( v.is<bool>( ) ) j[key] = v.as<bool>( );
                }
            }
            host->Net( )->WsBroadcast( session_id, j );
        };

        // ── alice.kv ──
        auto kv = alice.create_named( "kv" );
        kv["get"] = [b]( const std::string& key ) -> sol::optional<std::string> {
            auto* result = b->kv_get( b->ctx, key.c_str( ) );
            if ( result ) { std::string s( result ); b->free_string( result ); return s; }
            return sol::nullopt;
        };
        kv["set"] = [b]( const std::string& key, const std::string& value_json ) {
            b->kv_set( b->ctx, key.c_str( ), value_json.c_str( ) );
        };

        // ── alice.timer ──
        auto timer = alice.create_named( "timer" );
        timer["set"] = [b]( const std::string& dur, const std::string& label,
                             sol::optional<std::string> data_json ) -> std::string {
            auto* id = b->timer_set( b->ctx, dur.c_str( ), label.c_str( ),
                                      data_json.value_or( "{}" ).c_str( ) );
            if ( id ) { std::string s( id ); b->free_string( id ); return s; }
            return "";
        };
        timer["remove"] = [b]( const std::string& id ) { b->timer_remove( b->ctx, id.c_str( ) ); };
        timer["list"] = [b]( ) -> std::string {
            auto* result = b->timer_list( b->ctx );
            if ( result ) { std::string s( result ); b->free_string( result ); return s; }
            return "[]";
        };

        // ── alice.pipeline ──
        auto pipeline = alice.create_named( "pipeline" );
        pipeline["register"] = [host]( const std::string& pipe_name, const std::string& stage_name,
                                        int order, sol::protected_function handler ) {
            host->Pipeline( )->RegisterStage( pipe_name, stage_name, order,
                [handler]( const nlohmann::json& input, nlohmann::json& shared ) -> nlohmann::json {
                    auto result = handler( input.dump( ), shared.dump( ) );
                    if ( !result.valid( ) ) return nlohmann::json{ { "error", "Lua stage 错误" } };
                    std::string ret = result;
                    try { return nlohmann::json::parse( ret ); }
                    catch ( ... ) { return nlohmann::json( ret ); }
                } );
        };
        pipeline["execute"] = [host]( const std::string& pipe_name, sol::optional<std::string> input_json ) -> std::string {
            auto result = host->Pipeline( )->Execute( pipe_name,
                input_json.has_value( ) ? nlohmann::json::parse( *input_json ) : nlohmann::json::object( ) );
            if ( result ) return result->dump( );
            return nlohmann::json{ { "error", result.error( ).message } }.dump( );
        };

        // ── alice.platform ──
        auto platform = alice.create_named( "platform" );
        platform["name"] = [b]( ) -> std::string { return b->platform_name( b->ctx ); };
        platform["dataDir"] = [b]( ) -> std::string {
            auto* s = b->platform_data_dir( b->ctx );
            std::string result( s ); b->free_string( s ); return result;
        };

        // ── alice.json ──
        auto json = alice.create_named( "json" );
        json["encode"] = [&lua]( sol::object obj ) -> std::string {
            std::function<nlohmann::json( sol::object )> to_json;
            to_json = [&to_json]( sol::object o ) -> nlohmann::json {
                if ( o.is<bool>( ) ) return o.as<bool>( );
                if ( o.is<int64_t>( ) ) return o.as<int64_t>( );
                if ( o.is<double>( ) ) return o.as<double>( );
                if ( o.is<std::string>( ) ) return o.as<std::string>( );
                if ( o.is<sol::table>( ) ) {
                    auto t = o.as<sol::table>( );
                    // 判断是 array 还是 object: 检查 key 1 是否存在
                    bool is_array = t[1].valid( );
                    if ( is_array ) {
                        nlohmann::json arr = nlohmann::json::array( );
                        for ( size_t i = 1; i <= t.size( ); ++i ) arr.push_back( to_json( t[i] ) );
                        return arr;
                    }
                    nlohmann::json obj = nlohmann::json::object( );
                    for ( auto& [k, v] : t ) {
                        if ( k.is<std::string>( ) ) obj[k.as<std::string>( )] = to_json( v );
                    }
                    return obj;
                }
                if ( o.is<sol::nil_t>( ) || !o.valid( ) ) return nullptr;
                return nullptr;
            };
            return to_json( obj ).dump( );
        };
        json["decode"] = [&lua]( const std::string& str ) -> sol::object {
            std::function<sol::object( sol::state&, const nlohmann::json& )> to_lua;
            to_lua = [&to_lua]( sol::state& L, const nlohmann::json& j ) -> sol::object {
                if ( j.is_null( ) ) return sol::nil;
                if ( j.is_boolean( ) ) return sol::make_object( L, j.get<bool>( ) );
                if ( j.is_number_integer( ) ) return sol::make_object( L, j.get<int64_t>( ) );
                if ( j.is_number_float( ) ) return sol::make_object( L, j.get<double>( ) );
                if ( j.is_string( ) ) return sol::make_object( L, j.get<std::string>( ) );
                if ( j.is_array( ) ) {
                    sol::table t = L.create_table( );
                    for ( size_t i = 0; i < j.size( ); ++i ) t[i + 1] = to_lua( L, j[i] );
                    return t;
                }
                if ( j.is_object( ) ) {
                    sol::table t = L.create_table( );
                    for ( auto& [k, v] : j.items( ) ) t[k] = to_lua( L, v );
                    return t;
                }
                return sol::nil;
            };
            try {
                auto j = nlohmann::json::parse( str );
                return to_lua( lua, j );
            }
            catch ( ... ) { return sol::nil; }
        };

        // ── alice.process ──
        auto process = alice.create_named( "process" );
        process["exec"] = []( const std::string& cmd, sol::optional<std::string> opts_json ) -> std::string {
            nlohmann::json opts = nlohmann::json::object( );
            if ( opts_json ) { try { opts = nlohmann::json::parse( *opts_json ); } catch ( ... ) {} }
            auto result = stdlib::ProcessExec( cmd, opts );
            if ( result ) return result->dump( );
            return nlohmann::json{ { "error", result.error( ).message } }.dump( );
        };

        // ── alice.path ──
        auto path = alice.create_named( "path" );
        path["join"] = stdlib::PathJoin;
        path["dirname"] = stdlib::PathDirname;
        path["basename"] = stdlib::PathBasename;
        path["ext"] = stdlib::PathExtension;
        path["absolute"] = stdlib::PathAbsolute;

        // ── alice.regex ──
        auto regex = alice.create_named( "regex" );
        regex["match"] = []( const std::string& text, const std::string& pattern ) -> sol::optional<std::string> {
            auto result = stdlib::RegexMatch( text, pattern );
            if ( result ) return result->dump( );
            return sol::nullopt;
        };
        regex["replace"] = stdlib::RegexReplace;
        regex["test"] = stdlib::RegexTest;

        // ── alice.encoding ──
        auto encoding = alice.create_named( "encoding" );
        encoding["base64encode"] = stdlib::Base64Encode;
        encoding["base64decode"] = stdlib::Base64Decode;
        encoding["hex"] = stdlib::HexEncode;

        // ── alice.time ──
        auto time = alice.create_named( "time" );
        time["now"] = stdlib::TimeNow;
        time["format"] = stdlib::TimeFormat;

        // ── alice.script ──
        auto script = alice.create_named( "script" );
        script["eval"] = [b]( const std::string& lang, const std::string& code ) -> std::string {
            auto* result = b->script_eval( b->ctx, lang.c_str( ), code.c_str( ) );
            if ( result ) { std::string s( result ); b->free_string( result ); return s; }
            return R"({"success":false,"error":"eval 失败"})";
        };

        // ── print 捕获 ──
        lua["print"] = [this]( sol::variadic_args va ) {
            for ( size_t i = 0; i < va.size( ); ++i ) {
                if ( i > 0 ) output_buffer_ += "\t";
                output_buffer_ += va[i].as<std::string>( );
            }
            output_buffer_ += "\n";
        };
    }

    LuaResult LuaEngine::Execute( const std::string& code ) {
        output_buffer_.clear( );
        LuaResult result;
        auto r = lua_->safe_script( code, sol::script_pass_on_error );
        if ( !r.valid( ) ) { sol::error err = r; result.error = err.what( ); }
        else { result.success = true; if ( r.get_type( ) == sol::type::string ) result.return_value = r.get<std::string>( ); }
        result.output = output_buffer_;
        return result;
    }

    LuaResult LuaEngine::ExecuteFile( const std::filesystem::path& path ) {
        output_buffer_.clear( );
        LuaResult result;
        auto r = lua_->safe_script_file( path.string( ), sol::script_pass_on_error );
        if ( !r.valid( ) ) { sol::error err = r; result.error = err.what( ); }
        else { result.success = true; }
        result.output = output_buffer_;
        return result;
    }

    LuaResult LuaEngine::CallFunction( const std::string& func_name, const std::string& args_json ) {
        output_buffer_.clear( );
        LuaResult result;
        sol::protected_function func = ( *lua_ )[func_name];
        if ( !func.valid( ) ) { result.error = "函数 '" + func_name + "' 不存在"; return result; }

        sol::protected_function_result r = args_json.empty( ) ? func( ) : func( args_json );
        if ( !r.valid( ) ) { sol::error err = r; result.error = err.what( ); }
        else { result.success = true; if ( r.get_type( ) == sol::type::string ) result.return_value = r.get<std::string>( ); }
        result.output = output_buffer_;
        return result;
    }

    bool LuaEngine::HasFunction( const std::string& func_name ) const {
        return ( *lua_ )[func_name].is<sol::protected_function>( );
    }

} // namespace alice
