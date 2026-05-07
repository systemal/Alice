#include "pch.h"
#include "Script/QuickJS/QuickJSEngine.h"
#include "Host/IHostAPI.h"
#include "Script/StdLib.h"
#include "Storage/FileStorage.h"

#pragma warning(push)
#pragma warning(disable: 4244)
extern "C" {
#include <quickjs.h>
}
#pragma warning(pop)

namespace alice {

    struct QjsCtxData {
        HostBridge* bridge;
        IHostAPI* host;
        QuickJSEngine* engine;
        std::vector<JSValue> persistent_refs;

        void Track( JSContext* ctx, JSValue v ) {
            persistent_refs.push_back( JS_DupValue( ctx, v ) );
        }

        void ReleaseAll( JSContext* ctx ) {
            for ( auto& v : persistent_refs ) JS_FreeValue( ctx, v );
            persistent_refs.clear( );
        }
    };

    static QjsCtxData* D( JSContext* ctx ) { return static_cast<QjsCtxData*>( JS_GetContextOpaque( ctx ) ); }
    static HostBridge* B( JSContext* ctx ) { return D( ctx )->bridge; }
    static IHostAPI* H( JSContext* ctx ) { return D( ctx )->host; }

    // ── 工具 ──

    static std::string JsToStr( JSContext* ctx, JSValueConst v ) {
        const char* s = JS_ToCString( ctx, v );
        if ( !s ) return "";
        std::string result( s );
        JS_FreeCString( ctx, s );
        return result;
    }

    static JSValue StrToJs( JSContext* ctx, const std::string& s ) {
        return JS_NewString( ctx, s.c_str( ) );
    }

    static JSValue AllocStr( JSContext* ctx, const char* s ) {
        if ( s ) {
            JSValue v = JS_NewString( ctx, s );
            B( ctx )->free_string( s );
            return v;
        }
        return JS_NULL;
    }

    // ── alice.log ──

    static JSValue js_log_info( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) { B( ctx )->log_info( B( ctx )->ctx, JsToStr( ctx, argv[0] ).c_str( ) ); return JS_UNDEFINED; }
    static JSValue js_log_warn( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) { B( ctx )->log_warn( B( ctx )->ctx, JsToStr( ctx, argv[0] ).c_str( ) ); return JS_UNDEFINED; }
    static JSValue js_log_error( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) { B( ctx )->log_error( B( ctx )->ctx, JsToStr( ctx, argv[0] ).c_str( ) ); return JS_UNDEFINED; }
    static JSValue js_log_debug( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) { B( ctx )->log_debug( B( ctx )->ctx, JsToStr( ctx, argv[0] ).c_str( ) ); return JS_UNDEFINED; }

    // ── alice.fs ──

    static JSValue js_fs_read( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        auto* s = B( ctx )->storage_read( B( ctx )->ctx, JsToStr( ctx, argv[0] ).c_str( ) );
        return s ? AllocStr( ctx, s ) : JS_NULL;
    }
    static JSValue js_fs_write( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        int r = B( ctx )->storage_write( B( ctx )->ctx, JsToStr( ctx, argv[0] ).c_str( ), JsToStr( ctx, argv[1] ).c_str( ) );
        return JS_NewBool( ctx, r == 0 );
    }
    static JSValue js_fs_exists( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        return JS_NewBool( ctx, B( ctx )->storage_exists( B( ctx )->ctx, JsToStr( ctx, argv[0] ).c_str( ) ) != 0 );
    }

    // ── alice.kv ──

    static JSValue js_kv_get( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        auto* s = B( ctx )->kv_get( B( ctx )->ctx, JsToStr( ctx, argv[0] ).c_str( ) );
        return s ? AllocStr( ctx, s ) : JS_NULL;
    }
    static JSValue js_kv_set( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        B( ctx )->kv_set( B( ctx )->ctx, JsToStr( ctx, argv[0] ).c_str( ), JsToStr( ctx, argv[1] ).c_str( ) );
        return JS_UNDEFINED;
    }

