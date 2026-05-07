-- Handler: Code Execution
-- <run-pwsh>, <run-bash>: Shell 执行
-- <run-lua>, <run-js>: 脚本引擎执行 (完整 alice.* API)

function onLoad()
    alice.service.register("handler.code-exec", function(method, args_json)
        if method == "prompt" then return get_prompt()
        elseif method == "handle" then return handle_reply(args_json)
        elseif method == "mask" then return get_mask()
        end
        return alice.json.encode({ error = "unknown method" })
    end)
    alice.log.info("Handler 已注册: code-exec")
end

function get_prompt()
    return alice.json.encode({
        order = 100,
        text = [[你可以通过以下标签执行代码并获取结果:

Shell 执行 (结果通过 regen 返回):
<run-pwsh>PowerShell 代码</run-pwsh>
<run-bash>Bash 代码</run-bash>

脚本引擎执行 (可调用完整 alice.* API, 结果通过 regen 返回):
<run-lua>Lua 代码</run-lua>
<run-js>JavaScript 代码</run-js>

内联执行 (结果直接替换到文本中, 适合简短计算):
<inline-lua>Lua 表达式</inline-lua>
<inline-js>JS 表达式</inline-js>

执行后你会收到执行结果。
根据结果决定下一步操作。如果执行失败，请分析原因并尝试不同方案。
一次只执行必要的代码，避免不必要的操作。]]
    })
end

function get_mask()
    return alice.json.encode({
        block_tags = {
            { start = "<run-pwsh>", close = "</run-pwsh>", placeholder = "[PowerShell 执行中...]" },
            { start = "<run-bash>", close = "</run-bash>", placeholder = "[Bash 执行中...]" },
            { start = "<run-lua>", close = "</run-lua>", placeholder = "[Lua 执行中...]" },
            { start = "<run-js>", close = "</run-js>", placeholder = "[JS 执行中...]" },
        }
    })
end

function trim(s)
    return s:match("^%s*(.-)%s*$")
end

function collect_tags(content, pattern, tag_type)
    local steps = {}
    local search_start = 1
    while true do
        local s, e, code = content:find(pattern, search_start)
        if not s then break end
        table.insert(steps, { pos = s, code = trim(code), lang = tag_type })
        search_start = e + 1
    end
    return steps
end

function process_inline(content, pattern, lang)
    return content:gsub(pattern, function(code)
        code = trim(code)
        if lang == "lua" or lang == "js" then
            local result_raw = alice.script.eval(lang, code)
            local result = alice.json.decode(result_raw) or {}
            if result.return_value and #result.return_value > 0 then
                return result.return_value
            elseif result.error then
                return "[error: " .. result.error .. "]"
            end
            return ""
        end
        return ""
    end)
end

function handle_reply(args_json)
    local args = alice.json.decode(args_json)
    if not args or not args.content then
        return alice.json.encode({ regen = false, logs = {} })
    end

    local content = args.content

    -- 先处理 inline 标签 (直接替换, 不触发 regen)
    content = process_inline(content, "<inline%-lua>(.-)</inline%-lua>", "lua")
    content = process_inline(content, "<inline%-js>(.-)</inline%-js>", "js")

    -- 收集 run-* 标签
    local steps = {}
    for _, t in ipairs(collect_tags(content, "<run%-pwsh>(.-)</run%-pwsh>", "pwsh")) do table.insert(steps, t) end
    for _, t in ipairs(collect_tags(content, "<run%-bash>(.-)</run%-bash>", "bash")) do table.insert(steps, t) end
    for _, t in ipairs(collect_tags(content, "<run%-lua>(.-)</run%-lua>", "lua")) do table.insert(steps, t) end
    for _, t in ipairs(collect_tags(content, "<run%-js>(.-)</run%-js>", "js")) do table.insert(steps, t) end

    if #steps == 0 then
        return alice.json.encode({ regen = false, logs = {} })
    end

    table.sort(steps, function(a, b) return a.pos < b.pos end)

    local logs = {
        { role = "assistant", name = "code-exec", content = content }
    }

    for _, step in ipairs(steps) do
        alice.log.info("[code-exec] 执行 " .. step.lang .. ": " .. step.code:sub(1, 100))

        local tool_output

        if step.lang == "pwsh" then
            local cmd = 'powershell -NoProfile -Command "' .. step.code:gsub('"', '\\"') .. '"'
            local result = alice.json.decode(alice.process.exec(cmd)) or {}
            local parts = { "[pwsh] exit_code: " .. tostring(result.exit_code or -1) }
            if result.stdout and #result.stdout > 0 then table.insert(parts, "stdout:\n" .. result.stdout) end
            if result.stderr and #result.stderr > 0 then table.insert(parts, "stderr:\n" .. result.stderr) end
            tool_output = table.concat(parts, "\n")

        elseif step.lang == "bash" then
            local cmd = 'bash -c "' .. step.code:gsub('"', '\\"') .. '"'
            local result = alice.json.decode(alice.process.exec(cmd)) or {}
            local parts = { "[bash] exit_code: " .. tostring(result.exit_code or -1) }
            if result.stdout and #result.stdout > 0 then table.insert(parts, "stdout:\n" .. result.stdout) end
            if result.stderr and #result.stderr > 0 then table.insert(parts, "stderr:\n" .. result.stderr) end
            tool_output = table.concat(parts, "\n")

        elseif step.lang == "lua" or step.lang == "js" then
            local result_raw = alice.script.eval(step.lang, step.code)
            local result = alice.json.decode(result_raw) or {}
            local parts = { "[" .. step.lang .. "] success: " .. tostring(result.success or false) }
            if result.return_value and #result.return_value > 0 then
                table.insert(parts, "return: " .. result.return_value)
            end
            if result.output and #result.output > 0 then
                table.insert(parts, "output:\n" .. result.output)
            end
            if result.error and #result.error > 0 then
                table.insert(parts, "error: " .. result.error)
            end
            tool_output = table.concat(parts, "\n")
        end

        alice.log.info("[code-exec] 结果: " .. tool_output:sub(1, 200))

        table.insert(logs, {
            role = "tool",
            name = "code-exec",
            content = tool_output
        })
    end

    return alice.json.encode({ regen = true, logs = logs })
end

function onUnload()
    alice.log.info("Handler 已卸载: code-exec")
end
