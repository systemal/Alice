#pragma once

namespace alice {

    /// <summary>
    /// 注册 Drogon WebSocket 控制器 (路由: /ws).
    /// 协议: 客户端先 {"subscribe":"session_id"} 绑定会话, 然后发 {"type":"xxx","data":{...}}.
    /// 实现在 WsController.cpp, 用 Drogon 宏 WS_PATH_ADD 静态注册.
    /// </summary>
    void RegisterWsController( );

} // namespace alice
