# Mod 开发指南

## 基本概念

Mod 是 Alice 的扩展单元。每个 Mod 是一个目录，包含 `alice.json` 清单文件和入口代码。
Alice 启动时自动扫描 `mods/` 目录，按依赖关系排序后加载。

支持四种语言：Lua、JavaScript (QuickJS)、C# (.NET 10)、C++ DLL。

## 目录结构

```
mods/my-mod/
├── alice.json      必须
└── main.lua        Lua 入口
    main.mjs        或 JS 入口
    main.js         或 JS 入口
    MyMod.dll       或 C# 入口 (需要配合 runtime/entry_type 字段)
```

## alice.json

```json
{
    "id": "my-mod",
    "name": "My Mod",
    "version": "1.0.0",
    "type": "plugin",
    "dependencies": ["ai-manager"]
}
```

| 字段 | 必须 | 说明 |
|------|------|------|
| id | 是 | 唯一标识，建议 kebab-case |
| name | 否 | 显示名 |
| version | 否 | 语义版本号 |
| type | 否 | plugin / provider / handler / shell |
| dependencies | 否 | 依赖的其他 Mod ID (影响加载顺序) |
| runtime | 否 | "dotnet" 表示 C# 插件 |
| entry_type | 否 | C# 入口类型 (如 "MyMod.PluginEntry, MyMod") |
| entry_file | 否 | C# DLL 文件名 |

## Lua Mod

```lua
function onLoad()
    -- 注册服务
    alice.service.register("my.service", function(method, args_json)
        if method == "hello" then
            return alice.json.encode({ message = "hello world" })
        end
        return alice.json.encode({ error = "unknown method" })
    end)

    -- 监听事件
    alice.event.on("some.event", function(data_json)
        alice.log.info("收到事件: " .. data_json)
    end)

    alice.log.info("My Mod loaded!")
end

function onUnload()
    alice.log.info("My Mod unloaded")
end
```

## JavaScript Mod

```javascript
function onLoad() {
    alice.service.register("my.service", (method, argsJson) => {
        if (method === "hello") {
            return JSON.stringify({ message: "hello from JS" });
        }
        return JSON.stringify({ error: "unknown method" });
    });

    alice.log.info("My JS Mod loaded!");
}

function onUnload() {
    alice.log.info("My JS Mod unloaded");
}
```

JS 自带 `JSON.parse` / `JSON.stringify`，不需要 `alice.json.encode/decode`。

## C# Mod

### 项目结构

```
mods/my-csharp-mod/
├── alice.json
├── MyCsharpMod.csproj
└── PluginEntry.cs
```

### alice.json

```json
{
    "id": "my-csharp-mod",
    "name": "My C# Mod",
    "version": "1.0.0",
    "type": "plugin",
    "runtime": "dotnet",
    "entry_type": "MyCsharpMod.PluginEntry, MyCsharpMod",
    "entry_file": "MyCsharpMod.dll"
}
```

### csproj

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <EnableDynamicLoading>true</EnableDynamicLoading>
    <GenerateRuntimeConfigurationFiles>true</GenerateRuntimeConfigurationFiles>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="..\..\AliceSdk\Alice.SDK.csproj" />
  </ItemGroup>
</Project>
```

### PluginEntry.cs

```csharp
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Alice.SDK;

