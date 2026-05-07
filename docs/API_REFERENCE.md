# alice.* API 参考手册

所有 Mod 通过 `alice.*` 全局命名空间访问平台能力。这套 API 在 Lua、JavaScript、C# 中完全一致。

---

## alice.log

日志输出。自动附带 `[plugin_id]` 前缀，底层走 spdlog。

| 方法 | 参数 | 说明 |
|------|------|------|
| `info(msg)` | string | 信息级别 |
| `warn(msg)` | string | 警告级别 |
| `error(msg)` | string | 错误级别 |
| `debug(msg)` | string | 调试级别 (需 spdlog debug 开启) |

```lua
alice.log.info("服务已启动")
alice.log.warn("配置缺失，使用默认值")
```

---

## alice.event

事件总线。Mod 之间通信的主要通道。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `emit(name, data)` | string, table/string | 无 | 同步分发，所有订阅者在当前线程执行 |
| `emitAsync(name, data)` | string, table/string | 无 | 异步分发，投递到线程池后立即返回 |
| `on(name, callback)` | string, function | handle (number) | 订阅事件，callback 收到 data_json 字符串 |
| `off(handle)` | number | 无 | 取消订阅 |

```lua
-- 订阅
local handle = alice.event.on("chat.done", function(data_json)
    local data = alice.json.decode(data_json)
    alice.log.info("聊天完成: " .. data.model)
end)

-- 发送
alice.event.emit("my.event", { key = "value" })

-- 异步发送 (不阻塞当前代码)
alice.event.emitAsync("background.task", { id = 42 })

-- 取消
alice.event.off(handle)
```

**注意**: `emitAsync` 的回调在线程池线程中执行。Lua/JS 引擎是单线程的，不要在 `emitAsync` 的回调里访问当前 Mod 的状态。`emitAsync` 适合通知其他 C++ 模块或触发不需要回传结果的操作。

---

## alice.service

通用服务注册与调用。Core 不理解服务名的含义，Mod 自己约定。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `register(capability, handler)` | string, function | 无 | 注册服务。同一 Mod 可覆盖注册 |
| `call(capability, method, args_json)` | string, string, string | string (JSON) | 调用服务。服务不存在返回 "null" |
| `waitFor(capability, timeout_ms)` | string, number | boolean | 阻塞等待服务注册。超时返回 false |
| `list()` | 无 | string (JSON array) | 列出所有已注册服务 |

```lua
-- 注册
alice.service.register("my.calculator", function(method, args_json)
    local args = alice.json.decode(args_json)
    if method == "add" then
        return alice.json.encode({ result = args.a + args.b })
    end
    return alice.json.encode({ error = "未知方法: " .. method })
end)

-- 调用
local result = alice.service.call("my.calculator", "add", '{"a":1,"b":2}')
-- result = '{"result":3}'

-- 等待依赖的服务就绪
local ok = alice.service.waitFor("ai.chat", 5000)  -- 最多等 5 秒
if not ok then alice.log.error("ai.chat 服务未就绪") end
```

**handler 签名**: `function(method: string, args_json: string) -> string (JSON)`

---

## alice.fs

文件读写。路径相对于 AliceServer.exe 所在目录，也支持绝对路径。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `read(path)` | string | string 或 nil | 读取文件内容。不存在返回 nil |
| `write(path, content)` | string, string | boolean | 写入文件。原子操作 (先写 .tmp 再 rename) |
| `exists(path)` | string | boolean | 检查文件是否存在 |

```lua
alice.fs.write("output/result.txt", "hello world")
local content = alice.fs.read("output/result.txt")
if alice.fs.exists("config.json") then ... end
```

---

## alice.kv

键值存储。按 Mod ID 命名空间隔离——Mod A 看不到 Mod B 的数据。
底层存储在 `data/kv/{mod_id}.json`。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `get(key)` | string | string (JSON) 或 nil | 获取值。不存在返回 nil |
| `set(key, value_json)` | string, string | 无 | 设置值 (JSON 字符串) |

```lua
alice.kv.set("user_prefs", '{"theme":"dark","lang":"zh"}')
local prefs = alice.kv.get("user_prefs")
-- prefs = '"{\\"theme\\":\\"dark\\",\\"lang\\":\\"zh\\"}"'
```

---

## alice.net

出站 HTTP 请求 + 入站路由注册。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `fetch(url, opts_json)` | string, string | string (JSON) 或 nil | 同步 HTTP 请求 |
| `fetch_stream(url, opts_json, callback)` | string, string, function | number (status) | 流式 HTTP + SSE 解析 |
| `addRoute(method, path, handler)` | string, string, function | 无 | 注册 HTTP 路由 |

### fetch

```lua
local resp = alice.net.fetch("https://httpbin.org/get")
local data = alice.json.decode(resp)
-- data.status = 200, data.body = "..."

-- POST 请求
local resp = alice.net.fetch("https://api.example.com/data", alice.json.encode({
    method = "POST",
    headers = { ["Content-Type"] = "application/json", ["Authorization"] = "Bearer xxx" },
    body = '{"query":"hello"}',
}))
```

