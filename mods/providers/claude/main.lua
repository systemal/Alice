-- Claude (Anthropic) 协议插件

function onLoad()
    alice.service.register("ai.protocol.claude", function(method, args_json)
        if method == "chat" then return do_chat(args_json)
        elseif method == "stream_chat" then return do_stream_chat(args_json)
        end
        return alice.json.encode({ error = "未知方法" })
    end)
    alice.log.info("Claude 协议插件已注册")
end

function build_request(args)
    local cfg = args.config or {}
    local messages = args.messages or {}
    local base_url = cfg.base_url or "https://api.anthropic.com"
    local model = cfg.model or "claude-sonnet-4-20250514"
    local max_tokens = cfg.max_tokens or 8192

    local system_prompt = ""
    local claude_messages = {}
    local last_role = ""

    for _, msg in ipairs(messages) do
        if msg.role == "system" then
            if system_prompt ~= "" then system_prompt = system_prompt .. "\n\n" end
            system_prompt = system_prompt .. msg.content
        else
            local role = (msg.role == "assistant") and "assistant" or "user"
            if role == last_role and #claude_messages > 0 then
                local last = claude_messages[#claude_messages]
                last.content = last.content .. "\n\n" .. msg.content
            else
                table.insert(claude_messages, { role = role, content = msg.content })
                last_role = role
            end
        end
    end

    if #claude_messages > 0 and claude_messages[1].role == "assistant" then
        table.insert(claude_messages, 1, { role = "user", content = "[Start]" })
    end
    if #claude_messages > 0 and claude_messages[#claude_messages].role ~= "user" then
        table.insert(claude_messages, { role = "user", content = "[Continue]" })
    end

    local api_messages = {}
    for _, msg in ipairs(claude_messages) do
        table.insert(api_messages, {
            role = msg.role,
            content = {{ type = "text", text = msg.content }},
        })
    end

    local headers = {
        ["Content-Type"] = "application/json",
        ["Authorization"] = "Bearer " .. (cfg.api_key or ""),
        ["x-api-key"] = cfg.api_key or "",
        ["anthropic-version"] = cfg.anthropic_version or "2023-06-01",
    }

    return {
        url = base_url .. "/v1/messages",
        model = model,
        max_tokens = max_tokens,
        messages = api_messages,
        system_prompt = system_prompt,
        headers = headers,
    }
end

function do_chat(args_json)
    local args = alice.json.decode(args_json)
    local req = build_request(args)

    local body_table = {
        model = req.model,
        max_tokens = req.max_tokens,
        messages = req.messages,
        stream = false,
    }
    if req.system_prompt ~= "" then body_table.system = req.system_prompt end

    local resp_raw = alice.net.fetch(req.url, alice.json.encode({
        method = "POST",
        headers = req.headers,
        body = alice.json.encode(body_table),
    }))

    if not resp_raw then return alice.json.encode({ error = "HTTP 请求失败" }) end

    local resp = alice.json.decode(resp_raw)
    if not resp or resp.status ~= 200 then
        return alice.json.encode({ error = "Claude 错误 " .. tostring(resp and resp.status or "nil") .. ": " .. string.sub(resp and resp.body or "", 1, 200) })
    end

    local resp_body = alice.json.decode(resp.body)
    if not resp_body or not resp_body.content then
        return alice.json.encode({ error = "响应格式错误" })
    end

    local reply = ""
    for _, block in ipairs(resp_body.content) do
        if block.type == "text" then reply = reply .. block.text end
    end

    return alice.json.encode({ reply = reply, model = req.model })
end

function do_stream_chat(args_json)
    local args = alice.json.decode(args_json)
    local req = build_request(args)
    local session_id = args.session_id or ""

    local body_table = {
        model = req.model,
        max_tokens = req.max_tokens,
        messages = req.messages,
        stream = true,
    }
    if req.system_prompt ~= "" then body_table.system = req.system_prompt end

    local full_reply = ""

    alice.net.fetch_stream(req.url, alice.json.encode({
        method = "POST",
        headers = req.headers,
        body = alice.json.encode(body_table),
    }), function(event_type, event_data)
        if event_type == "message_stop" then return false end

        local ok, chunk = pcall(alice.json.decode, event_data)
        if not ok or not chunk then return true end

        if chunk.type == "content_block_delta" and chunk.delta then
            local token = chunk.delta.text or ""
            if #token > 0 then
                full_reply = full_reply .. token
                alice.event.emit("chat.token", {
                    session_id = session_id,
                    token = token,
                })
            end
        end
        return true
    end)

    alice.event.emit("chat.token", { session_id = session_id, token = "", done = true })
    return alice.json.encode({ reply = full_reply, model = req.model })
end

function onUnload() alice.log.info("Claude 协议插件已卸载") end
