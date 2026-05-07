#include "pch.h"
#include "Host/HostAPIImpl.h"
#include "Runtime/Runtime.h"
#include "Event/EventBus.h"
#include "Service/ServiceRegistry.h"
#include "Pipeline/Pipeline.h"
#include "Net/RouteRegistry.h"
#include "Net/HttpClient.h"
#include "Net/SseParser.h"
#include "Net/WsRouter.h"
#include "Net/WsRegistry.h"
#include "Storage/FileStorage.h"
#include "Platform/Platform.h"

namespace alice {

    HostAPIImpl::HostAPIImpl( const PluginID& plugin_id, Runtime& runtime )
        : plugin_id_( plugin_id ), runtime_( runtime ) {}

    std::string HostAPIImpl::GetPluginDataDir( ) const {
        return ( runtime_.DataDir( ) / "plugins" / plugin_id_ ).string( );
    }

    // ── Event ──

    EventHandle HostAPIImpl::On( const std::string& event_name, std::function<void( const nlohmann::json& )> callback ) {
        return runtime_.GetEventBus( ).Subscribe( event_name,
            [cb = std::move( callback )]( const EventData& evt ) { cb( evt.data ); },
            plugin_id_ );
    }

    EventHandle HostAPIImpl::OnRaw( const std::string& event_name, std::function<void( const EventData& )> callback ) {
        return runtime_.GetEventBus( ).Subscribe( event_name, std::move( callback ), plugin_id_ );
    }

    void HostAPIImpl::Emit( const std::string& event_name, const nlohmann::json& data, int ttl ) {
        runtime_.GetEventBus( ).Emit( event_name, data, plugin_id_, "", ttl );
    }

    void HostAPIImpl::EmitAsync( const std::string& event_name, const nlohmann::json& data, int ttl ) {
        runtime_.GetEventBus( ).EmitAsync( event_name, data, plugin_id_, "", ttl );
    }

    void HostAPIImpl::Off( EventHandle handle ) {
        runtime_.GetEventBus( ).Unsubscribe( handle );
    }

    // ── Net ──

    Result<nlohmann::json> HostAPIImpl::Fetch( const std::string& url, const nlohmann::json& opts ) {
        return HttpClient::Fetch( url, opts );
    }

    Result<int> HostAPIImpl::FetchStream( const std::string& url, const nlohmann::json& opts, SseEventCallback on_event ) {
        HttpClient::Request req;
        req.url = url;
        req.method = ( opts.is_object( ) && opts.contains( "method" ) )
            ? opts["method"].get<std::string>( ) : "GET";
        if ( opts.is_object( ) && opts.contains( "body" ) )
            req.body = opts["body"].is_string( ) ? opts["body"].get<std::string>( ) : opts["body"].dump( );
        if ( opts.is_object( ) && opts.contains( "headers" ) && opts["headers"].is_object( ) )
            for ( auto& [k, v] : opts["headers"].items( ) ) req.headers[k] = v.get<std::string>( );
        if ( !req.headers.contains( "Content-Type" ) && !req.body.empty( ) )
            req.headers["Content-Type"] = "application/json";

        SseParser parser;
        auto result = HttpClient::StreamRequest( req, [&parser, &on_event]( std::string_view chunk ) -> bool {
            return parser.Feed( chunk, [&on_event]( const SseEvent& evt ) -> bool {
                return on_event( evt.type, evt.data );
            } );
        } );
        if ( !result ) return std::unexpected( result.error( ) );
        return *result;
    }

    void HostAPIImpl::AddRoute( const std::string& method, const std::string& path, HttpRouteHandler handler ) {
        runtime_.GetRouteRegistry( ).Add( method, path,
            [h = std::move( handler )]( const HttpRequest& req ) -> HttpResponse {
                nlohmann::json req_json = {
                    { "method", req.method },
                    { "path", req.path },
                    { "body", req.body },
                };
                auto result = h( req_json );
                int status_code = 200;
                if ( result.is_object( ) && result.contains( "status" ) && result[ "status" ].is_number( ) )
                    status_code = result[ "status" ].get<int>( );

                std::string body_str;
                if ( result.is_object( ) && result.contains( "body" ) )
                    body_str = result[ "body" ].is_string( ) ? result[ "body" ].get<std::string>( ) : result[ "body" ].dump( );
                else
                    body_str = result.dump( );

                return { .status = status_code, .body = body_str };
            }, plugin_id_ );
    }

