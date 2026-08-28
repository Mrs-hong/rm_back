//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_EXTERNAL_SERVICES_SERVICE_TYPES_H
#define QIFENG_CA_EXTERNAL_SERVICES_SERVICE_TYPES_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "common/audio/audio_utils.h"

namespace qifeng_ca::external_services {

    // 类型: 转写 / 总结
    enum class ServiceType { Transcribe, Summary };

    // 类型: Base64字符串 / URL / 二进制文件流
    enum class AudioInputMode { Base64, Url, FileStream };

    struct AudioInput {
        AudioInputMode mMode {AudioInputMode::Base64};
        std::string mData;            // Base64 字符串 / URL / 文件路径
        std::vector<uint8_t> mBytes;  // 原始二进制
    };

    // 统一请求
    struct ServiceRequest {
        AudioInput mAudio;
        std::string mAudioFormat {"wav"};  // mp3 / wav / pcm / m4a
        AudioUtilsConfig mAudioConfig;
        std::string mLanguage {"zh"};
        std::string mModel;              // 指定模型
        bool mEnableDiarization {true};  // 是否启用说话人分离
        // 音频实际开始/结束时间(毫秒), 用于补充Segment时间信息
        int64_t mAudioStartMs {0};
        int64_t mAudioEndMs {0};
        // 预留扩展字段
        std::map<std::string, std::string> mExtras;
    };

    // 时间戳片段: 可按句或按词划分（尽量与现在的接口一致）
    struct Segment {
        int32_t mOrder {0};
        int64_t mStartMs {0};
        int64_t mEndMs {0};
        std::string mText;
        std::string mSpeakerId;
        int32_t mSpeakerLabel {0};
        std::string mSpeakerName;
        bool mByWord {false};  // true=按词, false=按句
    };

    // 错误信息: 包含 code 与 message
    struct ErrorInfo {
        int mCode {0};
        std::string mMessage;
    };

    // 统一服务响应结构
    struct ServiceResponse {
        bool mSuccess {false};
        std::vector<Segment> mSegments;
        ErrorInfo mError;
        std::string mRawResponse;  // 原始响应, 用于调试或透传
        int64_t mProcessingTimeMs {0};
    };

    // 总结请求
    struct SummaryRequest {
        std::string mText;  // 转写文本
        std::string mDate;
        std::string mLocation;
        std::string mHost;
        std::string mAttendees;
        std::string mModel;
        std::map<std::string, std::string> mExtras;  // 预留扩展
    };

    // 总结结果
    struct SummaryResult {
        bool mSuccess {false};
        std::string mOverview;
        std::vector<std::string> mKeywords;
        ErrorInfo mError;
        std::string mRawResponse;
    };

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_SERVICE_TYPES_H
