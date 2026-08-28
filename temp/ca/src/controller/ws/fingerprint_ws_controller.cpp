//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <string>

#include "qifeng_framework/common/logger.h"

#include "common/config/ws_config.h"
#include "controller/ws/fingerprint_ws_controller.h"

namespace qifeng_ca {

    void FingerprintWsController::handleNewConnection(const drogon::HttpRequestPtr &req,
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
            SLOG_WARN << "FingerprintWs: client_id is empty, closing connection";
            conn->shutdown();
            return;
        }

        // 解析 accountId 并查询用户组, 用于连接数限制
        AccountInfo accountInfo = ResolveAccountInfo(clientId);
        if (accountInfo.mAccountId == 0) {
            SLOG_WARN << "FingerprintWs: invalid client_id or user not found: " << clientId;
            conn->shutdown();
            return;
        }

        // 同 client_id 已有连接时先移除旧连接(指纹场景: 同设备仅保留最新连接)
        auto oldConns = RealtimeDispatchManager::GetInstance().DelClient(clientId);

        // AddClient 内部按 FIFO 淘汰超限旧连接(按 clientType 隔离)
        RealtimeDispatchManager::GetInstance().AddClient(clientId, EClientType::kFingerprint, conn, accountInfo);

        conn->setContext(std::make_shared<std::string>(clientId));
        int32_t pingInterval = WsConfig::GetInstance().GetPingIntervalSeconds();
        conn->setPingMessage("", std::chrono::seconds(pingInterval));
        SLOG_INFO << "FingerprintWs: client " << clientId << " connected";

        // 关闭被替换的旧连接(在锁外)
        for (auto &oldConn : oldConns) {
            if (oldConn) {
                oldConn->shutdown();
            }
        }
    }

    void FingerprintWsController::handleNewMessage(const drogon::WebSocketConnectionPtr &conn, std::string &&message,
                                                   const drogon::WebSocketMessageType &type) {
        (void)conn;
        (void)message;
        (void)type;
        // 指纹录入进度推送只接收消息维持连接, 不处理业务逻辑
    }

    void FingerprintWsController::handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn) {
        if (RealtimeDispatchManager::GetInstance().DelClientByConn(conn)) {
            SLOG_INFO << "FingerprintWs: connection closed";
        }
    }

}  // namespace qifeng_ca
