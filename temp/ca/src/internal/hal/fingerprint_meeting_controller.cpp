//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_ca/meeting.pb.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/audio/audio_hal_utils.h"
#include "common/common.h"
#include "common/utils/file_name_generator.h"
#include "core/meeting/meeting_access.h"
#include "core/meeting/recording_service.h"
#include "dao_managers/user_dao_manager.h"
#include "internal/display_manager.h"
#include "internal/hal/fingerprint_meeting_controller.h"
#include "internal/recording_manager.h"

namespace qifeng_ca {
    namespace hal {

        FingerprintMeetingController &FingerprintMeetingController::GetInstance() {
            static FingerprintMeetingController Instance;
            return Instance;
        }

        uint64_t FingerprintMeetingController::ResolveAccountId(uint16_t fingerId, bool matched) const {
            uint64_t accountId = 0;
            if (matched) {
                models::User user = UserDaoManager::GetInstance().GetByFingerprintId(fingerId);
                accountId = user.mAccountId;
            }
            if (accountId == 0) {
                accountId = GetGuestAccountId();
            }
            return accountId;
        }

        void FingerprintMeetingController::HandleFingerprintMatched(uint16_t fingerId, bool matched) {
            uint64_t accountId = ResolveAccountId(fingerId, matched);
            SLOG_DEBUG << "FingerprintMeeting: fingerprint " << fingerId << " bound to user " << accountId;

            auto &recMgr = RecordingManager::GetInstance();
            if (recMgr.IsRecording()) {
                HandleStopMeeting(accountId);
            } else {
                HandleStartMeeting(accountId, fingerId);
            }
        }

        void FingerprintMeetingController::HandleStopMeeting(uint64_t accountId) {
            auto &recMgr = RecordingManager::GetInstance();
            ScopeExit stopLedFunc([this]() { SetupFingerprintLed(); });  // 补丁

            auto [audio, accessStatus] = GetAccessibleAudio(accountId, recMgr.GetActiveAudioId());
            if (!accessStatus.IsSuccess()) {
                if (recMgr.IsRecording()) {
                    ShowPermissionDeniedTip();
                }
                return;
            }
            ResetPermissionDeniedCount();

            // 检查是否有未过期的停止确认弹窗(tip=3), 有则视为二次确认并停止会议
            bool confirmed = (DisplayManager::GetInstance().GetCurrentPopup() == 3);
            if (confirmed) {
                DisplayManager::GetInstance().ClearPopup();
                SLOG_INFO << "FingerprintMeeting: stop confirmed, accountId=" << accountId;
                DisplayManager::GetInstance().RefreshMeetingPage();
                stopLedFunc.Release();
                DoStopMeeting(accountId);
                return;
            }

            // 非当前开会人员: 先显示确认弹窗, 等待10秒内再次指纹确认
            uint64_t activeAccountId = recMgr.GetActiveAccountId();
            if (accountId != activeAccountId) {
                SLOG_INFO << "FingerprintMeeting: non-host stop, show confirm popup, accountId=" << accountId;
                ShowStopConfirmPopup();
                return;
            }

            stopLedFunc.Release();
            DoStopMeeting(accountId);
        }

        void FingerprintMeetingController::HandleStartMeeting(uint64_t accountId, uint16_t fingerId) const {
            AddRecordingRequest req;
            req.set_theme(FileNameGenerator::GenFile("新会议"));
            req.set_recording_time(static_cast<int64_t>(GetTimeMs()));
            req.set_is_rel_time(true);
            req.set_account_id(accountId);
            AddRecordingResponse resp;
            auto status = RecordingService().AddRecording(req, &resp);
            if (status.GetCode() != 0) {
                SLOG_ERROR << "FingerprintMeeting: start meeting failed, msg=" << status.ToString();
            } else {
                SLOG_INFO << "FingerprintRecording: fingerprint " << fingerId << ", accountId=" << accountId
                          << " -> start recording";
            }
        }

        void FingerprintMeetingController::ShowPermissionDeniedTip() {
            constexpr uint64_t denyResetIntervalMs = 10 * 1000;
            constexpr uint64_t tipDisplayMs = 2000;
            uint16_t tip = 0;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                uint64_t now = GetTimeMs();
                if (now > mLastPermissionDeniedTime + denyResetIntervalMs) {
                    mPermissionDeniedCount = 0;
                }
                mPermissionDeniedCount++;
                mLastPermissionDeniedTime = now;
                tip = (mPermissionDeniedCount > 2) ? 4 : 2;
            }
            DisplayManager::GetInstance().ShowPopup(tip, tipDisplayMs);
            DisplayManager::GetInstance().RefreshMeetingPage();
        }

        void FingerprintMeetingController::ResetPermissionDeniedCount() {
            std::lock_guard<std::mutex> lock(mMutex);
            mPermissionDeniedCount = 0;
            mLastPermissionDeniedTime = 0;
        }

        void FingerprintMeetingController::ShowStopConfirmPopup() {
            constexpr uint64_t confirmTimeoutMs = 10L * 1000;
            DisplayManager::GetInstance().ShowPopup(3, confirmTimeoutMs);
            DisplayManager::GetInstance().RefreshMeetingPage();
        }

        void FingerprintMeetingController::DoStopMeeting(uint64_t accountId) {
            ResetPermissionDeniedCount();

            auto &recMgr = RecordingManager::GetInstance();
            if (!recMgr.IsRecording()) {
                return;
            }
            RecordStopRequest req;
            req.set_audio_id(recMgr.GetActiveAudioId());
            req.set_account_id(accountId);
            qifeng_ca::Empty resp;
            auto status = RecordingService().RecordStop(req, &resp);
            if (status.GetCode() != 0) {
                SLOG_ERROR << "FingerprintMeeting: stop meeting failed, msg=" << status.ToString();
            } else {
                SLOG_INFO << "FingerprintRecording: accountId=" << accountId << " -> stop recording";
            }
        }

    }  // namespace hal
}  // namespace qifeng_ca
