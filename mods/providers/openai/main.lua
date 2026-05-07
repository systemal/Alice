-- OpenAI 兼容协议插件
-- 覆盖: OpenAI, DeepSeek, Ollama, 月之暗面, 通义千问, OpenRouter

function onLoad()
    alice.service.register("ai.protocol.openai", function(method, args_json)
        if method == "chat" then return do_chat(args_json)
        elseif method == "stream_chat" then return do_stream_chat(args_json)
        end
        return alice.json.encode({ error = "未知方法" })
    end)
    alice.log.info("OpenAI 协议插件已注册")
end

function build_request(args)
    local cfg = args.config or {}
    local messages = args.messages or {}
    local base_url = cfg.base_url or "https://api.openai.com/v1"
    local model = cfg.model or "gpt-4o-mini"

    local oai_messages = {}
    for _, msg in ipairs(messages) do
        table.insert(oai_messages, { role = msg.role, content = msg.content })
    end

    local headers = { ["Content-Type"] = "application/json" }
    if cfg.api_key and cfg.api_key ~= "" then
        headers["Authorization"] = "Bearer " .. cfg.api_key
    end

    return {
        url = base_url .. "/chat/completions",
        model = model,
        messages = oai_messages,
        headers = headers,
    }
end

function do_chat(args_json)
    local args = alice.json.decode(args_json)
    local req = build_request(args)

    local body = alice.json.encode({
        model = req.model,
        messages = req.messages,
        stream = false,
    })

    local resp_raw = alice.net.fetch(req.url, alice.json.encode({
        method = "POST",
        headers = req.headers,
        body = body,
    }))

    if not resp_raw then return alice.json.encode({ error = "HTTP 请求失败" }) end

    local resp = alice.json.decode(resp_raw)
    if not resp or resp.status ~= 200 then
        return alice.json.encode({ error = "OpenAI 错误 " .. tostring(resp and resp.status or "nil") })
    end

    local resp_body = alice.json.decode(resp.body)
    if not resp_body or not resp_body.choices then
        return alice.json.encode({ error = "响应格式错误" })
    end

    local reply = resp_body.choices[1] and resp_body.choices[1].message
        and resp_body.choices[1].message.content or ""

    return alice.json.encode({ reply = reply, model = req.model })
end

function do_stream_chat(args_json)
    local args = alice.json.decode(args_json)
    local req = build_request(args)
    local session_id = args.session_id or ""

    local body = alice.json.encode({
        model = req.model,
        messages = req.messages,
        stream = true,
    })

    local full_reply = ""

    alice.net.fetch_stream(req.url, alice.json.encode({
        method = "POST",
        headers = req.headers,
        body = body,
    }), function(event_type, event_data)
        if event_data == "[DONE]" then return false end

        local ok, chunk = pcall(alice.json.decode, event_data)
        if not ok or not chunk then return true end

        local delta = chunk.choices and chunk.choices[1] and chunk.choices[1].delta
        local token = delta and delta.content or ""

        if #token > 0 then
            full_reply = full_reply .. token
            alice.event.emit("chat.token", {
                session_id = session_id,
                token = token,
            })
        end
        return true
    end)

    alice.event.emit("chat.token", { session_id = session_id, token = "", done = true })
    return alice.json.encode({ reply = full_reply, model = req.model })
end

function onUnload() alice.log.info("OpenAI 协议插件已卸载") end
