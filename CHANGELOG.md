# 更新日志

## v0.3.0 (2026-05-08)

首个完整版本。

### Core
- 微内核架构: 12 个模块，零业务代码
- 四种插件语言: C++ DLL / C# (.NET 10) / Lua (sol2) / JavaScript (QuickJS)
- 统一 alice.* API (17 个命名空间, 50+ 方法)
- 事件总线: 同步/异步分发, TTL, 链路深度限制, 懒序列化
- 服务注册: 透明哈希, 覆盖注册, waitFor 条件变量等待
- 管线引擎: Continue/Break/Retry/Error, 脚本可控
- HTTP 服务器 (Drogon) + HTTP 客户端 (WinHTTP) + SSE 解析
- WebSocket: 会话绑定, 双向通信, 流式推送
- SSE 端点: 聊天流式 + 通用事件推送
- 文件存储 (原子写入) + KV 存储 (per-plugin 隔离)
- 线程池 + 定时器
- FsWatcher 热重载 (ReadDirectoryChangesW)
- 插件依赖拓扑排序
- 崩溃隔离 (单个插件 OnLoad 失败不影响其他)
- 链路追踪 (Span)
- CLR 宿主 (hostfxr, TPA 注入, 共享 SDK 目录)

### Mod 系统
- Handler + Regen Loop: LLM 工具调用循环 (最多 10 轮)
- Stream Masker: 逐字符状态机, 实时隐藏 XML 标签
- code-exec: `<run-pwsh>` / `<run-bash>` / `<run-lua>` / `<run-js>` / `<inline-lua>` / `<inline-js>`
- file-ops: `<view-file>` / `<write-file>` / `<replace-in-file>`
- timer: `<set-timer>` / `<list-timers/>` / `<remove-timer>`
- 多 LLM Provider: OpenAI / Claude / Gemini / Vertex AI (CF Gateway)
- AI Manager: 多 Provider 路由, 同步 + 流式

### 构建
- MSBuild + vcpkg manifest
- C++23 (MSVC v143)
- .NET 10 SDK
- Debug: 动态 CRT + nethost.dll
- Release: 静态 CRT + libnethost.lib