**opts 格式**: `{"method":"GET","headers":{},"body":"..."}`
**返回格式**: `{"status":200,"body":"..."}`

### fetch_stream

```lua
-- 流式接收 SSE 事件
local status = alice.net.fetch_stream(url, opts_json, function(event_type, event_data)
    -- event_type: SSE event 字段 (通常为空)
    -- event_data: SSE data 字段 (通常是 JSON 字符串)
    alice.log.info("收到: " .. event_data)
    return true  -- 返回 false 中止流
end)
```

调用阻塞直到流结束。回调在同一线程中同步执行，不涉及跨线程问题。

### addRoute

```lua
alice.net.addRoute("GET", "/api/my/endpoint", function(req_json)
    -- req_json: {"method":"GET","path":"/api/my/endpoint","body":""}
    return '{"message":"hello","status":200}'
end)
```

handler 返回的 JSON 如果包含 `status` (number) 字段，会作为 HTTP 状态码。
如果包含 `body` 字段，会作为响应体。否则整个 JSON 作为响应体。

---

## alice.ws

WebSocket 消息处理 + 会话广播。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `handle(type, handler)` | string, function | 无 | 注册 WS 消息处理 (按 type 路由) |
| `broadcast(session_id, data)` | string, table/string | 无 | 向指定会话的所有 WS 客户端广播 |

```lua
alice.ws.handle("my.action", function(req_json)
    return '{"result":"ok"}'
end)

alice.ws.broadcast("session-123", { type = "notify", content = "更新了" })
```

WS 客户端消息格式: `{"type":"my.action","data":{...},"request_id":"xxx"}`

---

## alice.timer

定时器。到期时通过 EventBus 发布 `timer.expired` 事件。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `set(duration, label, data_json)` | string, string, string | string (timer_id) | 设置定时器 |
| `remove(id)` | string | 无 | 删除定时器 |
| `list()` | 无 | string (JSON array) | 列出活跃定时器 |

**duration 格式**: `"5s"`, `"2m"`, `"1h30m"`, `"1d"`

```lua
local id = alice.timer.set("5m", "提醒喝水", '{"custom":"data"}')

-- 到期时会发事件:
alice.event.on("timer.expired", function(data_json)
    local data = alice.json.decode(data_json)
    alice.log.info("定时器到期: " .. data.label)
end)

-- 取消
alice.timer.remove(id)

-- 列出
local timers = alice.json.decode(alice.timer.list())
-- [{id:"timer-1", label:"提醒喝水", remaining_seconds:280, ...}]
```

---

## alice.pipeline

管线引擎。注册阶段 (stage) 后按 order 顺序执行。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `register(pipeline, stage, order, handler)` | string, string, number, function | 无 | 注册阶段 |
| `execute(pipeline, input_json)` | string, string | string (JSON) | 执行管线 |

```lua
alice.pipeline.register("my-pipeline", "validate", 100, function(input_json, shared_json)
    local input = alice.json.decode(input_json)
    if not input.value then
        return alice.json.encode({ error = "缺少 value", _action = "error" })
    end
    return alice.json.encode({ validated = true, value = input.value })
end)

alice.pipeline.register("my-pipeline", "transform", 200, function(input_json, shared_json)
    local input = alice.json.decode(input_json)
    return alice.json.encode({ result = input.value * 2 })
end)

local result = alice.pipeline.execute("my-pipeline", '{"value":21}')
-- result = '{"result":42}'
```

**handler 签名**: `function(input_json: string, shared_json: string) -> string (JSON)`

**流程控制** (通过返回 JSON 的 `_action` 字段):
| _action | 效果 |
|---------|------|
| (不设置) | Continue，继续下一阶段 |
| `"break"` | 终止管线，返回当前数据 |
| `"retry"` | 重试当前阶段 (最多 3 次) |
| `"error"` | 报错终止管线 |

---

## alice.process

子进程执行。内部使用 `cmd /c {command}` 包装。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `exec(command, opts_json)` | string, string (可选) | string (JSON) | 执行命令，等待完成 |

```lua
local result = alice.json.decode(alice.process.exec("echo hello"))
-- result.exit_code = 0
-- result.stdout = "hello\r\n"
-- result.stderr = ""

-- PowerShell
local r = alice.json.decode(alice.process.exec('powershell -NoProfile -Command "Get-Date"'))
```

**返回格式**: `{"exit_code":0,"stdout":"...","stderr":"..."}`

输出经过 UTF-8 清洗，非法字节替换为 `\xHH`，不会导致 JSON 构造崩溃。

---

## alice.path

路径操作。基于 `std::filesystem`，跨平台。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `join(a, b)` | string, string | string | 拼接路径 |
| `dirname(path)` | string | string | 父目录 |
| `basename(path)` | string | string | 文件名 |
| `ext(path)` | string | string | 扩展名 (含点) |
| `absolute(path)` | string | string | 绝对路径 |

