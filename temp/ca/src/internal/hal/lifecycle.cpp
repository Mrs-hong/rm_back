/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "qifeng_ca/meeting.pb.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/common.h"
#include "common/config/hal_config.h"
#include "common/config/upgrade_config.h"
#include "common/utils/file_name_generator.h"
#include "core/meeting/recording_service.h"
#include "core/network/network_service.h"
#include "core/upgrade/upgrade_service.h"
#include "internal/display_manager.h"
#include "internal/hal/fingerprint_bridge.h"
#include "internal/hal/fingerprint_meeting_controller.h"
#include "internal/hal/hal_bridge.h"
#include "internal/hal/hal_types.h"
#include "internal/hal/lifecycle.h"
#include "internal/recording_manager.h"

namespace qifeng_ca {
    namespace hal {

        static void SubmitBootDisplayTasks(HalBridge &hal);
        static void OnButtonEvent(const ButtonEventData &event);
        static void OnFingerprintEvent(const FingerprintEventData &event);
        static void OnLedEvent(uint32_t ledId, MicMuteEvent event);
        static void HandleShortPress();
        static void HandleLongPress();
        static void HandleFingerprintMatched(uint16_t fingerId, bool matched);

        bool InitHal() {
            auto &hal = HalBridge::GetInstance();

            // 初始化所有HAL设备(含指纹)
            if (!hal.InitAll()) {
                SLOG_ERROR << "TaskRegister: HAL init failed, some devices unavailable";
                return false;
            }

            // 指纹设备就绪状态
            if (FingerprintBridge::GetInstance().IsInitialized()) {
                SLOG_INFO << "TaskRegister: fingerprint device ready";
            } else {
                SLOG_WARN << "TaskRegister: fingerprint device not available";
            }

            // 注册按键回调
            if (HalConfig::GetInstance().IsSkipButton()) {
                SLOG_WARN << "TaskRegister: skip button";
            } else {
                if (HalConfig::GetInstance().IsFingerprintBridge()) {
                    // 注册指纹回调(根据指纹ID控制开始/停止会议)
                    if (FingerprintBridge::GetInstance().IsInitialized()) {
                        SLOG_DEBUG << "TaskRegister: set fingerprint callback";
                        FingerprintBridge::GetInstance().SetCallback(OnFingerprintEvent);
                    } else {
                        SLOG_WARN << "TaskRegister: fingerprint device not available, skip set callback";
                    }
                } else {
                    hal.SetButtonCallback(OnButtonEvent);
                }

                // 注册灯光按键(麦克风静音)回调
                hal.SetLedCallback(OnLedEvent);
            }

            // 检查升级结果(在HAL初始化后调用, 确保屏幕可展示升级结果)
            qifeng_ca::UpgradeService::CheckUpgradeResultAndSetComplete(
                qifeng_ca::UpgradeConfig::GetInstance().GetUpgradeSoftDir());
            SLOG_INFO << "Bms: upgrade result checked";

            // 开机显示: 待机页面 + 红灯
            SubmitBootDisplayTasks(hal);

            return true;
        }

        bool ShutdownHal() {
            auto &hal = HalBridge::GetInstance();
            hal.ReleaseAll();

            return true;
        }

        void OnButtonEvent(const ButtonEventData &event) {
            SLOG_INFO << "TaskRegister: button event, id=" << event.mButtonId
                      << " type=" << static_cast<int>(event.mPressType);

            if (event.mPressType == ButtonPressType::ShortPress) {
                HandleShortPress();
            } else {
                HandleLongPress();
            }
        }

        void HandleShortPress() {
            auto &recMgr = RecordingManager::GetInstance();
            if (recMgr.IsRecording()) {
                // 短按停止录音: 通过MeetingService::RecordStop完成完整链路
                auto audioId = recMgr.GetActiveAudioId();
                auto accountId = recMgr.GetActiveAccountId();

                RecordStopRequest req;
                req.set_audio_id(audioId);
                req.set_account_id(accountId);
                qifeng_ca::Empty resp;
                auto status = RecordingService().RecordStop(req, &resp);
                if (status.GetCode() != 0) {
                    SLOG_ERROR << "TaskRegister: short press stop recording failed, msg=" << status.ToString();
                } else {
                    SLOG_INFO << "TaskRegister: short press -> stop recording, audioId=" << audioId;
                }
            } else {
                // 短按开始录音: 构造AddRecording请求
                AddRecordingRequest req;
                req.set_theme(FileNameGenerator::GenFile("新会议"));
                req.set_recording_time(static_cast<int64_t>(GetTimeMs()));
                req.set_is_rel_time(true);
                req.set_account_id(GetGuestAccountId());
                AddRecordingResponse resp;
                auto status = RecordingService().AddRecording(req, &resp);
                if (status.GetCode() != 0) {
                    SLOG_ERROR << "TaskRegister: short press start recording failed, msg=" << status.ToString();
                } else {
                    SLOG_INFO << "TaskRegister: short press -> start recording, audioId="
                              << (resp.audio_ids_size() > 0 ? resp.audio_ids(0) : "");
                }
            }
        }

