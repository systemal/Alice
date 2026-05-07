-- Vertex AI CloudFlare Gateway 协议插件

function onLoad()
    alice.service.register("ai.protocol.vertexai-cf", function(method, args_json)
        if method == "chat" then return do_chat(args_json)
        elseif method == "stream_chat" then return do_stream_chat(args_json)
        end
        return alice.json.encode({ error = "未知方法" })
    end)
    alice.log.info("VertexAI-CF 协议插件已注册")
end

function to_contents(messages)
    local contents = {}
    for _, msg in ipairs(messages) do
        if msg.role == "system" then
            table.insert(contents, { role = "user", parts = {{ text = msg.content }} })
            table.insert(contents, { role = "model", parts = {{ text = "明白了。" }} })
        else
            local role = (msg.role == "assistant" or msg.role == "model") and "model" or "user"
            table.insert(contents, { role = role, parts = {{ text = msg.content }} })
        end
    end
    local merged = {}
    for _, c in ipairs(contents) do
        if #merged > 0 and merged[#merged].role == c.role then
            for _, p in ipairs(c.parts) do table.insert(merged[#merged].parts, p) end
        else
            table.insert(merged, c)
        end
    end
    if #merged > 0 and merged[#merged].role == "model" then
        table.insert(merged, { role = "user", parts = {{ text = "请继续。" }} })
    end
    return merged
end

function build_url(cfg, model, streaming)
    local action = streaming and ":streamGenerateContent" or ":generateContent"
    return "https://gateway.ai.cloudflare.com/v1/"
        .. (cfg.cf_account_id or "") .. "/" .. (cfg.cf_gateway_id or "gcp")
        .. "/google-vertex-ai/v1/projects/" .. (cfg.gcp_project_id or "")
        .. "/locations/" .. (cfg.gcp_region or "global")
        .. "/publishers/google/models/" .. model .. action
end

function build_body(contents)
    return alice.json.encode({
        contents = contents,
        safetySettings = {
            { category = "HARM_CATEGORY_HARASSMENT", threshold = "BLOCK_NONE" },
            { category = "HARM_CATEGORY_HATE_SPEECH", threshold = "BLOCK_NONE" },
            { category = "HARM_CATEGORY_SEXUALLY_EXPLICIT", threshold = "BLOCK_NONE" },
            { category = "HARM_CATEGORY_DANGEROUS_CONTENT", threshold = "BLOCK_NONE" },
        },
    })
end

function build_headers(cfg)
    return {
        ["Content-Type"] = "application/json",
        ["cf-aig-authorization"] = "Bearer " .. (cfg.cf_aig_token or ""),
    }
end

function do_chat(args_json)
    local args = alice.json.decode(args_json)
    local cfg = args.config or {}
    local model = cfg.model or "gemini-2.5-flash"

    local url = build_url(cfg, model, false)
    local contents = to_contents(args.messages or {})

    local resp_raw = alice.net.fetch(url, alice.json.encode({
        method = "POST",
        headers = build_headers(cfg),
        body = build_body(contents),
    }))

    if not resp_raw then return alice.json.encode({ error = "HTTP 请求失败" }) end
    local resp = alice.json.decode(resp_raw)
    if not resp or resp.status ~= 200 then
        return alice.json.encode({ error = "VertexAI 错误 " .. tostring(resp and resp.status or "nil") })
    end

    local resp_body = alice.json.decode(resp.body)
    if not resp_body or not resp_body.candidates then
        return alice.json.encode({ error = "响应格式错误" })
    end

    local reply = ""
    local parts = resp_body.candidates[1] and resp_body.candidates[1].content
        and resp_body.candidates[1].content.parts
    if parts then
        for _, part in ipairs(parts) do
            if part.text then reply = reply .. part.text end
        end
    end
    return alice.json.encode({ reply = reply, model = model })
end

function do_stream_chat(args_json)
    local args = alice.json.decode(args_json)
    local cfg = args.config or {}
    local model = cfg.model or "gemini-2.5-flash"
    local session_id = args.session_id or ""

    local url = build_url(cfg, model, true) .. "?alt=sse"
    local contents = to_contents(args.messages or {})

    local full_reply = ""

    alice.net.fetch_stream(url, alice.json.encode({
        method = "POST",
        headers = build_headers(cfg),
        body = build_body(contents),
    }), function(event_type, event_data)
        local ok, chunk = pcall(alice.json.decode, event_data)
        if not ok or not chunk then return true end

        local parts = chunk.candidates and chunk.candidates[1]
            and chunk.candidates[1].content and chunk.candidates[1].content.parts
        if parts then
            for _, part in ipairs(parts) do
                if part.text and #part.text > 0 then
                    full_reply = full_reply .. part.text
                    alice.event.emit("chat.token", {
                        session_id = session_id,
                        token = part.text,
                    })
                end
            end
        end
        return true
    end)

    alice.event.emit("chat.token", { session_id = session_id, token = "", done = true })
    return alice.json.encode({ reply = full_reply, model = model })
end

function onUnload() alice.log.info("VertexAI-CF 协议插件已卸载") end