    // ── alice.net ──

    static JSValue js_net_fetch( JSContext* ctx, JSValueConst, int argc, JSValueConst* argv ) {
        auto url = JsToStr( ctx, argv[0] );
        auto opts = argc > 1 ? JsToStr( ctx, argv[1] ) : std::string( "{}" );
        auto* s = B( ctx )->net_fetch( B( ctx )->ctx, url.c_str( ), opts.c_str( ) );
        return s ? AllocStr( ctx, s ) : JS_NULL;
    }

    static JSValue js_net_fetch_stream( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        auto url = JsToStr( ctx, argv[0] );
        auto opts = JsToStr( ctx, argv[1] );
        JSValue callback = argv[2];

        struct CbCtx { JSContext* ctx; JSValue fn; };
        CbCtx cb{ ctx, callback };

        int status = B( ctx )->net_fetch_stream( B( ctx )->ctx, url.c_str( ), opts.c_str( ),
            &cb, []( void* cb_ctx, const char* event_type, const char* event_data ) -> int {
                auto* c = static_cast<CbCtx*>( cb_ctx );
                JSValue args[2] = { JS_NewString( c->ctx, event_type ), JS_NewString( c->ctx, event_data ) };
                JSValue ret = JS_Call( c->ctx, c->fn, JS_UNDEFINED, 2, args );
                JS_FreeValue( c->ctx, args[0] );
                JS_FreeValue( c->ctx, args[1] );
                bool cont = true;
                if ( JS_IsBool( ret ) && !JS_ToBool( c->ctx, ret ) ) cont = false;
                if ( JS_IsException( ret ) ) cont = false;
                JS_FreeValue( c->ctx, ret );
                return cont ? 0 : -1;
            } );

        return JS_NewInt32( ctx, status );
    }

    static JSValue js_net_addRoute( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        auto method = JsToStr( ctx, argv[0] );
        auto path_str = JsToStr( ctx, argv[1] );
        JSValue callback = JS_DupValue( ctx, argv[2] );
        D( ctx )->persistent_refs.push_back( callback );
        JSContext* captured_ctx = ctx;

        H( ctx )->Net( )->AddRoute( method, path_str,
            [captured_ctx, callback]( const nlohmann::json& req ) -> nlohmann::json {
                JSValue arg = JS_NewString( captured_ctx, req.dump( ).c_str( ) );
                JSValue ret = JS_Call( captured_ctx, callback, JS_UNDEFINED, 1, &arg );
                JS_FreeValue( captured_ctx, arg );
                std::string result_str;
                if ( !JS_IsException( ret ) ) result_str = JsToStr( captured_ctx, ret );
                JS_FreeValue( captured_ctx, ret );
                try { return nlohmann::json::parse( result_str ); }
                catch ( ... ) { return nlohmann::json{ { "body", result_str } }; }
            } );
        return JS_UNDEFINED;
    }

    // ── alice.ws ──

    static JSValue js_ws_handle( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        auto type = JsToStr( ctx, argv[0] );
        JSValue callback = JS_DupValue( ctx, argv[1] );
        D( ctx )->persistent_refs.push_back( callback );
        JSContext* captured_ctx = ctx;

        H( ctx )->Net( )->AddWsHandler( type,
            [captured_ctx, callback]( const nlohmann::json& req ) -> nlohmann::json {
                JSValue arg = JS_NewString( captured_ctx, req.dump( ).c_str( ) );
                JSValue ret = JS_Call( captured_ctx, callback, JS_UNDEFINED, 1, &arg );
                JS_FreeValue( captured_ctx, arg );
                std::string result_str;
                if ( !JS_IsException( ret ) ) result_str = JsToStr( captured_ctx, ret );
                JS_FreeValue( captured_ctx, ret );
                try { return nlohmann::json::parse( result_str ); }
                catch ( ... ) { return nlohmann::json{ { "result", result_str } }; }
            } );
        return JS_UNDEFINED;
    }

