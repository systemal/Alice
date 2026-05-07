#include "pch.h"
#include "Storage/UserDirectory.h"
#include "Storage/FileStorage.h"

namespace alice {

    UserDirectory::UserDirectory( const std::filesystem::path& base_data_dir, const UserID& user_id )
        : root_( base_data_dir / "users" / user_id ) {
    }

    VoidResult UserDirectory::EnsureStructure( ) {
        const std::filesystem::path dirs[] = {
            root_,
            Chars( ),
            Settings( ),
            Shells( ),
            Worlds( ),
            Personas( ),
            Plugins( ),
            Chats( ),
        };

        for ( const auto& dir : dirs ) {
            auto result = FileStorage::EnsureDirectory( dir );
            if ( !result ) return result;
        }

        return VoidResult{};
    }

} // namespace alice
