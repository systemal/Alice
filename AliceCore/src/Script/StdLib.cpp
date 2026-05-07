#include "pch.h"
#include "Script/StdLib.h"
#include <bee/subprocess.h>
#include <bee/subprocess/common.h>
#include <bee/sys/file_handle.h>
#include <regex>
#include <ctime>
#include <cstdio>

namespace alice::stdlib {

    static std::string SanitizeUtf8( const std::string& input ) {
        std::string result;
        result.reserve( input.size( ) );
        for ( size_t i = 0; i < input.size( ); ) {
            unsigned char c = input[i];
            if ( c < 0x80 ) {
                result.push_back( c );
                ++i;
            } else if ( ( c & 0xE0 ) == 0xC0 && i + 1 < input.size( ) && ( input[i + 1] & 0xC0 ) == 0x80 ) {
                result.append( input, i, 2 ); i += 2;
            } else if ( ( c & 0xF0 ) == 0xE0 && i + 2 < input.size( ) && ( input[i + 1] & 0xC0 ) == 0x80 && ( input[i + 2] & 0xC0 ) == 0x80 ) {
                result.append( input, i, 3 ); i += 3;
            } else if ( ( c & 0xF8 ) == 0xF0 && i + 3 < input.size( ) && ( input[i + 1] & 0xC0 ) == 0x80 && ( input[i + 2] & 0xC0 ) == 0x80 && ( input[i + 3] & 0xC0 ) == 0x80 ) {
                result.append( input, i, 4 ); i += 4;
            } else {
                result.append( "\\x" );
                result.push_back( "0123456789abcdef"[c >> 4] );
                result.push_back( "0123456789abcdef"[c & 0xF] );
                ++i;
            }
        }
        return result;
    }

    // ── alice.process.exec ──

    Result<nlohmann::json> ProcessExec( const std::string& command, const nlohmann::json& opts ) {
        namespace sp = bee::subprocess;

        auto cwd = opts.is_object( ) && opts.contains( "cwd" ) ? opts["cwd"].get<std::string>( ) : ".";

        auto stdout_pipe = sp::pipe::open( );
        auto stderr_pipe = sp::pipe::open( );
        if ( !stdout_pipe || !stderr_pipe )
            return std::unexpected( MakeError( ErrorCode::ProcessFailed, "管道创建失败" ) );

        sp::spawn spawner;
        spawner.search_path( );
        spawner.set_console( sp::console::eDisable );
        spawner.redirect( sp::stdio::eOutput, std::move( stdout_pipe.wr ) );
        spawner.redirect( sp::stdio::eError, std::move( stderr_pipe.wr ) );

        sp::args_t args;
        args.push( "cmd" );
        args.push( "/c" );
        args.push( command );

        std::wstring wcwd( cwd.begin( ), cwd.end( ) );
        if ( !spawner.exec( args, wcwd ) )
            return std::unexpected( MakeError( ErrorCode::ProcessFailed, "进程启动失败" ) );

        sp::process proc( spawner );

        // 通过 to_file 转为 FILE*, 用 fread 读取
        auto read_pipe = []( bee::file_handle& h ) -> std::string {
            FILE* f = h.to_file( bee::file_handle::mode::read );
            if ( !f ) return "";
            std::string result;
            char buf[4096];
            while ( true ) {
                size_t n = fread( buf, 1, sizeof( buf ), f );
                if ( n == 0 ) break;
                result.append( buf, n );
            }
            // to_file 内部做了 _open_osfhandle + _fdopen, fclose 会关闭底层 handle
            fclose( f );
            return result;
        };

        std::string stdout_str = read_pipe( stdout_pipe.rd );
        std::string stderr_str = read_pipe( stderr_pipe.rd );

        auto exit_code = proc.wait( );

        return nlohmann::json{
            { "exit_code", exit_code.value_or( -1 ) },
            { "stdout", SanitizeUtf8( stdout_str ) },
            { "stderr", SanitizeUtf8( stderr_str ) },
        };
    }

    // ── alice.path.* ──

    std::string PathJoin( const std::string& a, const std::string& b ) {
        return ( std::filesystem::path( a ) / b ).string( );
    }
    std::string PathDirname( const std::string& path ) {
        return std::filesystem::path( path ).parent_path( ).string( );
    }
    std::string PathBasename( const std::string& path ) {
        return std::filesystem::path( path ).filename( ).string( );
    }
    std::string PathExtension( const std::string& path ) {
        return std::filesystem::path( path ).extension( ).string( );
    }
    std::string PathAbsolute( const std::string& path ) {
        return std::filesystem::absolute( path ).string( );
    }

    // ── alice.regex.* ──

    Result<nlohmann::json> RegexMatch( const std::string& text, const std::string& pattern ) {
        try {
            std::regex re( pattern );
            std::smatch matches;
            if ( std::regex_search( text, matches, re ) ) {
                nlohmann::json arr = nlohmann::json::array( );
                for ( auto& m : matches ) arr.push_back( m.str( ) );
                return arr;
            }
            return nlohmann::json( nullptr );
        }
        catch ( const std::regex_error& e ) {
            return std::unexpected( MakeError( ErrorCode::InvalidArgument, e.what( ) ) );
        }
    }

    std::string RegexReplace( const std::string& text, const std::string& pattern, const std::string& replacement ) {
        try { return std::regex_replace( text, std::regex( pattern ), replacement ); }
        catch ( ... ) { return text; }
    }

    bool RegexTest( const std::string& text, const std::string& pattern ) {
        try { return std::regex_search( text, std::regex( pattern ) ); }
        catch ( ... ) { return false; }
    }

    // ── alice.encoding.* ──

    static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string Base64Encode( const std::string& data ) {
        std::string result;
        int val = 0, valb = -6;
        for ( unsigned char c : data ) {
            val = ( val << 8 ) + c; valb += 8;
            while ( valb >= 0 ) { result.push_back( b64_table[( val >> valb ) & 0x3F] ); valb -= 6; }
        }
        if ( valb > -6 ) result.push_back( b64_table[( ( val << 8 ) >> ( valb + 8 ) ) & 0x3F] );
        while ( result.size( ) % 4 ) result.push_back( '=' );
        return result;
    }

    std::string Base64Decode( const std::string& data ) {
        std::vector<int> T( 256, -1 );
        for ( int i = 0; i < 64; i++ ) T[b64_table[i]] = i;
        std::string result;
        int val = 0, valb = -8;
        for ( unsigned char c : data ) {
            if ( T[c] == -1 ) break;
            val = ( val << 6 ) + T[c]; valb += 6;
            if ( valb >= 0 ) { result.push_back( char( ( val >> valb ) & 0xFF ) ); valb -= 8; }
        }
        return result;
    }

    std::string HexEncode( const std::string& data ) {
        std::string result;
        for ( unsigned char c : data ) result += std::format( "{:02x}", c );
        return result;
    }

    // ── alice.time.* ──

    int64_t TimeNow( ) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now( ).time_since_epoch( ) ).count( );
    }

    std::string TimeFormat( int64_t timestamp_ms, const std::string& fmt ) {
        auto t = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::time_point( std::chrono::milliseconds( timestamp_ms ) ) );
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s( &tm_buf, &t );
#else
        localtime_r( &t, &tm_buf );
#endif
        char buf[256];
        std::strftime( buf, sizeof( buf ), fmt.c_str( ), &tm_buf );
        return buf;
    }

} // namespace alice::stdlib
