# 编译指南

## 环境要求

| 工具 | 版本 | 用途 |
|------|------|------|
| Visual Studio 2022 | 17.x | C++ 编译 (v143 工具链) |
| vcpkg | 最新 | C++ 包管理 |
| .NET SDK | 10.0+ | C# 插件编译 (可选) |
| bun | 1.x | 集成测试 (可选) |

## vcpkg 依赖

vcpkg.json 声明了以下依赖，首次编译时自动安装：

| 包 | 用途 |
|---|------|
| drogon | HTTP/WS 服务器 |
| lua + sol2 | Lua 脚本引擎 |
| quickjs-ng | JavaScript 脚本引擎 |
| nlohmann-json | JSON 序列化 |
| spdlog | 日志 |

triplet: `x64-windows-static-md` (vcpkg 静态库 + 动态 CRT)

## 编译步骤

### Debug

1. 用 VS2022 打开 `Alice.sln`
2. 选择 `Debug | x64`
3. 生成解决方案 (Ctrl+Shift+B)
4. 输出在 `Build/bin/Debug/`

### Release

Release 配置使用静态 CRT (`/MT`)，需要对应的 vcpkg triplet。
AliceCore 和 AliceServer 的 Release 配置已设置 `x64-windows-static` + `MultiThreaded`。

## C# 插件编译

```bash
cd mods/hello-csharp
dotnet build
```

编译输出在 `mods/hello-csharp/bin/Debug/net10.0/`。

部署：把 DLL 复制到 AliceServer 运行目录的 `mods/hello-csharp/` 下。
Alice.SDK.dll 放到 `sdk/` 目录 (所有 C# 插件共享)。

## 目录结构

编译后的运行时目录：

```
Build/bin/Debug/
├── AliceServer.exe
├── nethost.dll           (Debug 需要, Release 静态链接不需要)
├── alice.runtimeconfig.json  (CLR 自动生成)
├── sdk/
│   └── Alice.SDK.dll     (C# 插件 SDK, 共享)
├── mods/                 (手动复制)
│   ├── ai-manager/
│   ├── chat-pipeline/
│   └── ...
└── data/
    ├── config.json       (首次运行自动生成)
    ├── ai-config.json    (手动创建, AI Provider 配置)
    └── kv/               (KV 存储)
```

## 验证

启动后访问 `http://localhost:645/api/ping`，返回 `{"status":"ok"}` 表示正常。
