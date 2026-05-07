#pragma once

#include "Plugin/IPlugin.h"
#include <unordered_map>
#include <memory>
#include <mutex>

namespace alice {

    /// <summary>
    /// 插件句柄 (RAII, 管理 DLL handle + 插件生命周期)
    /// </summary>
    struct PluginHandle {
        std::unique_ptr<IPlugin, void( * )( IPlugin* )> plugin;
        void* dll_handle = nullptr;
        PluginManifest manifest;

        PluginHandle( ) : plugin( nullptr, []( IPlugin* ) {} ) {}
        PluginHandle( IPlugin* p, void( *deleter )( IPlugin* ), void* dll = nullptr )
            : plugin( p, deleter ), dll_handle( dll ) {}

        ~PluginHandle( );
        PluginHandle( PluginHandle&& ) noexcept;
        PluginHandle& operator=( PluginHandle&& ) noexcept;
        PluginHandle( const PluginHandle& ) = delete;
        PluginHandle& operator=( const PluginHandle& ) = delete;

        IPlugin* get( ) const { return plugin.get( ); }
        explicit operator bool( ) const { return plugin != nullptr; }
    };

    /// <summary>
    /// 插件注册表 — 管理所有已加载插件的生命周期.
    /// Register 接收 PluginHandle (RAII, 包含 IPlugin 智能指针 + DLL handle).
    /// UnregisterAll 按注册顺序逆序卸载 (TODO: 实际上是 unordered_map, 顺序不保证).
    ///
    /// PluginHandle 析构时自动 FreeLibrary (DLL 插件) 或 delete (脚本适配器).
    /// 管理所有已加载插件的生命周期.
    /// </summary>
    class PluginRegistry {
    public:
        /// <summary>
        /// 注册插件 (加载后调用)
        /// </summary>
        void Register( PluginHandle handle );

        /// <summary>
        /// 卸载插件
        /// </summary>
        void Unregister( const PluginID& id );

        /// <summary>
        /// 卸载所有插件
        /// </summary>
        void UnregisterAll( );

        /// <summary>
        /// 获取插件
        /// </summary>
        IPlugin* Get( const PluginID& id ) const;

        /// <summary>
        /// 列出所有插件
        /// </summary>
        struct PluginInfo {
            PluginID id;
            std::string name;
            std::string type;
            std::string version;
        };
        std::vector<PluginInfo> List( ) const;

        /// <summary>
        /// 按类型列出插件
        /// </summary>
        std::vector<PluginInfo> ListByType( const std::string& type ) const;

        /// <summary>
        /// 插件数量
        /// </summary>
        size_t Count( ) const;

    private:
        mutable std::mutex mutex_;
        std::unordered_map<PluginID, PluginHandle> plugins_;
    };

} // namespace alice
