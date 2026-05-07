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

        // alice.json 里的 entry_type, 如 "HelloCSharp.PluginEntry, HelloCSharp"
        auto entry_type = manifest_.clr_entry_type;
        if ( entry_type.empty( ) ) {
            host->Log( )->Error( "C# 插件缺少 entry_type: " + manifest_.id );
            return;
        }

        std::wstring w_type( entry_type.begin( ), entry_type.end( ) );

        // 获取 Initialize 方法
        auto init_result = ClrHost::LoadAssemblyAndGetEntryPoint( dll_path, w_type, L"Initialize" );
        if ( !init_result ) {
            host->Log( )->Error( "C# Initialize 加载失败: " + init_result.error( ).message );
            return;
        }
        init_fn_ = reinterpret_cast<InitializeFn>( *init_result );

        // 获取 Shutdown 方法 (可选)
        auto shutdown_result = ClrHost::LoadAssemblyAndGetEntryPoint( dll_path, w_type, L"Shutdown" );
        if ( shutdown_result ) {
            shutdown_fn_ = reinterpret_cast<ShutdownFn>( *shutdown_result );
        }

        // 调用 C# Initialize, 传递 HostBridge 指针
        host->Log( )->Info( "加载 C# 插件: " + manifest_.id );
        init_fn_( &bridge_ );
    }

    void ClrPluginAdapter::OnUnload( ) {
        if ( shutdown_fn_ ) shutdown_fn_( );
    }

} // namespace alice
