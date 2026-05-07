#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <concepts>

namespace alice {

    class IHostAPI;

    /// <summary>
    /// 插件清单 — 从 alice.json 解析.
    /// 每个 mods/ 子目录下的 alice.json 对应一个清单.
    ///
    /// TODO: permissions 字段已声明但未实际检查, 权限系统是远期目标.
    /// TODO: dependencies 字段已用于拓扑排序加载, 但缺少版本约束 (如 ">=1.0.0").
    /// </summary>
    struct PluginManifest {
        PluginID id;                            ///< 唯一标识 (如 "handler-code-exec")
        std::string name;                       ///< 显示名
        std::string version;                    ///< 语义版本号
        std::string author;
        std::string description;
        std::string type;                       ///< "provider" / "handler" / "shell" / "plugin"
        std::vector<std::string> permissions;   ///< 权限声明 (TODO: 未实现检查)
        std::vector<PluginID> dependencies;     ///< 依赖的其他插件 ID (用于拓扑排序)
        std::filesystem::path path;             ///< 插件目录绝对路径
        std::string entry_file;                 ///< 入口文件 (main.lua / main.mjs / MyPlugin.dll)
        std::string runtime;                    ///< "lua" / "js" / "dotnet" / "" (空=自动检测)
        std::string clr_entry_type;             ///< C# 入口类型 (如 "MyPlugin.PluginEntry, MyPlugin")
        bool is_native = false;                 ///< C++ DLL 插件 (TODO: 未实现)
    };

    /// <summary>
    /// 插件接口 — 所有插件 (C++/Lua/JS/C#) 的统一抽象.
    ///
    /// 生命周期:
    /// 1. Core 扫描 mods/ → 解析 alice.json → 创建适配器 (ScriptPluginAdapter/ClrPluginAdapter)
    /// 2. Core 调 OnLoad(host) → 插件拿到 IHostAPI, 注册服务/事件/路由等
    /// 3. Core 调 OnUnload() → 插件清理内部状态 (Core 自动清理服务/事件注册)
    ///
    /// 设计: 插件只通过 IHostAPI 和 Core 交互, 不直接 include Core 内部头文件.
    /// 这保证了 "Core 不依赖插件, 插件只依赖接口" 的依赖方向.
    /// </summary>
    class IPlugin {
    public:
        virtual ~IPlugin( ) = default;
        virtual PluginManifest Manifest( ) const = 0;
        virtual void OnLoad( IHostAPI* host ) = 0;
        virtual void OnUnload( ) = 0;
    };

    /// <summary>
    /// 编译期插件契约检查.
    /// 用法: static_assert(AlicePlugin<MyPlugin>);
    /// 确保类型实现了 IPlugin 的所有必需方法.
    /// </summary>
    template<typename T>
    concept AlicePlugin = std::derived_from<T, IPlugin> && requires( T t, IHostAPI* host ) {
        { t.Manifest( ) } -> std::same_as<PluginManifest>;
        { t.OnLoad( host ) };
        { t.OnUnload( ) };
    };

    /// <summary>
    /// C++ DLL 插件导出宏.
    /// 原生插件必须导出 alice_create_plugin() 和 alice_destroy_plugin().
    /// TODO: DLL 插件加载尚未实现, 这只是接口预留.
    /// </summary>
#ifdef _WIN32
#define ALICE_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define ALICE_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

} // namespace alice
