#include "pch.h"
#include "Net/SseParser.h"

namespace alice {

    bool SseParser::Feed( std::string_view chunk, const EventCallback& on_event ) {
        buffer_.append( chunk );
        size_t pos = 0;

        while ( pos < buffer_.size( ) ) {
            auto nl = buffer_.find( '\n', pos );
            if ( nl == std::string::npos ) break;

            auto line = std::string_view( buffer_ ).substr( pos, nl - pos );
            pos = nl + 1;

            if ( !line.empty( ) && line.back( ) == '\r' ) line.remove_suffix( 1 );

            if ( line.empty( ) ) {
                if ( !current_data_.empty( ) ) {
                    SseEvent evt{ current_type_, current_data_ };
                    current_type_.clear( );
                    current_data_.clear( );
                    if ( !on_event( evt ) ) { buffer_.erase( 0, pos ); return false; }
                }
                continue;
            }

            if ( line.starts_with( "data: " ) ) {
                if ( !current_data_.empty( ) ) current_data_ += "\n";
                current_data_.append( line.substr( 6 ) );
            }
            else if ( line.starts_with( "data:" ) ) {
                if ( !current_data_.empty( ) ) current_data_ += "\n";
                current_data_.append( line.substr( 5 ) );
            }
            else if ( line.starts_with( "event: " ) ) { current_type_ = std::string( line.substr( 7 ) ); }
            else if ( line.starts_with( "event:" ) ) { current_type_ = std::string( line.substr( 6 ) ); }
        }

        if ( pos > 0 ) buffer_.erase( 0, pos );
        return true;
    }

    void SseParser::Reset( ) { buffer_.clear( ); current_type_.clear( ); current_data_.clear( ); }

} // namespace alice