    static JSValue js_ws_broadcast( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        auto session_id = JsToStr( ctx, argv[0] );
        auto data_json = JsToStr( ctx, argv[1] );
        nlohmann::json j;
        try { j = nlohmann::json::parse( data_json ); } catch ( ... ) {}
        H( ctx )->Net( )->WsBroadcast( session_id, j );
        return JS_UNDEFINED;
    }

    // ── alice.event ──

    static JSValue js_event_emit( JSContext* ctx, JSValueConst, int argc, JSValueConst* argv ) {
        auto name = JsToStr( ctx, argv[0] );
        auto data_json = argc > 1 ? JsToStr( ctx, argv[1] ) : std::string( "{}" );
        B( ctx )->event_emit( B( ctx )->ctx, name.c_str( ), data_json.c_str( ), -1 );
        return JS_UNDEFINED;
    }

    static JSValue js_event_emit_async( JSContext* ctx, JSValueConst, int argc, JSValueConst* argv ) {
        auto name = JsToStr( ctx, argv[0] );
        auto data_json = argc > 1 ? JsToStr( ctx, argv[1] ) : std::string( "{}" );
        B( ctx )->event_emit_async( B( ctx )->ctx, name.c_str( ), data_json.c_str( ), -1 );
        return JS_UNDEFINED;
    }

    static JSValue js_event_on( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        auto name = JsToStr( ctx, argv[0] );
        JSValue callback = JS_DupValue( ctx, argv[1] );
        D( ctx )->persistent_refs.push_back( callback );
        JSContext* captured_ctx = ctx;

        auto handle = H( ctx )->Event( )->On( name,
            [captured_ctx, callback]( const nlohmann::json& data ) {
                JSValue arg = JS_NewString( captured_ctx, data.dump( ).c_str( ) );
                JSValue ret = JS_Call( captured_ctx, callback, JS_UNDEFINED, 1, &arg );
                JS_FreeValue( captured_ctx, arg );
                JS_FreeValue( captured_ctx, ret );
            } );

        return JS_NewInt64( ctx, static_cast<int64_t>( handle ) );
    }

    static JSValue js_event_off( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        int64_t handle = 0;
        JS_ToInt64( ctx, &handle, argv[0] );
        H( ctx )->Event( )->Off( static_cast<uint64_t>( handle ) );
        return JS_UNDEFINED;
    }

    // ── alice.service ──

    static JSValue js_service_call( JSContext* ctx, JSValueConst, int argc, JSValueConst* argv ) {
        auto cap = JsToStr( ctx, argv[0] );
        auto method = JsToStr( ctx, argv[1] );
        auto args = argc > 2 ? JsToStr( ctx, argv[2] ) : std::string( "{}" );
        auto* s = B( ctx )->service_call( B( ctx )->ctx, cap.c_str( ), method.c_str( ), args.c_str( ) );
        return s ? AllocStr( ctx, s ) : StrToJs( ctx, "null" );
    }

    static JSValue js_service_register( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        auto capability = JsToStr( ctx, argv[0] );
        JSValue callback = JS_DupValue( ctx, argv[1] );
        D( ctx )->persistent_refs.push_back( callback );
        JSContext* captured_ctx = ctx;

        (void)H( ctx )->Service( )->Register( capability,
            [captured_ctx, callback]( const std::string& method, const nlohmann::json& args ) -> Result<nlohmann::json> {
                JSValue js_args[2] = { JS_NewString( captured_ctx, method.c_str( ) ), JS_NewString( captured_ctx, args.dump( ).c_str( ) ) };
                JSValue ret = JS_Call( captured_ctx, callback, JS_UNDEFINED, 2, js_args );
                JS_FreeValue( captured_ctx, js_args[0] );
                JS_FreeValue( captured_ctx, js_args[1] );
                if ( JS_IsException( ret ) ) {
                    JS_FreeValue( captured_ctx, ret );
                    return std::unexpected( MakeError( ErrorCode::ServiceCallFailed, "JS handler 异常" ) );
                }
                auto result_str = JsToStr( captured_ctx, ret );
                JS_FreeValue( captured_ctx, ret );
                try { return nlohmann::json::parse( result_str ); }
                catch ( ... ) { return nlohmann::json( result_str ); }
            } );
        return JS_UNDEFINED;
    }

