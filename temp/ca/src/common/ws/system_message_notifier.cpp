//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "common/common.h"
#include "common/ws/realtime_dispatch_manager.h"
#include "common/ws/system_message_notifier.h"

namespace qifeng_ca {

    static std::string GetSystemMessageClientId(uint64_t accountId) {
        return std::to_string(accountId) + "_msg";
    }

    SystemMessageNotifier &SystemMessageNotifier::GetInstance() {
        static SystemMessageNotifier Instance;
        return Instance;
    }

    std::string SystemMessageNotifier::BuildSystemMessage(int messageType, const std::string &dataId) {
        // 构建JSON格式的推送消息
        // 格式: {"heart":false,"messageType":N[,"dataId":"xxx"]}
        std::string msg = R"({"heart":false,"messageType":)" + std::to_string(messageType);
        if (!dataId.empty()) {
            msg += R"(,"dataId":")" + dataId + R"("})";
        } else {
            msg += "}";
        }
        return msg;
    }

    bool SystemMessageNotifier::DistributeOrSend(const std::string &msg, const uint64_t &accountId) {
        if (accountId != 0) {
            return RealtimeDispatchManager::GetInstance().SendTo(GetSystemMessageClientId(accountId), msg);
        }
        return RealtimeDispatchManager::GetInstance().Distribute(msg, EClientType::kMessage);
    }

    bool SystemMessageNotifier::SendStartRecording(const std::string &aid) {
        std::string msg = BuildSystemMessage(1, aid);
        return RealtimeDispatchManager::GetInstance().Distribute(msg, EClientType::kMessage);
    }

    bool SystemMessageNotifier::SendFileMissing(const std::string &aid, const uint64_t &accountId) {
        std::string msg = BuildSystemMessage(2, aid);
        SLOG_DEBUG << "SendFileMissing: aid=" << aid << " msg=" << msg;
        return DistributeOrSend(msg, accountId);
    }

    bool SystemMessageNotifier::SendMicroError(const uint64_t &accountId) {
        std::string msg = BuildSystemMessage(3);
        return DistributeOrSend(msg, accountId);
    }

    bool SystemMessageNotifier::SendDiskWarning(const uint64_t &accountId) {
        std::string msg = BuildSystemMessage(4);
        return DistributeOrSend(msg, accountId);
    }

    bool SystemMessageNotifier::SendDiskError(const uint64_t &accountId) {
        std::string msg = BuildSystemMessage(5);
        return DistributeOrSend(msg, accountId);
    }

    bool SystemMessageNotifier::SendRSError(const uint64_t &accountId, bool force) {
        int64_t nowTs = static_cast<int64_t>(GetTimeMs()) / 1000;
        if (!force && (nowTs - mLastNotifyTime) < NotifyIntervalSeconds) {
            return true;
        }
        mLastNotifyTime = nowTs;

        std::string msg = BuildSystemMessage(6);
        SLOG_DEBUG << "SendRSError: " << msg;
        return DistributeOrSend(msg, accountId);
    }

    bool SystemMessageNotifier::SendAASError(const uint64_t &accountId, bool force) {
        int64_t nowTs = static_cast<int64_t>(GetTimeMs()) / 1000;
        if (!force && (nowTs - mLastNotifyTime) < NotifyIntervalSeconds) {
            return true;
        }
        mLastNotifyTime = nowTs;

        std::string msg = BuildSystemMessage(6);
        SLOG_DEBUG << "SendAASError: " << msg;
        return DistributeOrSend(msg, accountId);
    }

    bool SystemMessageNotifier::SendLMSError(const uint64_t &accountId, bool force) {
        int64_t nowTs = static_cast<int64_t>(GetTimeMs()) / 1000;
        if (!force && (nowTs - mLastNotifyTime) < NotifyIntervalSeconds) {
            return true;
        }
        mLastNotifyTime = nowTs;

        std::string msg = BuildSystemMessage(6);
        SLOG_DEBUG << "SendLMSError: " << msg;
        return DistributeOrSend(msg, accountId);
    }

    bool SystemMessageNotifier::SendUserRelogin(const std::string &clientId) {
        std::string msg = BuildSystemMessage(7);
        return RealtimeDispatchManager::GetInstance().SendTo(clientId, msg);
    }

    bool SystemMessageNotifier::SendUserReloginToConn(const drogon::WebSocketConnectionPtr &conn) {
        if (!conn) {
            return false;
        }
        std::string msg = BuildSystemMessage(7);
        conn->send(msg);
        return true;
    }

    bool SystemMessageNotifier::SendUserLogin(const std::string &clientId) {
        SLOG_DEBUG << "SendUserLogin: " << clientId;
        return true;
    }

    bool SystemMessageNotifier::SendMeetingTimeOut(const std::string &aid, const uint64_t &accountId) {
        std::string msg = BuildSystemMessage(8, aid);
        return DistributeOrSend(msg, accountId);
    }

    bool SystemMessageNotifier::SendUpgradeNotice(const std::string &dataId) {
        std::string msg = BuildSystemMessage(9, dataId);
        SLOG_DEBUG << "SendUpgradeNotice: dataId=" << dataId << ", adminOnly=true";
        // 升级通知仅推送给已连接的管理员: 升级确认仅管理员可触发, 避免打扰普通用户/访客
        return RealtimeDispatchManager::GetInstance().DistributeToGroup(msg, EClientType::kMessage,
                                                                        Authority::ADMINISTRATOR);
    }

    bool SystemMessageNotifier::SendPausedTimeOut(const std::string &aid, const uint64_t &accountId) {
        std::string msg = BuildSystemMessage(10, aid);
        SLOG_DEBUG << "SendPausedTimeOut: aid=" << aid << " msg=" << msg;
        return DistributeOrSend(msg, accountId);
    }

}  // namespace qifeng_ca
