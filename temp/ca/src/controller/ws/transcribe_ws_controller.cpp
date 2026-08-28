//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <string>

#include "qifeng_framework/common/logger.h"

#include "common/config/ws_config.h"
#include "controller/ws/transcribe_ws_controller.h"

namespace qifeng_ca {

    void TranscribeWsController::handleNewConnection(const drogon::HttpRequestPtr &req,
                                                     const drogon::WebSocketConnectionPtr &conn) {
        std::string clientId = req->getParameter("client_id");
        if (clientId.empty()) {
            auto path = req->path();
            auto pos = path.rfind('/');
            if (pos != std::string::npos) {
                clientId = path.substr(pos + 1);
            }
        }

        if (clientId.empty()) {
            SLOG_WARN << "TranscribeWs: client_id is empty, closing connection";
            conn->shutdown();
            return;
        }

        // 解析 accountId 并查询用户组, 用于连接数限制
        AccountInfo accountInfo = ResolveAccountInfo(clientId);
        if (accountInfo.mAccountId == 0) {
            SLOG_WARN << "TranscribeWs: invalid client_id or user not found: " << clientId;
            conn->shutdown();
            return;
        }

        // AddClient 内部按 FIFO 淘汰超限旧连接(按 clientType 隔离)
        RealtimeDispatchManager::GetInstance().AddClient(clientId, EClientType::kTranscribe, conn, accountInfo);

        conn->setContext(std::make_shared<std::string>(clientId));
        int32_t pingInterval = WsConfig::GetInstance().GetPingIntervalSeconds();
        conn->setPingMessage("", std::chrono::seconds(pingInterval));
        SLOG_INFO << "TranscribeWs: client " << clientId << " connected";
    }

    void TranscribeWsController::handleNewMessage(const drogon::WebSocketConnectionPtr &conn, std::string &&message,
                                                  const drogon::WebSocketMessageType &type) {
        (void)conn;
        (void)message;
        (void)type;
        // TODO(yf): 转写订阅只接收消息维持连接，不处理业务逻辑（后续删除）
    }

    void TranscribeWsController::handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn) {
        if (RealtimeDispatchManager::GetInstance().DelClientByConn(conn)) {
            SLOG_INFO << "TranscribeWs: connection closed";
        }
    }

}  // namespace qifeng_ca
