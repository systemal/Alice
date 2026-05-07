#include "pch.h"
#include "Net/HttpClient.h"

#ifdef _WIN32
#include <Windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace alice {

#ifdef _WIN32

    static bool ParseUrl( const std::string& url, std::wstring& host, std::wstring& path, bool& is_https, INTERNET_PORT& port ) {
        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof( uc );
        uc.dwHostNameLength = 1;
        uc.dwUrlPathLength = 1;
        uc.dwExtraInfoLength = 1;

        std::wstring wide_url( url.begin( ), url.end( ) );
        if ( !WinHttpCrackUrl( wide_url.c_str( ), 0, 0, &uc ) ) return false;

        host = std::wstring( uc.lpszHostName, uc.dwHostNameLength );
        path = std::wstring( uc.lpszUrlPath, uc.dwUrlPathLength );
        if ( uc.lpszExtraInfo && uc.dwExtraInfoLength > 0 )
            path += std::wstring( uc.lpszExtraInfo, uc.dwExtraInfoLength );
        is_https = ( uc.nScheme == INTERNET_SCHEME_HTTPS );
        port = uc.nPort;
        return true;
    }

    struct WinHttpHandle {
        HINTERNET h = nullptr;
        WinHttpHandle( ) = default;
        explicit WinHttpHandle( HINTERNET handle ) : h( handle ) {}
        ~WinHttpHandle( ) { if ( h ) WinHttpCloseHandle( h ); }
        WinHttpHandle( const WinHttpHandle& ) = delete;
        WinHttpHandle& operator=( const WinHttpHandle& ) = delete;
        WinHttpHandle( WinHttpHandle&& o ) noexcept : h( o.h ) { o.h = nullptr; }
        explicit operator bool( ) const { return h != nullptr; }
        operator HINTERNET( ) const { return h; }
    };

    Result<int> HttpClient::StreamRequest( const Request& request, StreamCallback on_data ) {
        std::wstring host, path;
        bool is_https = false;
        INTERNET_PORT port = 0;
        if ( !ParseUrl( request.url, host, path, is_https, port ) )
            return std::unexpected( MakeError( ErrorCode::NetRequestFailed, "URL 解析失败" ) );

        WinHttpHandle session( WinHttpOpen( L"Alice/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 ) );
        if ( !session )
            return std::unexpected( MakeError( ErrorCode::NetRequestFailed, "WinHttpOpen 失败" ) );

        WinHttpHandle connection( WinHttpConnect( session, host.c_str( ), port, 0 ) );
        if ( !connection )
            return std::unexpected( MakeError( ErrorCode::NetRequestFailed, "WinHttpConnect 失败" ) );

        std::wstring wmethod( request.method.begin( ), request.method.end( ) );
        DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
        WinHttpHandle hreq( WinHttpOpenRequest( connection, wmethod.c_str( ), path.c_str( ),
                                                 nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags ) );
        if ( !hreq )
            return std::unexpected( MakeError( ErrorCode::NetRequestFailed, "WinHttpOpenRequest 失败" ) );

        for ( auto& [key, value] : request.headers ) {
            std::wstring header( key.begin( ), key.end( ) );
            header += L": ";
            std::wstring wval( value.begin( ), value.end( ) );
            header += wval;
            WinHttpAddRequestHeaders( hreq, header.c_str( ), static_cast<DWORD>( header.size( ) ),
                                      WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE );
        }

        if ( !WinHttpSendRequest( hreq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   (LPVOID)request.body.data( ), static_cast<DWORD>( request.body.size( ) ),
                                   static_cast<DWORD>( request.body.size( ) ), 0 ) )
            return std::unexpected( MakeError( ErrorCode::NetRequestFailed, "WinHttpSendRequest 失败" ) );

        if ( !WinHttpReceiveResponse( hreq, nullptr ) )
            return std::unexpected( MakeError( ErrorCode::NetRequestFailed, "WinHttpReceiveResponse 失败" ) );

        DWORD status_code = 0;
        DWORD size = sizeof( status_code );
        WinHttpQueryHeaders( hreq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size, WINHTTP_NO_HEADER_INDEX );

        char buffer[4096];
        DWORD bytes_available = 0, bytes_read = 0;
        while ( WinHttpQueryDataAvailable( hreq, &bytes_available ) && bytes_available > 0 ) {
            DWORD to_read = ( std::min )( bytes_available, static_cast<DWORD>( sizeof( buffer ) ) );
            if ( WinHttpReadData( hreq, buffer, to_read, &bytes_read ) && bytes_read > 0 ) {
                if ( on_data && !on_data( std::string_view( buffer, bytes_read ) ) ) break;
            }
        }

        return static_cast<int>( status_code );
    }

#else

    Result<int> HttpClient::StreamRequest( const Request&, StreamCallback ) {
        return std::unexpected( MakeError( ErrorCode::NetRequestFailed, "HttpClient 尚未在此平台实现" ) );
    }

#endif

    Result<HttpClient::Response> HttpClient::Send( const Request& request ) {
        Response resp;
        auto result = StreamRequest( request, [&resp]( std::string_view chunk ) {
            resp.body.append( chunk );
            return true;
        } );
        if ( !result ) return std::unexpected( result.error( ) );
        resp.status_code = *result;
        return resp;
    }

    Result<nlohmann::json> HttpClient::Fetch( const std::string& url, const nlohmann::json& opts ) {
        Request req;
        req.url = url;
        req.method = ( opts.is_object( ) && opts.contains( "method" ) )
            ? opts["method"].get<std::string>( ) : "GET";

        if ( opts.is_object( ) && opts.contains( "body" ) ) {
            req.body = opts["body"].is_string( ) ? opts["body"].get<std::string>( ) : opts["body"].dump( );
        }
        if ( opts.is_object( ) && opts.contains( "headers" ) && opts["headers"].is_object( ) ) {
            for ( auto& [k, v] : opts["headers"].items( ) ) req.headers[k] = v.get<std::string>( );
        }
        if ( !req.headers.contains( "Content-Type" ) && !req.body.empty( ) )
            req.headers["Content-Type"] = "application/json";

        auto result = Send( req );
        if ( !result ) return std::unexpected( result.error( ) );

        return nlohmann::json{ { "status", result->status_code }, { "body", result->body } };
    }

} // namespace alice
