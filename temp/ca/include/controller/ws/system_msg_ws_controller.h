//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_WS_SYSTEM_MSG_WS_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_WS_SYSTEM_MSG_WS_CONTROLLER_H

#include <drogon/HttpRequest.h>
#include <drogon/WebSocketConnection.h>
#include <drogon/WebSocketController.h>

#include "common/ws/realtime_dispatch_manager.h"
#include "common/ws/system_message_notifier.h"

namespace qifeng_ca {

    // 系统消息接口
    class SystemMsgWsController final : public drogon::WebSocketController<SystemMsgWsController> {
    public:
        WS_PATH_LIST_BEGIN
        // WS_PATH_ADD("/ws/subscribeSysMsg/{client_id}", drogon::Get);
        WS_PATH_ADD("/ws/subscribeSysMsg", drogon::Get);
        WS_PATH_LIST_END

        void handleNewConnection(const drogon::HttpRequestPtr &req,
                                 const drogon::WebSocketConnectionPtr &conn) override;

        void handleNewMessage(const drogon::WebSocketConnectionPtr &conn, std::string &&message,
                              const drogon::WebSocketMessageType &type) override;

        void handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn) override;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_WS_SYSTEM_MSG_WS_CONTROLLER_H