    void HostAPIImpl::AddWsHandler( const std::string& type, WsMessageHandler handler ) {
        runtime_.GetWsRouter( ).Handle( type,
            [h = std::move( handler )]( const WsRequest& req ) -> nlohmann::json {
                nlohmann::json req_json = {
                    { "type", req.type },
                    { "request_id", req.request_id },
                    { "data", req.data },
                    { "session_id", req.session_id },
                };
                return h( req_json );
            }, plugin_id_ );
    }

    void HostAPIImpl::AddWsStreamHandler( const std::string& type, WsStreamMessageHandler handler ) {
        runtime_.GetWsRouter( ).HandleStream( type,
            [h = std::move( handler )]( const WsRequest& req, WsSender& sender ) {
                nlohmann::json req_json = {
                    { "type", req.type },
                    { "request_id", req.request_id },
                    { "data", req.data },
                    { "session_id", req.session_id },
                };
                h( req_json,
                   [&sender]( const nlohmann::json& data ) { sender.Send( data ); },
                   [&sender]( ) { sender.Done( ); } );
            }, plugin_id_ );
    }

    void HostAPIImpl::WsBroadcast( const std::string& session_id, const nlohmann::json& data ) {
        WsRegistry::Instance( ).Broadcast( session_id, data.dump( ) );
    }

    // ── Storage ──

    Result<std::string> HostAPIImpl::ReadFile( const std::string& path ) {
        return FileStorage::ReadText( path );
    }

    VoidResult HostAPIImpl::WriteFile( const std::string& path, const std::string& content ) {
        return FileStorage::WriteText( path, content );
    }

    bool HostAPIImpl::FileExists( const std::string& path ) {
        return FileStorage::Exists( path );
    }

    Result<nlohmann::json> HostAPIImpl::ListDir( const std::string& path ) {
        if ( !std::filesystem::exists( path ) )
            return std::unexpected( MakeError( ErrorCode::NotFound, "目录不存在" ) );

        nlohmann::json entries = nlohmann::json::array( );
        for ( auto& entry : std::filesystem::directory_iterator( path ) ) {
            entries.push_back( {
                { "name", entry.path( ).filename( ).string( ) },
                { "is_dir", entry.is_directory( ) },
            } );
        }
        return entries;
    }

    VoidResult HostAPIImpl::RemoveFile( const std::string& path ) {
        return FileStorage::Remove( path );
    }

    Result<nlohmann::json> HostAPIImpl::KvGet( const std::string& key ) {
        return runtime_.GetKvStore( ).Get( plugin_id_, key );
    }

    VoidResult HostAPIImpl::KvSet( const std::string& key, const nlohmann::json& value ) {
        return runtime_.GetKvStore( ).Set( plugin_id_, key, value );
    }

    // ── Service ──

    VoidResult HostAPIImpl::Register( const std::string& capability,
        std::function<Result<nlohmann::json>( const std::string&, const nlohmann::json& )> handler,
        const nlohmann::json& schema ) {
        return runtime_.GetServiceRegistry( ).Register( capability, plugin_id_, std::move( handler ), schema );
    }

    Result<nlohmann::json> HostAPIImpl::Call( const std::string& capability,
        const std::string& method, const nlohmann::json& args ) {
        return runtime_.GetServiceRegistry( ).Call( capability, method, args );
    }

    bool HostAPIImpl::WaitFor( const std::string& capability, std::chrono::milliseconds timeout ) {
        return runtime_.GetServiceRegistry( ).WaitFor( capability, timeout );
    }

