-- AI Manager: 多 Provider 管理, 读配置, 路由到协议插件

local config = {}

function onLoad()
    local raw = alice.fs.read("data/ai-config.json")
    if raw then
        config = alice.json.decode(raw) or {}
    end

    if not config.providers or not next(config.providers) then
        alice.log.warn("AI Manager: 无 Provider 配置 (data/ai-config.json)")
        config.providers = {}
        config.default = ""
    end

    alice.service.register("ai.chat", function(method, args_json)
        if method == "chat" then return do_chat(args_json)
        elseif method == "stream_chat" then return do_stream_chat(args_json)
        elseif method == "list" then return do_list()
        end
        return alice.json.encode({ error = "未知方法: " .. method })
    end)

    local count = 0
    for id, _ in pairs(config.providers) do count = count + 1 end
    alice.log.info("AI Manager 已注册: " .. count .. " 个 Provider, 默认: " .. (config.default or "无"))
end

function resolve_provider(args)
    local provider_id = args.provider or config.default or ""
    local provider_cfg = config.providers[provider_id]
    if not provider_cfg then
        return nil, "Provider '" .. provider_id .. "' 不存在"
    end
    local protocol_type = provider_cfg.type
    if not protocol_type then
        return nil, "Provider '" .. provider_id .. "' 缺少 type"
    end
    return {
        service = "ai.protocol." .. protocol_type,
        config = provider_cfg,
    }, nil
end

function do_chat(args_json)
    local args = alice.json.decode(args_json)
    if not args or not args.messages then
        return alice.json.encode({ error = "缺少 messages" })
    end

    local provider, err = resolve_provider(args)
    if not provider then return alice.json.encode({ error = err }) end

    return alice.service.call(provider.service, "chat", alice.json.encode({
        config = provider.config,
        messages = args.messages,
    }))
end

function do_stream_chat(args_json)
    local args = alice.json.decode(args_json)
    if not args or not args.messages then
        return alice.json.encode({ error = "缺少 messages" })
    end

    local provider, err = resolve_provider(args)
    if not provider then return alice.json.encode({ error = err }) end

    return alice.service.call(provider.service, "stream_chat", alice.json.encode({
        config = provider.config,
        messages = args.messages,
        session_id = args.session_id or "",
    }))
end

function do_list()
    local list = {}
    for id, cfg in pairs(config.providers) do
        table.insert(list, {
            id = id,
            type = cfg.type,
            model = cfg.model,
            is_default = (id == config.default),
        })
    end
    return alice.json.encode(list)
end

function onUnload()
    alice.log.info("AI Manager 已卸载")
end
