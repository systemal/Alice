#include "pch.h"
#include "Script/CLR/ClrHost.h"
#include "Storage/FileStorage.h"

#ifdef _WIN32

#include <Windows.h>

#define NETHOST_USE_AS_STATIC
#include <dotnet/nethost.h>
#include <dotnet/hostfxr.h>
#include <dotnet/coreclr_delegates.h>

namespace alice {

    void* ClrHost::hostfxr_lib_ = nullptr;
    void* ClrHost::runtime_context_ = nullptr;
    void* ClrHost::load_assembly_fn_ = nullptr;
    ClrHost::LoadPluginFn ClrHost::load_plugin_fn_ = nullptr;
    ClrHost::UnloadPluginFn ClrHost::unload_plugin_fn_ = nullptr;

    static hostfxr_initialize_for_runtime_config_fn init_for_config_fn = nullptr;
    static hostfxr_get_runtime_delegate_fn get_delegate_fn = nullptr;
    static hostfxr_close_fn close_fn = nullptr;
    static hostfxr_set_error_writer_fn set_error_writer_fn = nullptr;
    static hostfxr_get_runtime_property_value_fn get_prop_fn = nullptr;
    static hostfxr_set_runtime_property_value_fn set_prop_fn = nullptr;
    static load_assembly_and_get_function_pointer_fn cached_load_fn = nullptr;

    bool ClrHost::IsInitialized( ) { return cached_load_fn != nullptr; }

