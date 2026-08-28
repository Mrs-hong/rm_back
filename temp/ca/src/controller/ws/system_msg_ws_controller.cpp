//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <string>

#include "qifeng_framework/common/logger.h"

#include "common/common.h"
#include "common/config/ws_config.h"
#include "controller/ws/system_msg_ws_controller.h"
#include "core/upgrade/upgrade_realtime.h"

namespace qifeng_ca {

    static std::string ExtractClientIdFromPath(const drogon::HttpRequestPtr &req) {
        std::string clientId = req->getParameter("client_id");
        if (!clientId.empty()) {
            return clientId;
        }
        auto path = req->path();
        auto pos = path.rfind('/');
        if (pos != std::string::npos) {
            return path.substr(pos + 1);
        }
        return {};
    }

    void SystemMsgWsController::handleNewConnection(const drogon::HttpRequestPtr &req,
                                                    const drogon::WebSocketConnectionPtr &conn) {
        std::string clientId = ExtractClientIdFromPath(req);
        if (clientId.empty()) {
            SLOG_WARN << "SystemMsgWs: client_id is empty, closing connection";
            conn->shutdown();
            return;
        }

        // 解析 accountId 并查询用户组, 用于连接数限制
        AccountInfo accountInfo = ResolveAccountInfo(clientId);
        if (accountInfo.mAccountId == 0) {
            SLOG_WARN << "SystemMsgWs: invalid client_id or user not found: " << clientId;
            conn->shutdown();
            return;
        }

        // 非访客: 同 clientId 已有旧连接时发送重登录通知并踢掉旧连接(单用户单连接)
        // 访客: 允许多个并发连接, 由 AddClient 内部按 FIFO 淘汰超限旧连接
        std::vector<drogon::WebSocketConnectionPtr> oldConns;
        if (accountInfo.mGroupId != Authority::GUEST) {
            oldConns = RealtimeDispatchManager::GetInstance().DelClient(clientId);
            if (!oldConns.empty()) {
                SLOG_INFO << "SystemMsgWs: kicking old connection for client: " << clientId;
                SystemMessageNotifier::GetInstance().SendUserRelogin(clientId);
            }
        }

        RealtimeDispatchManager::GetInstance().AddClient(clientId, EClientType::kMessage, conn, accountInfo);

        conn->setContext(std::make_shared<std::string>(clientId));
        int32_t pingInterval = WsConfig::GetInstance().GetPingIntervalSeconds();
        conn->setPingMessage("", std::chrono::seconds(pingInterval));
        SLOG_INFO << "SystemMsgWs: client " << clientId << " connected";

        // 关闭被替换的旧连接
        for (auto &oldConn : oldConns) {
            if (oldConn) {
                SLOG_DEBUG << "SystemMsgWs: closing old connection";
                handleConnectionClosed(oldConn);
                oldConn->shutdown();
                oldConn->forceClose();
            }
        }

        // 发送用户登录通知（含磁盘状态检查）
        SystemMessageNotifier::GetInstance().SendUserLogin(clientId);

        // 通知 OTA 管理器: 连接已建立, 若是管理员且有未通知的已准备包则推送一次
        // (登录前服务已准备好的包, 在管理员登录时补发通知)
        UpgradeRealtimeManager::GetInstance().OnAdminConnected(clientId, accountInfo.mGroupId);
    }

    void SystemMsgWsController::handleNewMessage(const drogon::WebSocketConnectionPtr &conn, std::string &&message,
                                                 const drogon::WebSocketMessageType &type) {
        (void)conn;
        (void)message;
        (void)type;
        // 系统消息订阅只接收消息维持连接，不处理业务逻辑
    }

    void SystemMsgWsController::handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn) {
        if (RealtimeDispatchManager::GetInstance().DelClientByConn(conn)) {
            SLOG_INFO << "SystemMsgWs: connection closed";
            // 通知 OTA 管理器: 连接断开, 若当前无管理员在线则重置通知窗口
            UpgradeRealtimeManager::GetInstance().OnAdminDisconnected();
        }
    }

}  // namespace qifeng_ca
