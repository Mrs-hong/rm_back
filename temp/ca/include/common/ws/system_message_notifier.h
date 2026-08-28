//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_WS_SYSTEM_MESSAGE_NOTIFIER_H
#define QIFENG_CA_INCLUDE_COMMON_WS_SYSTEM_MESSAGE_NOTIFIER_H

#include <cstdint>
#include <string>

#include "drogon/WebSocketConnection.h"

namespace qifeng_ca {

    class SystemMessageNotifier {
    public:
        static SystemMessageNotifier &GetInstance();

        ~SystemMessageNotifier() = default;

        SystemMessageNotifier(const SystemMessageNotifier &) = delete;
        SystemMessageNotifier &operator=(const SystemMessageNotifier &) = delete;
        SystemMessageNotifier(SystemMessageNotifier &&) = delete;
        SystemMessageNotifier &operator=(SystemMessageNotifier &&) = delete;

        bool SendStartRecording(const std::string &aid);

        bool SendFileMissing(const std::string &aid, const uint64_t &accountId = {});

        bool SendMicroError(const uint64_t &accountId = {});

        bool SendDiskWarning(const uint64_t &accountId = {});

        bool SendDiskError(const uint64_t &accountId = {});

        bool SendRSError(const uint64_t &accountId = {}, bool force = false);

        bool SendAASError(const uint64_t &accountId = {}, bool force = false);

        bool SendLMSError(const uint64_t &accountId = {}, bool force = false);

        bool SendUserRelogin(const std::string &clientId);

        // 直接向指定连接发送重登录通知(messageType=7), 用于访客 FIFO 淘汰旧连接时通知被关闭方
        bool SendUserReloginToConn(const drogon::WebSocketConnectionPtr &conn);

        bool SendUserLogin(const std::string &clientId);

        // 推送升级通知(messageType=9), dataId携带附加信息
        // 仅推送给已建立系统消息 WS 连接的管理员(升级确认仅管理员可触发)
        bool SendUpgradeNotice(const std::string &dataId = {});

        bool SendMeetingTimeOut(const std::string &aid, const uint64_t &accountId = {});

        bool SendPausedTimeOut(const std::string &aid, const uint64_t &accountId = {});

    private:
        SystemMessageNotifier() = default;

        static std::string BuildSystemMessage(int messageType, const std::string &dataId = {});

        bool DistributeOrSend(const std::string &msg, const uint64_t &accountId);

        int64_t mLastNotifyTime = 0;
        static constexpr int32_t NotifyIntervalSeconds = 15;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_WS_SYSTEM_MESSAGE_NOTIFIER_H