    VoidResult ClrHost::LoadHostfxr( ) {
        wchar_t buffer[MAX_PATH];
        size_t buffer_size = MAX_PATH;

        int rc = get_hostfxr_path( buffer, &buffer_size, nullptr );
        if ( rc != 0 )
            return std::unexpected( MakeError( ErrorCode::PluginLoadFailed,
                std::format( "get_hostfxr_path 失败: 0x{:x}", static_cast<uint32_t>( rc ) ) ) );

        HMODULE lib = LoadLibraryW( buffer );
        if ( !lib )
            return std::unexpected( MakeError( ErrorCode::PluginLoadFailed, "无法加载 hostfxr.dll" ) );

        hostfxr_lib_ = lib;

        init_for_config_fn = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
            GetProcAddress( lib, "hostfxr_initialize_for_runtime_config" ) );
        get_delegate_fn = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
            GetProcAddress( lib, "hostfxr_get_runtime_delegate" ) );
        close_fn = reinterpret_cast<hostfxr_close_fn>(
            GetProcAddress( lib, "hostfxr_close" ) );
        set_error_writer_fn = reinterpret_cast<hostfxr_set_error_writer_fn>(
            GetProcAddress( lib, "hostfxr_set_error_writer" ) );
        get_prop_fn = reinterpret_cast<hostfxr_get_runtime_property_value_fn>(
            GetProcAddress( lib, "hostfxr_get_runtime_property_value" ) );
        set_prop_fn = reinterpret_cast<hostfxr_set_runtime_property_value_fn>(
            GetProcAddress( lib, "hostfxr_set_runtime_property_value" ) );

        if ( !init_for_config_fn || !get_delegate_fn || !close_fn )
            return std::unexpected( MakeError( ErrorCode::PluginLoadFailed, "hostfxr 函数指针获取失败" ) );

        ALICE_INFO( "CLR: hostfxr 已加载" );
        return VoidResult{};
    }

    VoidResult ClrHost::EnsureRuntimeConfig( const std::filesystem::path& exe_dir ) {
        auto config_path = exe_dir / "alice.runtimeconfig.json";
        auto sdk_dir = exe_dir / "sdk";

        (void)FileStorage::EnsureDirectory( sdk_dir );

        auto sdk_abs = std::filesystem::absolute( sdk_dir ).string( );
        std::ranges::replace( sdk_abs, '\\', '/' );

        nlohmann::json config = {
            { "runtimeOptions", {
                { "tfm", "net10.0" },
                { "framework", {
                    { "name", "Microsoft.NETCore.App" },
                    { "version", "10.0.0" },
                }},
                { "additionalProbingPaths", { sdk_abs } },
            }},
        };

        return FileStorage::WriteJson( config_path, config, false );
    }

    VoidResult ClrHost::Initialize( const std::filesystem::path& exe_dir ) {
        if ( IsInitialized( ) ) return VoidResult{};

        auto result = LoadHostfxr( );
        if ( !result ) return result;

        if ( set_error_writer_fn ) {
            set_error_writer_fn( []( const wchar_t* msg ) {
                int len = WideCharToMultiByte( CP_UTF8, 0, msg, -1, nullptr, 0, nullptr, nullptr );
                std::string s( len - 1, '\0' );
                WideCharToMultiByte( CP_UTF8, 0, msg, -1, s.data( ), len, nullptr, nullptr );
                ALICE_ERROR( "CLR: {}", s );
            } );
        }

        // 生成 master runtimeconfig.json
        auto cfg_result = EnsureRuntimeConfig( exe_dir );
        if ( !cfg_result ) return cfg_result;

        auto config_path = exe_dir / "alice.runtimeconfig.json";

        // 用 master config 初始化 .NET Runtime (只初始化一次)
        hostfxr_handle ctx = nullptr;
        int32_t rc = init_for_config_fn( config_path.wstring( ).c_str( ), nullptr, &ctx );
        if ( rc < 0 ) {
            if ( ctx ) close_fn( ctx );
            return std::unexpected( MakeError( ErrorCode::PluginLoadFailed,
                std::format( "hostfxr_initialize 失败: 0x{:x}", static_cast<uint32_t>( rc ) ) ) );
        }
        runtime_context_ = ctx;

        // 把 sdk/ 目录下的 DLL 加入 TRUSTED_PLATFORM_ASSEMBLIES
        if ( get_prop_fn && set_prop_fn ) {
            const wchar_t* current_tpa = nullptr;
            if ( get_prop_fn( ctx, L"TRUSTED_PLATFORM_ASSEMBLIES", &current_tpa ) == 0 && current_tpa ) {
                auto sdk_dir = exe_dir / "sdk";
                std::wstring new_tpa( current_tpa );
                if ( std::filesystem::exists( sdk_dir ) ) {
                    for ( auto& entry : std::filesystem::directory_iterator( sdk_dir ) ) {
                        if ( entry.path( ).extension( ) == ".dll" ) {
                            new_tpa += L";";
                            new_tpa += std::filesystem::absolute( entry.path( ) ).wstring( );
                        }
                    }
                }
                set_prop_fn( ctx, L"TRUSTED_PLATFORM_ASSEMBLIES", new_tpa.c_str( ) );
                ALICE_INFO( "CLR: SDK 程序集已加入 TPA" );
            }
        }

        // 获取 load_assembly_and_get_function_pointer (全局共享)
        rc = get_delegate_fn( ctx, hdt_load_assembly_and_get_function_pointer,
                              reinterpret_cast<void**>( &cached_load_fn ) );
        if ( rc != 0 || !cached_load_fn ) {
            return std::unexpected( MakeError( ErrorCode::PluginLoadFailed,
                std::format( "get_runtime_delegate 失败: 0x{:x}", static_cast<uint32_t>( rc ) ) ) );
        }

        // 获取 PluginLoader 的 LoadPlugin/UnloadPlugin 入口
        auto sdk_dll_path = std::filesystem::absolute( exe_dir / "sdk" / "Alice.SDK.dll" ).wstring( );

        int32_t lp_rc = cached_load_fn( sdk_dll_path.c_str( ),
                                         L"Alice.SDK.PluginLoader, Alice.SDK",
                                         L"LoadPlugin",
                                         UNMANAGEDCALLERSONLY_METHOD,
                                         nullptr,
                                         reinterpret_cast<void**>( &load_plugin_fn_ ) );
        if ( lp_rc != 0 || !load_plugin_fn_ ) {
            ALICE_WARN( "CLR: PluginLoader.LoadPlugin 获取失败 (0x{:x}), C# 热重载不可用", static_cast<uint32_t>( lp_rc ) );
        }

        int32_t up_rc = cached_load_fn( sdk_dll_path.c_str( ),
                                         L"Alice.SDK.PluginLoader, Alice.SDK",
                                         L"UnloadPlugin",
                                         UNMANAGEDCALLERSONLY_METHOD,
                                         nullptr,
                                         reinterpret_cast<void**>( &unload_plugin_fn_ ) );
        if ( up_rc != 0 || !unload_plugin_fn_ ) {
            ALICE_WARN( "CLR: PluginLoader.UnloadPlugin 获取失败 (0x{:x}), C# 热重载不可用", static_cast<uint32_t>( up_rc ) );
        }

        if ( load_plugin_fn_ && unload_plugin_fn_ ) {
            ALICE_INFO( "CLR: PluginLoader 已就绪 (热重载可用)" );
        }

        ALICE_INFO( "CLR: .NET Runtime 已初始化 (sdk: {})", ( exe_dir / "sdk" ).string( ) );
        return VoidResult{};
    }

    Result<void*> ClrHost::LoadAssemblyAndGetEntryPoint(
        const std::filesystem::path& assembly_path,
        const std::wstring& type_name,
        const std::wstring& method_name ) {

        if ( !cached_load_fn )
            return std::unexpected( MakeError( ErrorCode::PluginLoadFailed, "CLR 未初始化" ) );

        void* entry_point = nullptr;
        auto abs_path = std::filesystem::absolute( assembly_path ).wstring( );

        int32_t rc = cached_load_fn( abs_path.c_str( ),
                                      type_name.c_str( ),
                                      method_name.c_str( ),
                                      UNMANAGEDCALLERSONLY_METHOD,
                                      nullptr,
                                      &entry_point );

        if ( rc != 0 || !entry_point )
            return std::unexpected( MakeError( ErrorCode::PluginLoadFailed,
                std::format( "加载 C# 方法失败: 0x{:x}", static_cast<uint32_t>( rc ) ) ) );

        return entry_point;
    }

    VoidResult ClrHost::LoadPlugin( const std::string& plugin_id,
                                      const std::filesystem::path& dll_path,
                                      const std::string& entry_type,
                                      void* bridge_ptr ) {
        if ( !load_plugin_fn_ )
            return std::unexpected( MakeError( ErrorCode::PluginLoadFailed, "PluginLoader 未就绪" ) );

        auto abs_dll = std::filesystem::absolute( dll_path ).string( );

        int rc = load_plugin_fn_( plugin_id.c_str( ), abs_dll.c_str( ), entry_type.c_str( ), bridge_ptr );
        if ( rc != 0 )
            return std::unexpected( MakeError( ErrorCode::PluginLoadFailed,
                std::format( "PluginLoader.LoadPlugin 失败: rc={}", rc ) ) );

        return VoidResult{};
    }

    VoidResult ClrHost::UnloadPlugin( const std::string& plugin_id ) {
        if ( !unload_plugin_fn_ )
            return std::unexpected( MakeError( ErrorCode::PluginLoadFailed, "PluginLoader 未就绪" ) );

        int rc = unload_plugin_fn_( plugin_id.c_str( ) );
        if ( rc != 0 )
            return std::unexpected( MakeError( ErrorCode::PluginLoadFailed,
                std::format( "PluginLoader.UnloadPlugin 失败: rc={}", rc ) ) );

        return VoidResult{};
    }

    void ClrHost::Shutdown( ) {
        if ( runtime_context_ ) {
            close_fn( static_cast<hostfxr_handle>( runtime_context_ ) );
            runtime_context_ = nullptr;
        }
        if ( hostfxr_lib_ ) {
            FreeLibrary( static_cast<HMODULE>( hostfxr_lib_ ) );
            hostfxr_lib_ = nullptr;
        }
        cached_load_fn = nullptr;
        load_plugin_fn_ = nullptr;
        unload_plugin_fn_ = nullptr;
        ALICE_INFO( "CLR: 已关闭" );
    }

} // namespace alice