        void HandleLongPress() {
            // 长按: 重置网络配置为默认值
            SLOG_INFO << "TaskRegister: long press -> reset network config";
            NetworkService netService;
            SetNetworkResponse resp;
            auto status = netService.ResetNetwork(&resp);
            if (status.GetCode() != 0) {
                SLOG_ERROR << "TaskRegister: reset network failed, msg=" << status.ToString();
            } else {
                SLOG_INFO << "TaskRegister: network reset to default";
            }
        }

        void OnFingerprintEvent(const FingerprintEventData &event) {
            SLOG_INFO << "TaskRegister: fingerprint event, type=" << static_cast<int>(event.mType)
                      << " matched=" << event.mMatched << " fingerId=" << event.mFingerId;

            // 仅在识别成功(Record事件且匹配)时触发业务
            if (event.mType == FingerprintEventType::Recognizing) {
                if (HalConfig::GetInstance().IsFingerprintBridge()) {
                    if (FingerprintBridge::GetInstance().SetLed(qifeng::FingerprintLedState::BlueBreath, 500) !=
                        FingerprintResult::OK) {
                        SLOG_ERROR << "FingerprintBridge: SetLed failed";
                    }
                }
            } else if (event.mType == FingerprintEventType::RecognizingEnd) {
                if (HalConfig::GetInstance().IsFingerprintBridge()) {
                    auto led = qifeng::FingerprintLedState::Red;
                    if (RecordingManager::GetInstance().IsRecording() || HalBridge::GetInstance().IsRecordingActive()) {
                        led = qifeng::FingerprintLedState::Green;
                    }
                    if (FingerprintBridge::GetInstance().SetLed(led, 500) != FingerprintResult::OK) {
                        SLOG_ERROR << "FingerprintBridge: SetLed failed";
                    }
                }
            }
            if (event.mType != FingerprintEventType::Record) {
                return;
            }
            HandleFingerprintMatched(event.mFingerId, event.mMatched);
        }

        // 根据指纹ID对应的用户身份控制开始/停止会议
        void HandleFingerprintMatched(uint16_t fingerId, bool matched) {
            FingerprintMeetingController::GetInstance().HandleFingerprintMatched(fingerId, matched);
        }

        void OnLedEvent(uint32_t ledId, MicMuteEvent event) {
            (void)ledId;
            SLOG_INFO << "TaskRegister: led event, type=" << static_cast<int>(event);

            auto &recMgr = RecordingManager::GetInstance();
            if (!recMgr.IsRecording()) {
                SLOG_WARN << "TaskRegister: not recording, ignore led event";
                return;
            }

            RecordStopRequest req;
            req.set_audio_id(recMgr.GetActiveAudioId());
            req.set_account_id(recMgr.GetActiveAccountId());
            req.set_type(RecordStopType::Default);
            Empty resp;

            if (event == MicMuteEvent::Mute && !recMgr.IsPaused()) {
                auto status = RecordingService().PauseRecording(req, &resp);
                if (status.GetCode() != 0) {
                    SLOG_ERROR << "TaskRegister: led mute -> pause failed, msg=" << status.ToString();
                } else {
                    SLOG_INFO << "TaskRegister: led mute -> recording paused";
                }
            } else if (event == MicMuteEvent::Unmute && recMgr.IsPaused()) {
                auto status = RecordingService().ResumeRecording(req, &resp);
                if (status.GetCode() != 0) {
                    SLOG_ERROR << "TaskRegister: led unmute -> resume failed, msg=" << status.ToString();
                } else {
                    SLOG_INFO << "TaskRegister: led unmute -> recording resumed";
                }
            } else {
                SLOG_DEBUG << "TaskRegister: led event ignored (already in target state)";
            }
        }

        void SubmitBootDisplayTasks(HalBridge &hal) {
            // 显示屏: 切换到待机页面并显示默认指标
            DisplayManager::GetInstance().ShowBootDisplay();

            // 设置LED为红灯(待机状态)
            if (HalConfig::GetInstance().IsSkipButton()) {
                SLOG_WARN << "FingerprintBridge: skip button";
            } else {
                if (HalConfig::GetInstance().IsFingerprintBridge()) {
                    if (FingerprintBridge::GetInstance().SetLed(qifeng::FingerprintLedState::Red, 500) !=
                        FingerprintResult::OK) {
                        SLOG_ERROR << "FingerprintBridge: SetLed failed";
                    }
                }
            }
            LedState ledRed {true, HalLedColor::Red, HalLedMode::Solid};
            hal.SetLedState(ledRed);

            SLOG_INFO << "TaskRegister: boot display tasks done (idle page + red led)";
        }
    }  // namespace hal
}  // namespace qifeng_ca
