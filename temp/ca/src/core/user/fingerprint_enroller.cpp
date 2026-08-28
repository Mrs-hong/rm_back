//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <chrono>
#include <cstdint>
#include <thread>

#include "qifeng_framework/common/logger.h"
#include "workflow/WFTaskFactory.h"

#include "common/config/fingerprint_config.h"
#include "common/fingerprint_enums.h"
#include "common/timer_manager.h"
#include "common/ws/realtime_dispatch_manager.h"
#include "core/user/fingerprint_enroller.h"
#include "dao_managers/user_dao_manager.h"
#include "internal/display_manager.h"
#include "internal/hal/hal_bridge.h"
#include "json/writer.h"

namespace qifeng_ca {

    namespace {

        std::string_view EnrollResultMessage(FingerprintResult ret) {
            switch (ret) {
                case FingerprintResult::Timeout:
                    return FingerprintMsg::Timeout;
                case FingerprintResult::StorageFull:
                    return FingerprintMsg::StorageFull;
                case FingerprintResult::EnrollFailed:
                    return FingerprintMsg::EnrollFailed;
                case FingerprintResult::DuplicateFinger:
                    return FingerprintMsg::DuplicateFinger;
                case FingerprintResult::CaptureFail:
                    return FingerprintMsg::CaptureFail;
                case FingerprintResult::IdentifyFailed:
                    return FingerprintMsg::IdentifyFailed;
                case FingerprintResult::NotFound:
                    return FingerprintMsg::NotFound;
                case FingerprintResult::SmallContactArea:
                    return FingerprintMsg::SmallContactArea;
                case FingerprintResult::PoorImageQuality:
                case FingerprintResult::MoveTooMuch:
                case FingerprintResult::MoveTooLittle:
                    return FingerprintMsg::PoorImageQuality;
                default:
                    return FingerprintMsg::SystemBusy;
            }
        }

        void RestoreFingerprintLed() {
            FingerprintBridge::GetInstance().SetLed(qifeng::FingerprintLedState::Red,
                                                    FingerprintConfig::GetInstance().GetEnrollTimeoutMs());
        }

        std::string DuplicateFingerMessage() {
            auto &fp = FingerprintBridge::GetInstance();
            MatchResult matchResult;
            auto verifyRet = fp.Verify(matchResult, FingerprintConfig::GetInstance().GetEnrollTimeoutMs());
            if (verifyRet == FingerprintResult::OK && matchResult.mMatched) {
                models::User existUser = UserDaoManager::GetInstance().GetByFingerprintId(matchResult.mFingerId);
                if (existUser.mAccountId != 0) {
                    return std::string(FingerprintMsg::DuplicateFingerPrefix) + existUser.mUserName +
                           std::string(FingerprintMsg::DuplicateFingerSuffix);
                }
            }
            return std::string(FingerprintMsg::DuplicateFinger);
        }

        // 指纹录入WS推送事件数据
        struct EnrollEvent {
            std::string event;
            bool success {false};
            bool finished {false};  // 是否录制结束
            uint8_t successCount {0};
            std::string message;
        };

        // 构造WS推送消息JSON并广播给所有指纹WS客户端
        void BroadcastEnrollEvent(const EnrollEvent &evt) {
            Json::Value root;
            root["event"] = evt.event;
            root["success"] = evt.success;
            root["finished"] = evt.finished;
            root["success_count"] = evt.successCount;
            root["required_count"] = FingerprintConfig::GetInstance().GetRequiredSuccesses();
            root["message"] = evt.message;
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            std::string msg = Json::writeString(builder, root);
            RealtimeDispatchManager::GetInstance().Distribute(msg, EClientType::kFingerprint);
        }

        // 更新指纹录入显示屏(进度1~7, 提示, 结果)
        void UpdateFingerprintDisplay(uint8_t successCount, const std::string &tip, uint16_t result = 1) {
            FingerprintDisplayData data;
            data.mRatioLevel = static_cast<uint16_t>(successCount + 1);
            if (data.mRatioLevel > 7) {
                data.mRatioLevel = 7;
            }
            data.mTip = tip;
            data.mResult = result;
            DisplayManager::GetInstance().UpdateFingerprintPage(data);
        }
    }  // namespace