#else

namespace alice {
    void* ClrHost::hostfxr_lib_ = nullptr;
    void* ClrHost::runtime_context_ = nullptr;
    void* ClrHost::load_assembly_fn_ = nullptr;
    ClrHost::LoadPluginFn ClrHost::load_plugin_fn_ = nullptr;
    ClrHost::UnloadPluginFn ClrHost::unload_plugin_fn_ = nullptr;

    bool ClrHost::IsInitialized( ) { return false; }
    VoidResult ClrHost::Initialize( const std::filesystem::path& ) { return std::unexpected( MakeError( ErrorCode::PluginLoadFailed, "CLR 不支持此平台" ) ); }
    void ClrHost::Shutdown( ) {}
    VoidResult ClrHost::LoadHostfxr( ) { return std::unexpected( MakeError( ErrorCode::PluginLoadFailed, "CLR 不支持此平台" ) ); }
    VoidResult ClrHost::EnsureRuntimeConfig( const std::filesystem::path& ) { return std::unexpected( MakeError( ErrorCode::PluginLoadFailed, "CLR 不支持此平台" ) ); }
    Result<void*> ClrHost::LoadAssemblyAndGetEntryPoint( const std::filesystem::path&, const std::wstring&, const std::wstring& ) {
        return std::unexpected( MakeError( ErrorCode::PluginLoadFailed, "CLR 不支持此平台" ) );
    }
    VoidResult ClrHost::LoadPlugin( const std::string&, const std::filesystem::path&, const std::string&, void* ) {
        return std::unexpected( MakeError( ErrorCode::PluginLoadFailed, "CLR 不支持此平台" ) );
    }
    VoidResult ClrHost::UnloadPlugin( const std::string& ) {
        return std::unexpected( MakeError( ErrorCode::PluginLoadFailed, "CLR 不支持此平台" ) );
    }
}

#endif