    static JSValue js_service_list( JSContext* ctx, JSValueConst, int, JSValueConst* ) {
        auto list = H( ctx )->Service( )->List( );
        return StrToJs( ctx, nlohmann::json( list ).dump( ) );
    }

    static JSValue js_service_waitFor( JSContext* ctx, JSValueConst, int argc, JSValueConst* argv ) {
        auto cap = JsToStr( ctx, argv[0] );
        int32_t timeout_ms = 5000;
        if ( argc > 1 ) JS_ToInt32( ctx, &timeout_ms, argv[1] );
        bool ok = H( ctx )->Service( )->WaitFor( cap, std::chrono::milliseconds( timeout_ms ) );
        return JS_NewBool( ctx, ok );
    }

    // ── alice.pipeline ──

    static JSValue js_pipeline_register( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        auto pipe_name = JsToStr( ctx, argv[0] );
        auto stage_name = JsToStr( ctx, argv[1] );
        int32_t order = 0; JS_ToInt32( ctx, &order, argv[2] );
        JSValue callback = JS_DupValue( ctx, argv[3] );
        D( ctx )->persistent_refs.push_back( callback );
        JSContext* captured_ctx = ctx;

        H( ctx )->Pipeline( )->RegisterStage( pipe_name, stage_name, order,
            [captured_ctx, callback]( const nlohmann::json& input, nlohmann::json& shared ) -> nlohmann::json {
                JSValue js_args[2] = { JS_NewString( captured_ctx, input.dump( ).c_str( ) ), JS_NewString( captured_ctx, shared.dump( ).c_str( ) ) };
                JSValue ret = JS_Call( captured_ctx, callback, JS_UNDEFINED, 2, js_args );
                JS_FreeValue( captured_ctx, js_args[0] );
                JS_FreeValue( captured_ctx, js_args[1] );
                auto s = JsToStr( captured_ctx, ret );
                JS_FreeValue( captured_ctx, ret );
                try { return nlohmann::json::parse( s ); }
                catch ( ... ) { return nlohmann::json( s ); }
            } );
        return JS_UNDEFINED;
    }

    static JSValue js_pipeline_execute( JSContext* ctx, JSValueConst, int argc, JSValueConst* argv ) {
        auto pipe_name = JsToStr( ctx, argv[0] );
        auto input_json = argc > 1 ? JsToStr( ctx, argv[1] ) : std::string( "{}" );
        nlohmann::json input;
        try { input = nlohmann::json::parse( input_json ); } catch ( ... ) {}
        auto result = H( ctx )->Pipeline( )->Execute( pipe_name, input );
        if ( result ) return StrToJs( ctx, result->dump( ) );
        return StrToJs( ctx, nlohmann::json{ { "error", result.error( ).message } }.dump( ) );
    }

    // ── alice.timer ──

