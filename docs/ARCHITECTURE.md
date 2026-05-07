# 架构

## 总览

Alice 是微内核架构。Core 极小，只提供运行时能力，不包含业务逻辑。

```mermaid
graph TB
    subgraph Server["AliceServer.exe"]
        subgraph Core["AliceCore (微内核)"]
            direction LR
            Event["Event\n事件总线"]
            Service["Service\n服务注册"]
            Pipeline["Pipeline\n管线引擎"]
            Net["Net\nHTTP/WS/SSE"]
            Storage["Storage\n文件/KV"]
            Script["Script\nLua/JS/CLR"]
            Schedule["Schedule\n线程池/定时器"]
        end

        Host["IHostAPI + HostBridge\n(C ABI 桥接层)"]

        subgraph Mods["Mod 插件层"]
            direction LR
            M1["ai-manager\n多 Provider 路由"]
            M2["chat-pipeline\nRegen Loop"]
            M3["providers/*\nLLM 协议"]
            M4["handlers/*\n工具执行"]
        end
    end

    Core --> Host
    Host --> Mods
    Mods -.->|"alice.*\nAPI 调用"| Host

    style Core fill:#1a1a2e,color:#fff
    style Host fill:#16213e,color:#fff
    style Mods fill:#0f3460,color:#fff
```

## 依赖方向

```mermaid
graph LR
    Mod["Mod 插件"] -->|"调用"| API["IHostAPI / HostBridge"]
    API -->|"委托"| Core["Core 模块"]
    Mod -.->|"事件/服务"| Mod

    style Mod fill:#0f3460,color:#fff
    style API fill:#16213e,color:#fff
    style Core fill:#1a1a2e,color:#fff
```

单向依赖。Core 不引用任何 Mod。Mod 之间通过事件和服务通信，不直接引用。

## Core 模块职责

| 模块 | 文件 | 职责 |
|------|------|------|
| Runtime | Runtime.h/.cpp | 启动/关闭/生命周期，插件拓扑排序加载 |
| Event | EventBus.h/.cpp | 事件总线，同步/异步，TTL + 深度治理 |
| Service | ServiceRegistry.h/.cpp | 通用能力注册/调用/等待 |
| Pipeline | Pipeline.h/.cpp | 管线引擎，Continue/Break/Retry/Error |
| Net | HttpServer, HttpClient, RouteRegistry, WsRouter, SseParser | HTTP/WS/SSE |
| Storage | FileStorage, KvStore, Config | 文件读写 + KV + 配置 |
| Script | LuaEngine, QuickJSEngine, ClrHost, HostBridge | 三引擎 + C ABI 桥接 |
| Schedule | ThreadPool, Timer | 线程池 + 定时器 |
| Plugin | PluginRegistry, ScriptPluginAdapter, ClrPluginAdapter | 插件管理 |
| Host | IHostAPI, HostAPIImpl | 子接口聚合 + 实现 |
| Platform | Platform.h, WinPlatform.cpp | 平台抽象 (FsWatcher, 路径) |
| Trace | Trace.h/.cpp | 链路追踪 |

## 插件生命周期

```mermaid
sequenceDiagram
    participant R as Runtime
    participant A as 适配器
    participant M as Mod 代码

    R->>R: 扫描 mods/, 解析 alice.json
    R->>R: 拓扑排序 (dependencies)
    R->>A: 创建适配器 (Script/CLR)
    R->>A: OnLoad(host)
    A->>M: 执行 onLoad()
    M->>M: alice.service.register(...)
    M->>M: alice.event.on(...)

    Note over R,M: 运行中...

    R->>A: OnUnload()
    A->>M: 执行 onUnload()
    R->>R: 自动清理服务/事件/路由注册
```

## HostBridge — 跨语言桥接

IHostAPI 用了 C++ 类型 (std::string, json, std::function)，不能直接跨 DLL/语言边界。
HostBridge 是它的 "C 投影"：一个纯 C 函数指针表，参数只用 `const char*`、`int`、`void*`。

```mermaid
graph LR
    CPP["C++ 插件"] -->|"直接调用"| API["IHostAPI"]
    Lua["Lua 插件"] -->|"sol2 绑定"| Bridge["HostBridge\n(C ABI)"]
    JS["JS 插件"] -->|"QuickJS C API"| Bridge
    CS["C# 插件"] -->|"P/Invoke"| Bridge
    Bridge --> API

    style Bridge fill:#e94560,color:#fff
```

## Regen Loop (Agent 循环)

```mermaid
flowchart TD
    Start([用户消息]) --> Discover[发现 handler.* 服务]
    Discover --> BuildPrompt[组装 system prompt]
    BuildPrompt --> Loop{Regen Loop}

    Loop --> CallLLM[调用 LLM 流式]
    CallLLM --> RunHandlers[遍历 Handler 解析标签]
    RunHandlers --> Check{任何 handler\nregen=true?}

    Check -->|是| AppendLogs[追加工具结果到 messages]
    AppendLogs --> Counter{迭代 < 10?}
    Counter -->|是| CallLLM
    Counter -->|否| End([返回最终回复])

    Check -->|否| End

    style Loop fill:#e94560,color:#fff
    style End fill:#0f3460,color:#fff
```

## 流式输出

```mermaid
graph LR
    LLM["LLM API\n(SSE)"] -->|"fetch_stream"| Provider["协议插件"]
    Provider -->|"emit(chat.token)"| CP["chat-pipeline"]
    CP -->|"Stream Masker"| WS["WS 广播"]
    CP -->|"Stream Masker"| SSE["SSE 推送"]
    WS --> Client["客户端"]
    SSE --> Client

    style LLM fill:#1a1a2e,color:#fff
```

Stream Masker 是逐字符状态机：普通文本立即转发，检测到标签开头则缓冲并显示占位符。
