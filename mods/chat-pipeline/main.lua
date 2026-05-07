-- Chat Pipeline: 聊天工作流编排 + Regen Loop + Streaming
-- 注册 "chat.send" 服务
-- 自动发现 handler.* 服务，组装 system prompt，执行 regen 循环
-- 支持流式输出 (通过 chat.token 事件 + Stream Masker + WS 广播)

local MAX_REGEN = 10
local BASE_PROMPT = "你是 Alice，一个智能 AI 助手。回答简洁有用。"

-- ══════════════════════════════════════
-- Stream Masker (逐字符状态机)
-- ══════════════════════════════════════

function create_masker(tag_pairs)
    local m = {
        state = "NORMAL",
        buffer = "",
        tags = tag_pairs or {},
        current_close = nil,
    }

    function m:process(text)
        if #self.tags == 0 then return text end
        local output = ""
        for i = 1, #text do
            output = output .. self:_char(text:sub(i, i))
        end
        return output
    end

    function m:_char(ch)
        if self.state == "NORMAL" then
            if ch == "<" then
                self.state = "BUFFERING"
                self.buffer = "<"
                return ""
            end
            return ch

        elseif self.state == "BUFFERING" then
            self.buffer = self.buffer .. ch
            for _, tag in ipairs(self.tags) do
                if tag.start == self.buffer then
                    self.state = "IN_TAG"
                    self.current_close = tag.close
                    self.buffer = ""
                    return tag.placeholder or "[...]"
                end
            end
            local could_match = false
            for _, tag in ipairs(self.tags) do
                if tag.start:sub(1, #self.buffer) == self.buffer then
                    could_match = true
                    break
                end
            end
            if not could_match then
                local flushed = self.buffer
                self.buffer = ""
                self.state = "NORMAL"
                return flushed
            end
            return ""

        elseif self.state == "IN_TAG" then
            self.buffer = self.buffer .. ch
            if #self.buffer >= #self.current_close then
                if self.buffer:sub(-#self.current_close) == self.current_close then
                    self.buffer = ""
                    self.current_close = nil
                    self.state = "NORMAL"
                end
            end
            return ""
        end
        return ch
    end

    function m:flush()
        local out = self.buffer
        self.buffer = ""
        self.state = "NORMAL"
        self.current_close = nil
        return out
    end

    return m
end

-- ══════════════════════════════════════
-- Handler 发现
-- ══════════════════════════════════════

function discover_handlers()
    local services_raw = alice.service.list()
    local services = alice.json.decode(services_raw)
    if not services then return {} end

    local handlers = {}
    for _, svc in ipairs(services) do
        local cap = svc.capability or svc
        if type(cap) == "string" and cap:match("^handler%.") then
            local prompt_raw = alice.service.call(cap, "prompt", "{}")
            local prompt_data = alice.json.decode(prompt_raw)
            if prompt_data and prompt_data.text then
                table.insert(handlers, {
                    service = cap,
                    name = cap:gsub("^handler%.", ""),
                    text = prompt_data.text,
                    order = prompt_data.order or 100,
                })
            end
        end
    end

    table.sort(handlers, function(a, b) return a.order < b.order end)
    return handlers
end

-- ══════════════════════════════════════
-- Mask 配置收集
-- ══════════════════════════════════════

function collect_mask_tags(handlers)
    local tags = {}
    for _, h in ipairs(handlers) do
        local mask_raw = alice.service.call(h.service, "mask", "{}")
        local mask_data = alice.json.decode(mask_raw)
        if mask_data and mask_data.block_tags then
            for _, tag in ipairs(mask_data.block_tags) do
                table.insert(tags, {
                    start = tag.start,
                    close = tag.close,
                    placeholder = tag.placeholder or "[...]",
                })
            end
        end
    end
    return tags
end

-- ══════════════════════════════════════
-- System Prompt 组装
-- ══════════════════════════════════════

function build_system_prompt(handlers)
    local parts = { BASE_PROMPT }

    if #handlers > 0 then
        table.insert(parts, "\n你可以使用以下工具:\n")
        for _, h in ipairs(handlers) do
            table.insert(parts, h.text)
            table.insert(parts, "")
        end
        table.insert(parts, "重要规则:")
        table.insert(parts, "- 每次只调用必要的工具，避免不必要的操作")
        table.insert(parts, "- 执行失败时分析原因，尝试不同方案")
        table.insert(parts, "- 工具调用的结果会自动反馈给你，你可以基于结果继续推理")
    end

    return table.concat(parts, "\n")
end

-- ══════════════════════════════════════
-- 历史管理
-- ══════════════════════════════════════

function load_history(session_id)
    local raw = alice.kv.get("session:" .. session_id)
    if raw then return alice.json.decode(raw) or {} end
    return {}
end

function save_history(session_id, history)
    while #history > 40 do
        table.remove(history, 1)
    end
    alice.kv.set("session:" .. session_id, alice.json.encode(history))
end

-- ══════════════════════════════════════
-- AI 调用 (同步 / 流式)
-- ══════════════════════════════════════

function call_ai(args, messages)
    local ai_args = { messages = messages }
    if args.provider then ai_args.provider = args.provider end

    local ai_result_raw = alice.service.call("ai.chat", "chat", alice.json.encode(ai_args))
    local ai_result = alice.json.decode(ai_result_raw)

    if not ai_result then return { error = "AI 调用失败: 无响应" } end
    if ai_result.error then return { error = ai_result.error } end
    return { reply = ai_result.reply or "", model = ai_result.model or "unknown" }
end

function call_ai_stream(args, messages, session_id)
    local ai_args = { messages = messages, session_id = session_id }
    if args.provider then ai_args.provider = args.provider end

    local ai_result_raw = alice.service.call("ai.chat", "stream_chat", alice.json.encode(ai_args))
    local ai_result = alice.json.decode(ai_result_raw)

    if not ai_result then return { error = "AI 调用失败: 无响应" } end
    if ai_result.error then return { error = ai_result.error } end
    return { reply = ai_result.reply or "", model = ai_result.model or "unknown" }
end

-- ══════════════════════════════════════
-- 核心: Regen Loop
-- ══════════════════════════════════════

function do_send(args_json)
    local args = alice.json.decode(args_json)
    if not args or not args.message then
        return alice.json.encode({ error = "缺少 message" })
    end

    local session_id = args.session_id or "default"
    local message = args.message
    local use_stream = args.stream ~= false

    -- 1. 加载历史
    local history = load_history(session_id)

    -- 2. 发现 handlers，组装 system prompt
    local handlers = discover_handlers()
    local system_prompt = build_system_prompt(handlers)

    -- 3. 收集 mask 标签，创建 masker
    local mask_tags = collect_mask_tags(handlers)
    local masker = create_masker(mask_tags)

    -- 4. 添加用户消息到历史
    table.insert(history, { role = "user", content = message })

    -- 5. Regen Loop
    local additional_logs = {}
    local final_reply = ""
    local final_model = "unknown"
    local iteration = 0
    local cancelled = false

    local cancel_handle = alice.event.on("chat.cancel", function(data_json)
        local data = alice.json.decode(data_json)
        if data and data.session_id == session_id then cancelled = true end
    end)

    while iteration < MAX_REGEN do
        if cancelled then break end

        -- 5a. 组装 messages
        local messages = {}
        table.insert(messages, { role = "system", content = system_prompt })
        for _, entry in ipairs(history) do
            table.insert(messages, entry)
        end
        for _, log in ipairs(additional_logs) do
            table.insert(messages, log)
        end

        -- 5b. 调用 LLM
        if use_stream then
            -- 流式: 监听 token 事件，应用 masking，广播到 WS
            masker = create_masker(mask_tags)

            local token_handle = alice.event.on("chat.token", function(data_json)
                local data = alice.json.decode(data_json)
                if not data or data.session_id ~= session_id then return end
                if data.done then return end

                local masked = masker:process(data.token)
                if masked and #masked > 0 then
                    alice.ws.broadcast(session_id, {
                        type = "chat.token",
                        content = masked,
                        iteration = iteration,
                    })
                end
            end)

            local ai_result = call_ai_stream(args, messages, session_id)
            alice.event.off(token_handle)

            -- flush masker 剩余缓冲
            local remaining = masker:flush()
            if #remaining > 0 then
                alice.ws.broadcast(session_id, {
                    type = "chat.token",
                    content = remaining,
                    iteration = iteration,
                })
            end

            if ai_result.error then
                alice.event.off(cancel_handle)
                return alice.json.encode({ error = ai_result.error })
            end
            final_reply = ai_result.reply
            final_model = ai_result.model
        else
            -- 非流式
            local ai_result = call_ai(args, messages)
            if ai_result.error then
                alice.event.off(cancel_handle)
                return alice.json.encode({ error = ai_result.error })
            end
            final_reply = ai_result.reply
            final_model = ai_result.model
        end

        -- 5c. 遍历 handlers
        local need_regen = false
        if #handlers > 0 then
            for _, handler in ipairs(handlers) do
                if cancelled then break end
                local handle_result_raw = alice.service.call(handler.service, "handle",
                    alice.json.encode({
                        content = final_reply,
                        session_id = session_id,
                        iteration = iteration,
                    }))
                local hr = alice.json.decode(handle_result_raw)
                if hr and hr.regen then
                    need_regen = true
                    for _, log in ipairs(hr.logs or {}) do
                        table.insert(additional_logs, log)
                    end
                    alice.log.info("[chat-pipeline] " .. handler.name .. " 触发 regen (iteration=" .. iteration .. ")")
                end
            end
        end

        -- 5d. 通知前端 regen 状态
        if need_regen and use_stream then
            alice.ws.broadcast(session_id, {
                type = "chat.regen",
                iteration = iteration + 1,
            })
        end

        if not need_regen then break end
        iteration = iteration + 1
    end

    alice.event.off(cancel_handle)

    if iteration >= MAX_REGEN then
        alice.log.warn("[chat-pipeline] 达到最大迭代次数 " .. MAX_REGEN)
    end

    -- 6. 通知流式结束
    if use_stream then
        alice.ws.broadcast(session_id, {
            type = "chat.done",
            model = final_model,
            iterations = iteration,
        })
    end

    -- 7. 保存最终结果到历史
    table.insert(history, { role = "assistant", content = final_reply })
    save_history(session_id, history)

    return alice.json.encode({
        reply = final_reply,
        model = final_model,
        session_id = session_id,
        iterations = iteration,
    })
end

-- ══════════════════════════════════════
-- 入口
-- ══════════════════════════════════════

function onLoad()
    alice.service.register("chat.send", function(method, args_json)
        if method == "send" then return do_send(args_json) end
        return alice.json.encode({ error = "未知方法: " .. method })
    end)
    alice.log.info("Chat Pipeline 已注册 (Regen Loop + Streaming, max=" .. MAX_REGEN .. ")")
end

function onUnload()
    alice.log.info("Chat Pipeline 已卸载")
end