    static JSValue js_timer_set( JSContext* ctx, JSValueConst, int argc, JSValueConst* argv ) {
        auto dur = JsToStr( ctx, argv[0] );
        auto label = JsToStr( ctx, argv[1] );
        auto data = argc > 2 ? JsToStr( ctx, argv[2] ) : std::string( "{}" );
        auto* s = B( ctx )->timer_set( B( ctx )->ctx, dur.c_str( ), label.c_str( ), data.c_str( ) );
        return s ? AllocStr( ctx, s ) : StrToJs( ctx, "" );
    }
    static JSValue js_timer_remove( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        B( ctx )->timer_remove( B( ctx )->ctx, JsToStr( ctx, argv[0] ).c_str( ) ); return JS_UNDEFINED;
    }
    static JSValue js_timer_list( JSContext* ctx, JSValueConst, int, JSValueConst* ) {
        auto* s = B( ctx )->timer_list( B( ctx )->ctx );
        return s ? AllocStr( ctx, s ) : StrToJs( ctx, "[]" );
    }

    // ── alice.process ──

    static JSValue js_process_exec( JSContext* ctx, JSValueConst, int argc, JSValueConst* argv ) {
        auto cmd = JsToStr( ctx, argv[0] );
        nlohmann::json opts;
        if ( argc > 1 ) { try { opts = nlohmann::json::parse( JsToStr( ctx, argv[1] ) ); } catch ( ... ) {} }
        auto result = stdlib::ProcessExec( cmd, opts );
        if ( result ) return StrToJs( ctx, result->dump( ) );
        return StrToJs( ctx, nlohmann::json{ { "error", result.error( ).message } }.dump( ) );
    }

    // ── alice.path ──

    static JSValue js_path_join( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) { return StrToJs( ctx, stdlib::PathJoin( JsToStr( ctx, argv[0] ), JsToStr( ctx, argv[1] ) ) ); }
    static JSValue js_path_dirname( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) { return StrToJs( ctx, stdlib::PathDirname( JsToStr( ctx, argv[0] ) ) ); }
    static JSValue js_path_basename( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) { return StrToJs( ctx, stdlib::PathBasename( JsToStr( ctx, argv[0] ) ) ); }
    static JSValue js_path_ext( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) { return StrToJs( ctx, stdlib::PathExtension( JsToStr( ctx, argv[0] ) ) ); }
    static JSValue js_path_absolute( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) { return StrToJs( ctx, stdlib::PathAbsolute( JsToStr( ctx, argv[0] ) ) ); }

    // ── alice.regex ──

    static JSValue js_regex_test( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        return JS_NewBool( ctx, stdlib::RegexTest( JsToStr( ctx, argv[0] ), JsToStr( ctx, argv[1] ) ) );
    }
    static JSValue js_regex_match( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        auto result = stdlib::RegexMatch( JsToStr( ctx, argv[0] ), JsToStr( ctx, argv[1] ) );
        if ( result ) return StrToJs( ctx, result->dump( ) );
        return JS_NULL;
    }
    static JSValue js_regex_replace( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        return StrToJs( ctx, stdlib::RegexReplace( JsToStr( ctx, argv[0] ), JsToStr( ctx, argv[1] ), JsToStr( ctx, argv[2] ) ) );
    }

    // ── alice.encoding ──

    static JSValue js_base64encode( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) { return StrToJs( ctx, stdlib::Base64Encode( JsToStr( ctx, argv[0] ) ) ); }
    static JSValue js_base64decode( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) { return StrToJs( ctx, stdlib::Base64Decode( JsToStr( ctx, argv[0] ) ) ); }
    static JSValue js_hex( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) { return StrToJs( ctx, stdlib::HexEncode( JsToStr( ctx, argv[0] ) ) ); }

    // ── alice.time ──

    static JSValue js_time_now( JSContext* ctx, JSValueConst, int, JSValueConst* ) { return JS_NewInt64( ctx, stdlib::TimeNow( ) ); }
    static JSValue js_time_format( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        int64_t ts = 0; JS_ToInt64( ctx, &ts, argv[0] );
        return StrToJs( ctx, stdlib::TimeFormat( ts, JsToStr( ctx, argv[1] ) ) );
    }

    // ── alice.platform ──