```lua
alice.path.join("a/b", "c.txt")     -- "a/b\c.txt" (Windows)
alice.path.dirname("a/b/c.txt")     -- "a/b"
alice.path.basename("a/b/c.txt")    -- "c.txt"
alice.path.ext("a/b/c.txt")         -- ".txt"
alice.path.absolute(".")            -- "G:\Alice\Build\bin\Debug"
```

---

## alice.regex

正则表达式。使用 `std::regex`，ECMAScript 语法。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `test(text, pattern)` | string, string | boolean | 是否匹配 |
| `match(text, pattern)` | string, string | string (JSON array) 或 nil | 返回匹配组 |
| `replace(text, pattern, replacement)` | string, string, string | string | 替换 |

```lua
alice.regex.test("hello world", "wor")         -- true
alice.regex.test("hello world", "^xyz$")        -- false
alice.regex.match("age: 25", "([0-9]+)")        -- '["25","25"]' (全匹配 + 捕获组)
alice.regex.replace("hello world", "world", "lua")  -- "hello lua"
```

**注意**: Lua 字符串中的 `\d` 需要写成 `\\d` 或用 `[0-9]` 替代 (Lua 5.4 不支持 `\d` 转义)。

---

## alice.encoding

编码工具。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `base64encode(data)` | string | string | Base64 编码 |
| `base64decode(data)` | string | string | Base64 解码 |
| `hex(data)` | string | string | 十六进制编码 |

```lua
alice.encoding.base64encode("Hello!")    -- "SGVsbG8h"
alice.encoding.base64decode("SGVsbG8h")  -- "Hello!"
alice.encoding.hex("AB")                 -- "4142"
```

---

## alice.time

时间工具。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `now()` | 无 | number | 当前时间戳 (毫秒, Unix epoch) |
| `format(timestamp_ms, fmt)` | number, string | string | 格式化时间 (strftime 语法) |

```lua
local ts = alice.time.now()                           -- 1778128909033
alice.time.format(ts, "%Y-%m-%d %H:%M:%S")            -- "2026-05-07 12:41:49"
```

---

## alice.platform

平台信息。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `name()` | 无 | string | 平台名 ("Windows") |
| `dataDir()` | 无 | string | 数据目录 (exe 同级 data/) |
| `exeDir()` | 无 | string | 可执行文件目录 |

```lua
alice.platform.name()      -- "Windows"
alice.platform.dataDir()   -- "G:\Alice\Build\bin\Debug\data"
alice.platform.exeDir()    -- "G:\Alice\Build\bin\Debug"
```

---

## alice.script

临时脚本引擎执行。创建一个隔离的 Lua/JS 引擎，注入完整 `alice.*` API，执行代码后销毁。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `eval(lang, code)` | string, string | string (JSON) | 执行代码，返回结果 |

**lang**: `"lua"` 或 `"js"`

```lua
local r = alice.json.decode(alice.script.eval("lua", "return tostring(1+2)"))
-- r.success = true, r.return_value = "3"

local r = alice.json.decode(alice.script.eval("js", "alice.platform.name()"))
-- r.success = true, r.return_value = "Windows"
```

**返回格式**: `{"success":bool,"return_value":"...","output":"...","error":"..."}`

临时引擎拥有完整的 `alice.*` API，可以读写文件、调用服务、发送事件等。
用于 `<run-lua>` / `<run-js>` handler 标签的底层实现。

---

## alice.json (仅 Lua)

JSON 编解码。JavaScript 自带 `JSON.parse` / `JSON.stringify`，不需要这个。

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `encode(obj)` | table | string | Lua table → JSON 字符串 |
| `decode(str)` | string | table 或 nil | JSON 字符串 → Lua table。解析失败返回 nil |

```lua
alice.json.encode({ name = "Alice", version = 2 })  -- '{"name":"Alice","version":2}'
alice.json.encode({ 1, 2, 3 })                      -- '[1,2,3]'

local t = alice.json.decode('{"a":1,"b":"hello"}')
-- t.a = 1, t.b = "hello"

alice.json.decode("invalid json")  -- nil
alice.json.decode("null")          -- nil
```

自动区分 array 和 object：如果 table 的 key `[1]` 存在，视为 array。

---

## C# 差异

C# 通过 `Alice.SDK.Alice` 静态类访问，命名空间用 PascalCase：

```csharp
Alice.SDK.Alice.Log.Info("hello");
Alice.SDK.Alice.Service.Register("my.svc", (method, args) => "{}");
var result = Alice.SDK.Alice.Service.Call("my.svc", "test");
Alice.SDK.Alice.Event.Emit("my.event", "{\"key\":\"value\"}");
Alice.SDK.Alice.Fs.Write("test.txt", "content");
```

C# 中 event data 和 service args 都是 JSON 字符串 (不是原生对象)，需要手动序列化/反序列化。
