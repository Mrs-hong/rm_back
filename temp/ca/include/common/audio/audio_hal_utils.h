/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_COMMON_AUDIO_AUDIO_HAL_UTILS_H
#define QIFENG_CA_INCLUDE_COMMON_AUDIO_AUDIO_HAL_UTILS_H

#include <string>

#include "qifeng_ca/meeting.pb.h"

#include "common/status.h"

namespace qifeng_ca {

    enum class AudioType : uint8_t {
        Normal = 1,
        Public = 2,
        Private = 3,
    };

    Status HalStartAudio(const std::string &meetingName, const std::string &sponsor, AudioType type = AudioType::Normal,
                         bool isDisplay = true);

    void HalStopAudio();

    // 暂停实时录音: 关闭HAL音频接收(StopRecording), 但不释放设备
    Status HalPauseAudio(RecordStopType type);

    // 继续实时录音: 重新启动HAL音频接收(StartRecording)
    Status HalResumeAudio(RecordStopType type);

    // HAL指纹开启录音失败对外提供的LED接口（目的是为了HAL开启失败蓝灯不变化打补丁）
    bool StopFingerprintLed();
    bool SetupFingerprintLed();

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_AUDIO_AUDIO_HAL_UTILS_H
