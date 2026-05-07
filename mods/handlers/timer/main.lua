-- Handler: Timer
-- LLM 通过 <set-timer>, <list-timers/>, <remove-timer> 管理定时器

function onLoad()
    alice.service.register("handler.timer", function(method, args_json)
        if method == "prompt" then return get_prompt()
        elseif method == "handle" then return handle_reply(args_json)
        elseif method == "mask" then return get_mask()
        end
        return alice.json.encode({ error = "unknown method" })
    end)
    alice.log.info("Handler 已注册: timer")
end

function get_prompt()
    return alice.json.encode({
        order = 200,
        text = [[你可以通过以下标签管理定时器:

设置定时器:
<set-timer>
<time>30m</time>
<reason>提醒内容</reason>
</set-timer>

时间格式: 30s(秒), 5m(分钟), 2h(小时), 1d(天), 组合如 1h30m

列出所有定时器:
<list-timers/>

删除定时器:
<remove-timer>timer-id</remove-timer>]]
    })
end

function get_mask()
    return alice.json.encode({
        block_tags = {
            { start = "<set-timer>", close = "</set-timer>", placeholder = "[设置定时器...]" },
            { start = "<remove-timer>", close = "</remove-timer>", placeholder = "[删除定时器...]" },
            { start = "<list-timers", close = "/>", placeholder = "[查询定时器...]" },
        }
    })
end

function parse_time_str(s)
    if not s then return nil end
    s = s:match("^%s*(.-)%s*$")
    -- 支持纯数字 (当作分钟)
    local n = tonumber(s)
    if n then return tostring(math.floor(n)) .. "m" end
    -- 已经是正确格式 (含 s/m/h/d)
    if s:match("[smhd]") then return s end
    return s
end

function handle_reply(args_json)
    local args = alice.json.decode(args_json)
    if not args or not args.content then
        return alice.json.encode({ regen = false, logs = {} })
    end

    local content = args.content
    local regen = false
    local logs = {}
    local has_action = false

    -- 处理 <set-timer>
    local search_start = 1
    while true do
        local s, e, inner = content:find("<set%-timer>(.-)</set%-timer>", search_start)
        if not s then break end
        has_action = true

        local time_str = inner:match("<time>(.-)</time>")
        local reason = inner:match("<reason>(.-)</reason>")

        if time_str and reason then
            local duration = parse_time_str(time_str)
            local timer_id = alice.timer.set(duration, reason, alice.json.encode({
                reason = reason,
                session_id = args.session_id or "default"
            }))
            table.insert(logs, {
                role = "tool",
                name = "timer",
                content = "定时器已设置: " .. timer_id .. " (" .. duration .. ") - " .. reason
            })
            alice.log.info("[timer] 设置: " .. timer_id .. " " .. duration .. " - " .. reason)
        else
            table.insert(logs, {
                role = "tool",
                name = "timer",
                content = "定时器设置失败: 缺少 <time> 或 <reason>"
            })
        end
        regen = true
        search_start = e + 1
    end

    -- 处理 <list-timers/> (自闭合)
    if content:find("<list%-timers%s*/>") or content:find("<list%-timers>%s*</list%-timers>") then
        has_action = true
        local timers_raw = alice.timer.list()
        local timers = alice.json.decode(timers_raw) or {}

        if #timers == 0 then
            table.insert(logs, {
                role = "tool",
                name = "timer",
                content = "当前没有活跃的定时器。"
            })
        else
            local lines = { "当前活跃定时器:" }
            for _, t in ipairs(timers) do
                local remaining = t.remaining_seconds or 0
                local time_desc
                if remaining >= 3600 then
                    time_desc = string.format("%dh%dm", math.floor(remaining / 3600), math.floor(remaining % 3600 / 60))
                elseif remaining >= 60 then
                    time_desc = string.format("%dm%ds", math.floor(remaining / 60), remaining % 60)
                else
                    time_desc = remaining .. "s"
                end
                table.insert(lines, "- " .. t.id .. ": " .. (t.label or "") .. " (剩余 " .. time_desc .. ")")
            end
            table.insert(logs, {
                role = "tool",
                name = "timer",
                content = table.concat(lines, "\n")
            })
        end
        regen = true
    end

    -- 处理 <remove-timer>
    search_start = 1
    while true do
        local s, e, timer_id = content:find("<remove%-timer>(.-)</remove%-timer>", search_start)
        if not s then break end
        has_action = true

        timer_id = timer_id:match("^%s*(.-)%s*$")
        alice.timer.remove(timer_id)
        table.insert(logs, {
            role = "tool",
            name = "timer",
            content = "定时器已删除: " .. timer_id
        })
        alice.log.info("[timer] 删除: " .. timer_id)
        regen = true
        search_start = e + 1
    end

    if has_action then
        table.insert(logs, 1, {
            role = "assistant",
            name = "timer",
            content = content
        })
    end

    return alice.json.encode({ regen = regen, logs = logs })
end

function onUnload()
    alice.log.info("Handler 已卸载: timer")
end
