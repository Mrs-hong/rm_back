//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_WS_FINGERPRINT_WS_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_WS_FINGERPRINT_WS_CONTROLLER_H

#include <drogon/HttpRequest.h>
#include <drogon/WebSocketConnection.h>
#include <drogon/WebSocketController.h>

#include "common/ws/realtime_dispatch_manager.h"

namespace qifeng_ca {

    // 指纹录入进度推送接口
    // 前端建立连接后, 调用 /web/user/enrollFingerprint 传入 client_id,
    // 服务端在采集过程中通过此连接推送 {event, success, success_count, required_count, message}
    class FingerprintWsController final : public drogon::WebSocketController<FingerprintWsController> {
    public:
        WS_PATH_LIST_BEGIN
        WS_PATH_ADD("/ws/fingerprint", drogon::Get);
        WS_PATH_LIST_END

        void handleNewConnection(const drogon::HttpRequestPtr &req,
                                 const drogon::WebSocketConnectionPtr &conn) override;

        void handleNewMessage(const drogon::WebSocketConnectionPtr &conn, std::string &&message,
                              const drogon::WebSocketMessageType &type) override;

        void handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn) override;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_WS_FINGERPRINT_WS_CONTROLLER_H