    static JSValue js_platform_name( JSContext* ctx, JSValueConst, int, JSValueConst* ) { return JS_NewString( ctx, B( ctx )->platform_name( B( ctx )->ctx ) ); }
    static JSValue js_platform_dataDir( JSContext* ctx, JSValueConst, int, JSValueConst* ) {
        auto* s = B( ctx )->platform_data_dir( B( ctx )->ctx );
        return s ? AllocStr( ctx, s ) : JS_NULL;
    }
    static JSValue js_platform_exeDir( JSContext* ctx, JSValueConst, int, JSValueConst* ) {
        auto* s = B( ctx )->platform_exe_dir( B( ctx )->ctx );
        return s ? AllocStr( ctx, s ) : JS_NULL;
    }

    // ── alice.script ──

    static JSValue js_script_eval( JSContext* ctx, JSValueConst, int, JSValueConst* argv ) {
        auto lang = JsToStr( ctx, argv[0] );
        auto code = JsToStr( ctx, argv[1] );
        auto* s = B( ctx )->script_eval( B( ctx )->ctx, lang.c_str( ), code.c_str( ) );
        return s ? AllocStr( ctx, s ) : StrToJs( ctx, R"({"success":false,"error":"eval 失败"})" );
    }

    // ── console.log ──

    static JSValue js_console_log( JSContext* ctx, JSValueConst, int argc, JSValueConst* argv ) {
        std::string msg;
        for ( int i = 0; i < argc; ++i ) {
            if ( i > 0 ) msg += " ";
            msg += JsToStr( ctx, argv[i] );
        }
        B( ctx )->log_info( B( ctx )->ctx, msg.c_str( ) );
        return JS_UNDEFINED;
    }

    // ══════════════════════════════════════
    // InjectHostAPI
    // ══════════════════════════════════════

