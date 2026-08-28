//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_WS_TRANSCRIBE_WS_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_WS_TRANSCRIBE_WS_CONTROLLER_H

#include <drogon/HttpRequest.h>
#include <drogon/WebSocketConnection.h>
#include <drogon/WebSocketController.h>

#include "common/ws/realtime_dispatch_manager.h"

namespace qifeng_ca {

    // 实时录音转写接口
    class TranscribeWsController final : public drogon::WebSocketController<TranscribeWsController> {
    public:
        WS_PATH_LIST_BEGIN
        // WS_PATH_ADD("/ws/subscribe/{client_id}", drogon::Get);
        WS_PATH_ADD("/ws/subscribe", drogon::Get);
        WS_PATH_LIST_END

        void handleNewConnection(const drogon::HttpRequestPtr &req,
                                 const drogon::WebSocketConnectionPtr &conn) override;

        void handleNewMessage(const drogon::WebSocketConnectionPtr &conn, std::string &&message,
                              const drogon::WebSocketMessageType &type) override;

        void handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn) override;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_WS_TRANSCRIBE_WS_CONTROLLER_H