public static class PluginEntry
{
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void Initialize(nint hostBridgePtr)
    {
        Alice.SDK.Alice.Initialize(hostBridgePtr);
        Alice.SDK.Alice.Log.Info("C# Mod loaded!");

        Alice.SDK.Alice.Service.Register("my.service", (method, args) =>
        {
            return $"{{\"echo\":\"{method}\"}}";
        });
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void Shutdown()
    {
        Alice.SDK.Alice.Log.Info("C# Mod unloaded");
        Alice.SDK.Alice.Cleanup();
    }
}
```

### 部署

```bash
dotnet build
# 把 DLL 复制到运行目录的 mods/my-csharp-mod/
# Alice.SDK.dll 放到 sdk/ (只需一份)
```

## alice.* API 详细参考

### alice.log

```lua
alice.log.info("消息")
alice.log.warn("警告")
alice.log.error("错误")
alice.log.debug("调试")
```

### alice.event

```lua
-- 同步发送事件
alice.event.emit("event.name", { key = "value" })

-- 异步发送 (投递到线程池, 立即返回)
alice.event.emitAsync("event.name", { key = "value" })

-- 监听事件
local handle = alice.event.on("event.name", function(data_json)
    local data = alice.json.decode(data_json)
end)

-- 取消监听
alice.event.off(handle)
```

### alice.service

```lua
-- 注册服务
alice.service.register("my.capability", function(method, args_json)
    return alice.json.encode({ result = "ok" })
end)

-- 调用服务
local result = alice.service.call("my.capability", "method", '{"arg":1}')

-- 等待服务注册 (阻塞, 超时返回 false)
local ok = alice.service.waitFor("ai.chat", 5000)

-- 列出所有服务
local services_json = alice.service.list()
```

### alice.fs

```lua
alice.fs.write("path/to/file.txt", "内容")
local content = alice.fs.read("path/to/file.txt")  -- 不存在返回 nil
local exists = alice.fs.exists("path/to/file.txt")
```

### alice.kv

```lua
-- KV 按插件隔离, 插件 A 看不到插件 B 的数据
alice.kv.set("key", '{"value":42}')
local v = alice.kv.get("key")  -- 不存在返回 nil
```

### alice.net

```lua
-- 同步 HTTP 请求
local resp = alice.net.fetch("https://example.com", alice.json.encode({
    method = "POST",
    headers = { ["Content-Type"] = "application/json" },
    body = '{"key":"value"}',
}))

-- 流式 HTTP + SSE
alice.net.fetch_stream(url, opts_json, function(event_type, event_data)
    -- 每个 SSE 事件调一次
    return true  -- 返回 false 中止
end)

-- 注册 HTTP 路由
alice.net.addRoute("GET", "/api/my/endpoint", function(req_json)
    return '{"message":"hello"}'
end)
```

### alice.ws

```lua
-- 注册 WS 消息处理
alice.ws.handle("my.type", function(req_json)
    return '{"echo":"ok"}'
end)

-- 向指定会话广播
alice.ws.broadcast(session_id, { type = "notify", data = "hello" })
```

### alice.timer

```lua
-- 设置定时器 (到期时发 "timer.expired" 事件)
local id = alice.timer.set("5m", "提醒标签", '{"custom":"data"}')

-- 删除定时器
alice.timer.remove(id)

-- 列出活跃定时器
local timers_json = alice.timer.list()
```

### alice.pipeline

```lua
-- 注册管线阶段
alice.pipeline.register("my-pipeline", "step1", 100, function(input_json, shared_json)
    local input = alice.json.decode(input_json)
    return alice.json.encode({ result = input.value + 10 })
end)

-- 执行管线
local result = alice.pipeline.execute("my-pipeline", '{"value":5}')

-- 控制流程 (通过 _action 字段):
-- return alice.json.encode({ result = 42, _action = "break" })  -- 终止管线
-- return alice.json.encode({ _action = "retry" })               -- 重试当前阶段
-- return alice.json.encode({ _action = "error" })               -- 报错终止
```

### alice.process

```lua
local result_json = alice.process.exec("echo hello")
-- 返回: {"exit_code":0,"stdout":"hello\r\n","stderr":""}
```

### alice.script

```lua
-- 创建临时引擎执行代码 (完整 alice.* API)
local result = alice.script.eval("lua", "return tostring(1+2)")
local result = alice.script.eval("js", "'hello from js'")
-- 返回: {"success":true,"return_value":"3","output":"","error":""}
```

## 写一个 Handler

Handler 是特殊的 Mod，注册 `handler.*` 前缀的服务。chat-pipeline 自动发现并调用。

```lua
function onLoad()
    alice.service.register("handler.my-tool", function(method, args_json)
        if method == "prompt" then
            return alice.json.encode({
                order = 150,
                text = "你可以用 <my-tool>指令</my-tool> 来做某事。"
            })
        elseif method == "handle" then
            local args = alice.json.decode(args_json)
            local content = args.content
            -- 解析标签, 执行操作, 返回结果
            return alice.json.encode({ regen = false, logs = {} })
        elseif method == "mask" then
            return alice.json.encode({
                block_tags = {
                    { start = "<my-tool>", close = "</my-tool>", placeholder = "[处理中...]" }
                }
            })
        end
    end)
end
```

Handler 服务的三个方法：
- `prompt` — 返回工具描述 (注入 system prompt) + 排序权重
- `handle` — 处理 LLM 回复，解析标签，返回 `{regen, logs}`
- `mask` — 返回流式输出时需要隐藏的标签对