    void QuickJSEngine::InjectHostAPI( ) {
        auto set = [this]( JSValue parent, const char* name, JSCFunction* fn, int argc ) {
            JS_SetPropertyStr( ctx_, parent, name, JS_NewCFunction( ctx_, fn, name, argc ) );
        };

        JSValue global = JS_GetGlobalObject( ctx_ );
        JSValue alice = JS_NewObject( ctx_ );

        // alice.log
        JSValue log = JS_NewObject( ctx_ );
        set( log, "info", js_log_info, 1 );
        set( log, "warn", js_log_warn, 1 );
        set( log, "error", js_log_error, 1 );
        set( log, "debug", js_log_debug, 1 );
        JS_SetPropertyStr( ctx_, alice, "log", log );

        // alice.fs
        JSValue fs = JS_NewObject( ctx_ );
        set( fs, "read", js_fs_read, 1 );
        set( fs, "write", js_fs_write, 2 );
        set( fs, "exists", js_fs_exists, 1 );
        JS_SetPropertyStr( ctx_, alice, "fs", fs );

        // alice.kv
        JSValue kv = JS_NewObject( ctx_ );
        set( kv, "get", js_kv_get, 1 );
        set( kv, "set", js_kv_set, 2 );
        JS_SetPropertyStr( ctx_, alice, "kv", kv );

        // alice.net
        JSValue net = JS_NewObject( ctx_ );
        set( net, "fetch", js_net_fetch, 2 );
        set( net, "fetch_stream", js_net_fetch_stream, 3 );
        set( net, "addRoute", js_net_addRoute, 3 );
        JS_SetPropertyStr( ctx_, alice, "net", net );

        // alice.ws
        JSValue ws = JS_NewObject( ctx_ );
        set( ws, "handle", js_ws_handle, 2 );
        set( ws, "broadcast", js_ws_broadcast, 2 );
        JS_SetPropertyStr( ctx_, alice, "ws", ws );

        // alice.event
        JSValue event = JS_NewObject( ctx_ );
        set( event, "emit", js_event_emit, 2 );
        set( event, "emitAsync", js_event_emit_async, 2 );
        set( event, "on", js_event_on, 2 );
        set( event, "off", js_event_off, 1 );
        JS_SetPropertyStr( ctx_, alice, "event", event );

        // alice.service
        JSValue service = JS_NewObject( ctx_ );
        set( service, "call", js_service_call, 3 );
        set( service, "register", js_service_register, 2 );
        set( service, "list", js_service_list, 0 );
        set( service, "waitFor", js_service_waitFor, 2 );
        JS_SetPropertyStr( ctx_, alice, "service", service );

        // alice.pipeline
        JSValue pipeline = JS_NewObject( ctx_ );
        set( pipeline, "register", js_pipeline_register, 4 );
        set( pipeline, "execute", js_pipeline_execute, 2 );
        JS_SetPropertyStr( ctx_, alice, "pipeline", pipeline );

        // alice.timer
        JSValue timer = JS_NewObject( ctx_ );
        set( timer, "set", js_timer_set, 3 );
        set( timer, "remove", js_timer_remove, 1 );
        set( timer, "list", js_timer_list, 0 );
        JS_SetPropertyStr( ctx_, alice, "timer", timer );

        // alice.process
        JSValue process = JS_NewObject( ctx_ );
        set( process, "exec", js_process_exec, 2 );
        JS_SetPropertyStr( ctx_, alice, "process", process );

        // alice.path
        JSValue path = JS_NewObject( ctx_ );
        set( path, "join", js_path_join, 2 );
        set( path, "dirname", js_path_dirname, 1 );
        set( path, "basename", js_path_basename, 1 );
        set( path, "ext", js_path_ext, 1 );
        set( path, "absolute", js_path_absolute, 1 );
        JS_SetPropertyStr( ctx_, alice, "path", path );

        // alice.regex
        JSValue regex = JS_NewObject( ctx_ );
        set( regex, "test", js_regex_test, 2 );
        set( regex, "match", js_regex_match, 2 );
        set( regex, "replace", js_regex_replace, 3 );
        JS_SetPropertyStr( ctx_, alice, "regex", regex );

        // alice.encoding
        JSValue encoding = JS_NewObject( ctx_ );
        set( encoding, "base64encode", js_base64encode, 1 );
        set( encoding, "base64decode", js_base64decode, 1 );
        set( encoding, "hex", js_hex, 1 );
        JS_SetPropertyStr( ctx_, alice, "encoding", encoding );

        // alice.time
        JSValue time = JS_NewObject( ctx_ );
        set( time, "now", js_time_now, 0 );
        set( time, "format", js_time_format, 2 );
        JS_SetPropertyStr( ctx_, alice, "time", time );

        // alice.platform
        JSValue platform = JS_NewObject( ctx_ );
        set( platform, "name", js_platform_name, 0 );
        set( platform, "dataDir", js_platform_dataDir, 0 );
        set( platform, "exeDir", js_platform_exeDir, 0 );
        JS_SetPropertyStr( ctx_, alice, "platform", platform );

        // alice.script
        JSValue script = JS_NewObject( ctx_ );
        set( script, "eval", js_script_eval, 2 );
        JS_SetPropertyStr( ctx_, alice, "script", script );

        JS_SetPropertyStr( ctx_, global, "alice", alice );

        // console.log
        JSValue console = JS_NewObject( ctx_ );
        set( console, "log", js_console_log, 1 );
        JS_SetPropertyStr( ctx_, global, "console", console );

        JS_FreeValue( ctx_, global );
    }

    // ══════════════════════════════════════
    // 构造 / 析构
    // ══════════════════════════════════════

    QuickJSEngine::QuickJSEngine( const HostBridge& bridge, IHostAPI* host )
        : bridge_( bridge ), host_( host ) {

        rt_ = JS_NewRuntime( );
        ctx_ = JS_NewContext( rt_ );

        auto* data = new QjsCtxData{ &bridge_, host_, this };
        JS_SetContextOpaque( ctx_, data );

        InjectHostAPI( );
    }

