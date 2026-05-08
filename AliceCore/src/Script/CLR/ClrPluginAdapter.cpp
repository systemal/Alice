#include "pch.h"
#include "Script/CLR/ClrPluginAdapter.h"
#include "Script/CLR/ClrHost.h"
#include "Host/IHostAPI.h"

namespace alice {

    ClrPluginAdapter::ClrPluginAdapter( const PluginManifest& manifest )
        : manifest_( manifest ) {
    }

    void ClrPluginAdapter::OnLoad( IHostAPI* host ) {
        bridge_ = CreateHostBridge( host );

        auto dll_path = manifest_.path / manifest_.entry_file;

        auto entry_type = manifest_.clr_entry_type;
        if ( entry_type.empty( ) ) {
            host->Log( )->Error( "C# 插件缺少 entry_type: " + manifest_.id );
            return;
        }

        host->Log( )->Info( "加载 C# 插件: " + manifest_.id );

        auto result = ClrHost::LoadPlugin( manifest_.id, dll_path, entry_type, &bridge_ );
        if ( !result ) {
            host->Log( )->Error( "C# 插件加载失败: " + result.error( ).message );
        }
    }

    void ClrPluginAdapter::OnUnload( ) {
        auto result = ClrHost::UnloadPlugin( manifest_.id );
        if ( !result ) {
            ALICE_WARN( "C# 插件卸载失败 ({}): {}", manifest_.id, result.error( ).message );
        }
    }

} // namespace alice