    std::vector<nlohmann::json> HostAPIImpl::List( ) {
        auto infos = runtime_.GetServiceRegistry( ).List( );
        std::vector<nlohmann::json> result;
        for ( auto& info : infos ) {
            result.push_back( {
                { "capability", info.capability },
                { "provider", info.provider_plugin },
            } );
        }
        return result;
    }

    // ── Pipeline ──

    void HostAPIImpl::RegisterStage( const std::string& pipeline, const std::string& stage,
        int order, std::function<nlohmann::json( const nlohmann::json&, nlohmann::json& )> handler ) {
        runtime_.GetPipelineEngine( ).RegisterStage( pipeline, stage, order,
            [h = std::move( handler )]( const nlohmann::json& input, PipelineContext& ctx ) -> StageOutput {
                auto result = h( input, ctx.shared_data );
                auto action = StageResult::Continue;
                if ( result.is_object( ) && result.contains( "_action" ) ) {
                    auto a = result["_action"].get<std::string>( );
                    if ( a == "break" ) action = StageResult::Break;
                    else if ( a == "retry" ) action = StageResult::Retry;
                    else if ( a == "error" ) action = StageResult::Error;
                    result.erase( "_action" );
                }
                return { .result = action, .data = result };
            }, plugin_id_ );
    }

    Result<nlohmann::json> HostAPIImpl::Execute( const std::string& pipeline, const nlohmann::json& input ) {
        PipelineContext ctx;
        ctx.trace_id = GenerateTraceId( );
        return runtime_.GetPipelineEngine( ).Execute( pipeline, input, ctx );
    }

    // ── Schedule ──

    TimerID HostAPIImpl::SetTimer( const std::string& duration, const std::string& label, const nlohmann::json& data ) {
        auto seconds = ParseDuration( duration );
        return runtime_.GetTimerScheduler( ).Add( seconds, label, data, plugin_id_ );
    }

    void HostAPIImpl::RemoveTimer( const TimerID& id ) {
        runtime_.GetTimerScheduler( ).Remove( id );
    }

    std::vector<nlohmann::json> HostAPIImpl::ListTimers( ) {
        auto entries = runtime_.GetTimerScheduler( ).ListActive( );
        std::vector<nlohmann::json> result;
        for ( auto& e : entries ) {
            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                e.fire_time - std::chrono::steady_clock::now( ) ).count( );
            result.push_back( {
                { "id", e.id },
                { "label", e.label },
                { "data", e.data },
                { "owner", e.owner_plugin },
                { "remaining_seconds", remaining },
            } );
        }
        return result;
    }

    // ── Log ──

    void HostAPIImpl::Info( const std::string& msg )  { ALICE_INFO( "[{}] {}", plugin_id_, msg ); }
    void HostAPIImpl::Warn( const std::string& msg )  { ALICE_WARN( "[{}] {}", plugin_id_, msg ); }
    void HostAPIImpl::Error( const std::string& msg ) { ALICE_ERROR( "[{}] {}", plugin_id_, msg ); }
    void HostAPIImpl::Debug( const std::string& msg ) { ALICE_DEBUG( "[{}] {}", plugin_id_, msg ); }

    // ── Platform ──

    std::string HostAPIImpl::Name( ) { return platform::GetPlatformName( ); }
    std::string HostAPIImpl::DataDir( ) { return runtime_.DataDir( ).string( ); }
    std::string HostAPIImpl::ExeDir( ) { return runtime_.ExeDir( ).string( ); }

    // ── 工具 ──

    std::chrono::seconds HostAPIImpl::ParseDuration( std::string_view str ) {
        int total = 0, current = 0;
        for ( char c : str ) {
            if ( c >= '0' && c <= '9' ) { current = current * 10 + ( c - '0' ); }
            else if ( c == 's' || c == 'S' ) { total += current; current = 0; }
            else if ( c == 'm' || c == 'M' ) { total += current * 60; current = 0; }
            else if ( c == 'h' || c == 'H' ) { total += current * 3600; current = 0; }
            else if ( c == 'd' || c == 'D' ) { total += current * 86400; current = 0; }
        }
        return std::chrono::seconds( total + current );
    }

} // namespace alice