    QuickJSEngine::~QuickJSEngine( ) {
        if ( ctx_ ) {
            auto* data = static_cast<QjsCtxData*>( JS_GetContextOpaque( ctx_ ) );
            if ( data ) {
                data->ReleaseAll( ctx_ );
                delete data;
            }
            JS_FreeContext( ctx_ );
        }
        if ( rt_ ) {
            JS_RunGC( rt_ );
            JS_FreeRuntime( rt_ );
        }
    }

    // ══════════════════════════════════════
    // Execute / CallFunction
    // ══════════════════════════════════════

    JsResult QuickJSEngine::Execute( const std::string& code ) {
        JsResult result;
        JSValue val = JS_Eval( ctx_, code.c_str( ), code.length( ), "<eval>", JS_EVAL_TYPE_GLOBAL );
        if ( JS_IsException( val ) ) {
            JSValue exc = JS_GetException( ctx_ );
            result.error = JsToStr( ctx_, exc );
            JS_FreeValue( ctx_, exc );
        } else {
            result.success = true;
            if ( !JS_IsUndefined( val ) ) result.return_value = JsToStr( ctx_, val );
        }
        JS_FreeValue( ctx_, val );
        return result;
    }

    JsResult QuickJSEngine::ExecuteFile( const std::filesystem::path& path ) {
        auto content = alice::FileStorage::ReadText( path );
        if ( !content ) return { .error = "文件读取失败: " + path.string( ) };

        JsResult result;
        JSValue val = JS_Eval( ctx_, content->c_str( ), content->length( ), path.string( ).c_str( ), JS_EVAL_TYPE_GLOBAL );
        if ( JS_IsException( val ) ) {
            JSValue exc = JS_GetException( ctx_ );
            result.error = JsToStr( ctx_, exc );
            JS_FreeValue( ctx_, exc );
        } else {
            result.success = true;
        }
        JS_FreeValue( ctx_, val );
        return result;
    }

    JsResult QuickJSEngine::CallFunction( const std::string& func_name, const std::string& args_json ) {
        JsResult result;
        JSValue global = JS_GetGlobalObject( ctx_ );
        JSValue func = JS_GetPropertyStr( ctx_, global, func_name.c_str( ) );

        if ( !JS_IsFunction( ctx_, func ) ) {
            result.error = "函数 '" + func_name + "' 不存在";
            JS_FreeValue( ctx_, func );
            JS_FreeValue( ctx_, global );
            return result;
        }

        JSValue ret;
        if ( args_json.empty( ) ) {
            ret = JS_Call( ctx_, func, JS_UNDEFINED, 0, nullptr );
        } else {
            JSValue arg = JS_NewString( ctx_, args_json.c_str( ) );
            ret = JS_Call( ctx_, func, JS_UNDEFINED, 1, &arg );
            JS_FreeValue( ctx_, arg );
        }

        if ( JS_IsException( ret ) ) {
            JSValue exc = JS_GetException( ctx_ );
            result.error = JsToStr( ctx_, exc );
            JS_FreeValue( ctx_, exc );
        } else {
            result.success = true;
            if ( !JS_IsUndefined( ret ) ) result.return_value = JsToStr( ctx_, ret );
        }

        JS_FreeValue( ctx_, ret );
        JS_FreeValue( ctx_, func );
        JS_FreeValue( ctx_, global );
        return result;
    }

    bool QuickJSEngine::HasFunction( const std::string& func_name ) const {
        JSValue global = JS_GetGlobalObject( ctx_ );
        JSValue func = JS_GetPropertyStr( ctx_, global, func_name.c_str( ) );
        bool is_fn = JS_IsFunction( ctx_, func );
        JS_FreeValue( ctx_, func );
        JS_FreeValue( ctx_, global );
        return is_fn;
    }

} // namespace alice
