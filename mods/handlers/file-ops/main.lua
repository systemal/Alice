-- Handler: File Operations
-- LLM 通过 <view-file>, <write-file>, <replace-in-file> 标签操作文件

function onLoad()
    alice.service.register("handler.file-ops", function(method, args_json)
        if method == "prompt" then return get_prompt()
        elseif method == "handle" then return handle_reply(args_json)
        elseif method == "mask" then return get_mask()
        end
        return alice.json.encode({ error = "unknown method" })
    end)
    alice.log.info("Handler 已注册: file-ops")
end

function get_prompt()
    return alice.json.encode({
        order = 50,
        text = [[你可以通过以下标签操作文件:

查看文件:
<view-file>
文件路径 (每行一个)
</view-file>

写入/覆写文件:
<write-file path="文件路径">
文件内容
</write-file>

差量替换 (支持多组):
<replace-in-file path="文件路径">
<search>要查找的文本</search>
<replace>替换为的文本</replace>
</replace-in-file>

查看文件后你会收到文件内容。写入/替换后你会收到操作结果。
路径支持相对路径和绝对路径。]]
    })
end

function get_mask()
    return alice.json.encode({
        block_tags = {
            { start = "<view-file>", close = "</view-file>", placeholder = "[读取文件中...]" },
            { start = "<write-file", close = "</write-file>", placeholder = "[写入文件中...]" },
            { start = "<replace-in-file", close = "</replace-in-file>", placeholder = "[替换文件中...]" },
        }
    })
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

    -- 处理 <view-file>
    local search_start = 1
    while true do
        local s, e, paths_str = content:find("<view%-file>(.-)</view%-file>", search_start)
        if not s then break end
        has_action = true

        local results = {}
        for line in paths_str:gmatch("[^\r\n]+") do
            local fpath = line:match("^%s*(.-)%s*$")
            if #fpath > 0 then
                local file_content = alice.fs.read(fpath)
                if file_content then
                    table.insert(results, "=== " .. fpath .. " ===\n" .. file_content)
                else
                    table.insert(results, "=== " .. fpath .. " === (文件不存在或无法读取)")
                end
            end
        end

        if #results > 0 then
            table.insert(logs, {
                role = "tool",
                name = "file-ops",
                content = table.concat(results, "\n\n")
            })
            regen = true
        end

        search_start = e + 1
    end

    -- 处理 <write-file path="...">
    search_start = 1
    while true do
        local s, e, fpath, file_content = content:find('<write%-file%s+path="([^"]+)">(.-)</write%-file>', search_start)
        if not s then break end
        has_action = true

        local ok = alice.fs.write(fpath, file_content)
        if ok then
            table.insert(logs, {
                role = "tool",
                name = "file-ops",
                content = "文件已写入: " .. fpath .. " (" .. tostring(#file_content) .. " 字节)"
            })
        else
            table.insert(logs, {
                role = "tool",
                name = "file-ops",
                content = "文件写入失败: " .. fpath
            })
        end
        regen = true
        search_start = e + 1
    end

    -- 处理 <replace-in-file path="...">
    search_start = 1
    while true do
        local s, e, fpath, inner = content:find('<replace%-in%-file%s+path="([^"]+)">(.-)</replace%-in%-file>', search_start)
        if not s then break end
        has_action = true

        local file_content = alice.fs.read(fpath)
        if not file_content then
            table.insert(logs, {
                role = "tool",
                name = "file-ops",
                content = "替换失败: " .. fpath .. " (文件不存在)"
            })
            regen = true
            search_start = e + 1
            goto continue_replace
        end

        local replace_count = 0
        for search_text, replace_text in inner:gmatch("<search>(.-)</search>%s*<replace>(.-)</replace>") do
            local new_content, n = file_content:gsub(search_text, replace_text, 1)
            if n > 0 then
                file_content = new_content
                replace_count = replace_count + n
            end
        end

        if replace_count > 0 then
            local ok = alice.fs.write(fpath, file_content)
            if ok then
                table.insert(logs, {
                    role = "tool",
                    name = "file-ops",
                    content = "文件已替换: " .. fpath .. " (" .. replace_count .. " 处)"
                })
            else
                table.insert(logs, {
                    role = "tool",
                    name = "file-ops",
                    content = "替换写回失败: " .. fpath
                })
            end
        else
            table.insert(logs, {
                role = "tool",
                name = "file-ops",
                content = "替换: " .. fpath .. " (未找到匹配项)"
            })
        end
        regen = true
        search_start = e + 1

        ::continue_replace::
    end

    if has_action then
        table.insert(logs, 1, {
            role = "assistant",
            name = "file-ops",
            content = content
        })
    end

    return alice.json.encode({ regen = regen, logs = logs })
end

function onUnload()
    alice.log.info("Handler 已卸载: file-ops")
end