    FingerprintEnroller &FingerprintEnroller::GetInstance() {
        static FingerprintEnroller Instance;
        return Instance;
    }

    Status FingerprintEnroller::StartEnroll(uint64_t accountId, uint16_t oldFingerId, const std::string &accountName) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mSessions.empty()) {
            return Status {-1, "已有指纹录入进行中,请先取消"};
        }
        // 覆盖录入(oldFingerId!=0)不占用新槽位, 跳过容量检查
        if (oldFingerId == 0) {
            constexpr uint16_t maxFingerprintCapacity = 100;
            auto enrolled = UserDaoManager::GetInstance().CountFingerprintEnrolled();
            if (enrolled > maxFingerprintCapacity) {
                return Status {-1, std::string(FingerprintMsg::StorageFull)};
            }
        }
        // 切换显示屏到指纹录入页面并显示初始状态
        if (!DisplayManager::GetInstance().SwitchToFingerprint()) {
            // 异常退出: 直接清理上下文
            mSessions.erase(accountId);
            CancelUnsafe(accountId);
            return Status {-1, "已有会议进行中, 无法录入指纹"};
        }

        // 防御性取消上一次录入结束遗留的切页定时器, 避免在新录入期间误切到Idle
        TimerManager::GetInstance().CancelByName(TimerName::FingerprintPageSwitchTimer);
        if (FingerprintBridge::GetInstance().SetLed(qifeng::FingerprintLedState::BlueBreath,
                                                    FingerprintConfig::GetInstance().GetCancelTimeoutMs()) !=
            FingerprintResult::OK) {
            return Status {-1, std::string(FingerprintMsg::FingerprintOffline)};
        }

        auto session = std::make_shared<EnrollSession>();
        session->mAccountId = accountId;
        session->mOldFingerId = oldFingerId;
        session->mAccountName = accountName;
        mSessions[accountId] = session;

        UpdateFingerprintDisplay(0, std::string(FingerprintMsg::SmallContactArea));

        // 启动超时定时器: 超时自动取消录
        auto sessionTimeoutSec = FingerprintConfig::GetInstance().GetEnrollSessionTimeoutSec();
        auto* timeoutTask = WFTaskFactory::create_timer_task(
            std::string(TimerName::FingerprintEnrollTimeoutTimer), sessionTimeoutSec, 0,
            [session, sessionTimeoutSec](WFTimerTask* task) {
                if (task->get_state()) {
                    return;
                }
                if (session->mCancelled.load(std::memory_order_acquire)) {
                    return;
                }
                SLOG_WARN << "Fingerprint enroll timeout after " << sessionTimeoutSec
                          << "s, auto-cancelling, account=" << session->mAccountId;
                session->mCancelled.store(true, std::memory_order_release);
                FingerprintBridge::GetInstance().Cancel(FingerprintConfig::GetInstance().GetCancelTimeoutMs());
            });
        timeoutTask->start();

        std::thread(&FingerprintEnroller::EnrollWorker, this, session).detach();
        SLOG_INFO << "Fingerprint enroll started, account=" << accountId;
        return Status {};
    }

    Status FingerprintEnroller::CancelUnsafe(uint64_t accountId) {
        auto it = mSessions.find(accountId);
        if (it == mSessions.end()) {
            return Status {-1, "无进行中的指纹录入"};
        }

        it->second->mCancelled.store(true, std::memory_order_release);
        FingerprintBridge::GetInstance().Cancel(FingerprintConfig::GetInstance().GetCancelTimeoutMs());
        SLOG_INFO << "Fingerprint enroll cancelled, account=" << accountId;
        return Status {};
    }

    Status FingerprintEnroller::Cancel(uint64_t accountId) {
        std::lock_guard<std::mutex> lock(mMutex);
        return CancelUnsafe(accountId);
    }

    bool FingerprintEnroller::IsEnrolling(uint64_t accountId) const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mSessions.contains(accountId);
    }

    void FingerprintEnroller::EnrollWorker(EnrollSessionPtr session) {
        SLOG_INFO << "Fingerprint enroll worker started, account=" << session->mAccountId;
        while (!session->mCancelled.load(std::memory_order_acquire) &&
               session->mSuccessCount < FingerprintConfig::GetInstance().GetRequiredSuccesses()) {
            session->mTotalCount++;
            uint8_t regIdx = static_cast<uint8_t>(session->mSuccessCount + 1);
            EnrollProgress progress;
            auto ret = FingerprintBridge::GetInstance().EnrollCapture(
                regIdx, progress, FingerprintConfig::GetInstance().GetEnrollTimeoutMs());
            if (progress.mFingerId != 0) {
                session->mFingerId = progress.mFingerId;
            }
            SLOG_INFO << "Fingerprint enroll capture, account=" << session->mAccountId
                      << ", regIdx=" << static_cast<int>(regIdx)
                      << ", progress=" << static_cast<int>(progress.mProgress) << ", completed=" << progress.mCompleted
                      << ", fingerId=" << static_cast<int>(progress.mFingerId);

            if (session->mCancelled.load(std::memory_order_acquire)) {
                break;
            }
            if (HandleCaptureResult(session, ret)) {
                break;
            }
        }

        if (session->mCancelled.load(std::memory_order_acquire)) {
            BroadcastEnrollEvent(
                {"cancelled", false, true, session->mSuccessCount, std::string(FingerprintMsg::EnrollCancelled)});
            UpdateFingerprintDisplay(session->mSuccessCount, std::string(FingerprintMsg::EnrollCancelled),
                                     3);  // 失败/取消
        }
        FingerprintBridge::GetInstance().Cancel(FingerprintConfig::GetInstance().GetCancelTimeoutMs());
        CleanupSession(session->mAccountId);
        SLOG_INFO << "Fingerprint enroll worker finished, account=" << session->mAccountId;
    }

    bool FingerprintEnroller::HandleCaptureResult(EnrollSessionPtr session, FingerprintResult ret) {
        if (ret == FingerprintResult::OK) {
            session->mSuccessCount++;
            if (session->mSuccessCount >= FingerprintConfig::GetInstance().GetRequiredSuccesses()) {
                return !FinalizeEnroll(session);
            }
            BroadcastEnrollEvent(
                {"capture", true, false, session->mSuccessCount, std::string(FingerprintMsg::CaptureSuccess)});
            UpdateFingerprintDisplay(session->mSuccessCount, std::string(FingerprintMsg::CaptureSuccess));
            return false;
        }
        if (ret == FingerprintResult::DuplicateFinger) {
            std::string msg = DuplicateFingerMessage();
            BroadcastEnrollEvent({"failed", false, false, session->mSuccessCount, msg});
            UpdateFingerprintDisplay(session->mSuccessCount, msg);  // 失败
        } else {
            std::string msg {EnrollResultMessage(ret)};
            SLOG_WARN << "Fingerprint enroll capture failed, account=" << session->mAccountId
                      << ", ret=" << static_cast<int>(ret) << ", msg=" << msg;
            BroadcastEnrollEvent({"capture", false, false, session->mSuccessCount, msg});
            UpdateFingerprintDisplay(session->mSuccessCount, msg);
        }

        session->mFailCount++;

        if (session->mFailCount >= FingerprintConfig::GetInstance().GetMaxFailures() ||
            session->mTotalCount >= FingerprintConfig::GetInstance().GetMaxTotalCalls()) {
            std::string msg(FingerprintMsg::MaxFailuresExceeded);
            BroadcastEnrollEvent({"failed", false, true, session->mSuccessCount, msg});
            UpdateFingerprintDisplay(session->mSuccessCount, msg, 3);  // 失败
            return true;
        }
        return false;
    }

    // 注意：指纹覆盖存在一种可能性，即用户覆盖指纹时最终就指纹一直删除失败则会占用指纹模组一个槽位
    bool FingerprintEnroller::FinalizeEnroll(EnrollSessionPtr session) {
        if (session->mFingerId == 0) {
            SLOG_DEBUG << "Fingerprint enroll finalize, account=" << session->mAccountId << ", fingerId=0";
            return false;
        }
        // 覆盖写入: 先删除旧指纹模板(若存在), 再保存新模板
        SLOG_DEBUG << "Fingerprint enroll fingerId=" << session->mFingerId << ", OldFingerId=" << session->mOldFingerId;

        SaveResult saveResult;
        auto saveRet = FingerprintBridge::GetInstance().SaveTemplate(
            session->mFingerId, saveResult, FingerprintConfig::GetInstance().GetEnrollTimeoutMs());
        if (saveRet != FingerprintResult::OK) {
            SLOG_ERROR << "Fingerprint enroll finalize, account=" << session->mAccountId
                       << ", save fingerId=" << session->mFingerId << " failed, ret=" << static_cast<int>(saveRet);
            BroadcastEnrollEvent(
                {"failed", false, true, session->mSuccessCount, std::string {EnrollResultMessage(saveRet)}});
            return false;
        }

        if (!UserDaoManager::GetInstance().UpdateFingerprintId(session->mAccountId, session->mFingerId)) {
            FingerprintBridge::GetInstance().Delete(session->mFingerId,
                                                    FingerprintConfig::GetInstance().GetEnrollTimeoutMs());
            SLOG_ERROR << "Fingerprint enroll finalize, account=" << session->mAccountId
                       << ", update fingerId=" << session->mFingerId << " failed";
            BroadcastEnrollEvent(
                {"failed", false, true, session->mSuccessCount, std::string(FingerprintMsg::SystemBusy)});
            return false;
        }
        // 删除旧指纹模板(已写入新模板, 删除失败不影响录入成功, 仅记录日志)
        // Bridge内部已带重试且NotFound视为幂等成功
        if (session->mOldFingerId != 0) {
            auto delRet = FingerprintBridge::GetInstance().Delete(
                session->mOldFingerId, FingerprintConfig::GetInstance().GetEnrollTimeoutMs());
            if (delRet != FingerprintResult::OK) {
                SLOG_ERROR << "Fingerprint enroll finalize, account=" << session->mAccountId
                           << ", delete old fingerId=" << session->mOldFingerId
                           << " failed, ret=" << static_cast<int>(delRet) << ", resource leaked";
            }
        }
        BroadcastEnrollEvent(
            {"complete", true, true, session->mSuccessCount, std::string(FingerprintMsg::EnrollComplete)});
        UpdateFingerprintDisplay(session->mSuccessCount, std::string(FingerprintMsg::EnrollComplete), 2);  // 成功
        SLOG_INFO << "Fingerprint enrolled for user: " << session->mAccountName << ", fingerId=" << session->mFingerId;
        CleanupSession(session->mAccountId);
        return true;
    }

    void FingerprintEnroller::CleanupSession(uint64_t accountId) {
        std::lock_guard<std::mutex> lock(mMutex);
        mSessions.erase(accountId);
        RestoreFingerprintLed();
        TimerManager::GetInstance().CancelByName(TimerName::FingerprintEnrollTimeoutTimer);
        // 延时2秒后切回空闲页面, 让用户看到录入结果(使用workflow命名定时器, 支持优雅退出时取消)
        constexpr time_t kDelaySec = 2;
        auto* timerTask = WFTaskFactory::create_timer_task(
            std::string(TimerName::FingerprintPageSwitchTimer), kDelaySec, 0, [](WFTimerTask* task) {
                if (task->get_state()) {
                    SLOG_ERROR << "FingerprintPageSwitchTimer: timer callback, state: " << task->get_state()
                               << ", error: " << task->get_error();
                    return;
                }
                DisplayManager::GetInstance().SwitchToIdle();
            });
        timerTask->start();
    }

}  // namespace qifeng_ca
