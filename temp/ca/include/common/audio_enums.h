//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_AUDIO_ENUMS_H
#define QIFENG_CA_INCLUDE_COMMON_AUDIO_ENUMS_H

#include <cstdint>
#include <string_view>

namespace qifeng_ca {

    // 音频来源类型
    enum class AudioSource : int32_t {
        DeviceCollect = 1,  // 设备采集(实时录音)
        UserUpload = 2      // 用户上传(离线录音)
    };

    // 录音状态(对应数据库is_recording字段)
    enum class RecordingFlag : int32_t { NotRecording = 0, Recording = 1 };

    // 音频处理状态(对应数据库status字段)
    enum class AudioStatus : int32_t {
        Meeting = 0,  // 录音中
        Paused = 6,   // 录音暂停(实时录音临时停止, 恢复后继续录音)

        WaitTrans = 1,  // 等待转写
        Transing = 2,   // 转写中

        WaitSummary = 3,      // 等待生成纪要
        Summarying = 4,       // 纪要生成中
        SummaryComplete = 5,  // 纪要完成

        TransException = 98,   // 转写异常
        PermanentFailed = 99,  // 永久失败
        Deleting = 100         // 删除中
    };

    // 音频状态对应的固定提示文案(UpdateAudioStatus/UpdateStatus 的 message 参数统一引用此处)
    // 设计目的: 避免散落在各任务实现中的字符串字面量, 便于统一维护和国际化
    namespace AudioStatusMsg {
        // Meeting: 实时录音转写中
        static constexpr std::string_view RecordingTranscribing = "录音转写中";

        // Deleting: 删除中
        static constexpr std::string_view Deleting = "删除中";

        // Paused: 实时录音已暂停
        static constexpr std::string_view RecordingPaused = "录音已暂停";

        // WaitTrans: 等待转写 / 任务恢复转为离线转写 / 任务恢复重新离线转写
        static constexpr std::string_view WaitTrans = "等待转写";
        static constexpr std::string_view RecoverToOfflineTrans = "任务恢复,转为离线转写";
        static constexpr std::string_view RecoverRestartOfflineTrans = "任务恢复,重新离线转写";

        // Transing: 离线转写中
        static constexpr std::string_view OfflineTranscribing = "离线转写中";

        // Transing: 文件模拟实时转写中(测试用, 以离线音频替代麦克风输入测试实时转写链路)
        static constexpr std::string_view FileRealtimeTranscribing = "文件实时转写中";

        // WaitSummary: 等待总结 / 等待生成会议纪要 / 等待纪要生成(用户主动中断)
        static constexpr std::string_view WaitSummary = "等待总结";
        static constexpr std::string_view WaitMeetingSummary = "等待生成会议纪要";
        static constexpr std::string_view WaitReMeetingSummary = "等待重新生成会议纪要";

        // Summarying: 开始总结
        static constexpr std::string_view Summarying = "开始总结";

        // SummaryComplete: 纪要生成完成
        static constexpr std::string_view SummaryComplete = "纪要生成完成";

        // PermanentFailed: 通用失败原因(具体错误信息由调用方动态传入)
        static constexpr std::string_view AudioFileMissing = "音频文件不存在";
        static constexpr std::string_view AudioFileParseFailed = "音频文件解析失败";
        static constexpr std::string_view OfflineTransFailed = "离线转写失败";
        static constexpr std::string_view OfflineTransTimeout = "离线转写超时";
        static constexpr std::string_view FileRealtimeTransFailed = "文件实时转写失败";

        // 恢复中断的纪要
        static constexpr std::string_view RecoverRestartSummary = "任务恢复,重新生成纪要";
    }  // namespace AudioStatusMsg

    // 总结任务Status消息文案(用于SummaryResult.mStatus, 失败时也会写入DB的remark字段)
    // 设计目的: 统一管理LMS状态码映射文案与总结任务内部错误文案
    namespace SummaryMsg {
        // LMS StateCode 映射文案
        static constexpr std::string_view InputTooShort = "输入文本过短";
        static constexpr std::string_view InputTooLong = "输入文本过长";
        static constexpr std::string_view ChainError = "推理链内部错误";
        static constexpr std::string_view LmsConnectionError = "LLM服务连接失败";
        static constexpr std::string_view ManualStopInfer = "用户主动中断推理";  // code=1, 非错误
        static constexpr std::string_view OutputTooLong = "输出文本过长";
        static constexpr std::string_view StillInRunning = "推理未完成";
        static constexpr std::string_view UnknownError = "未知错误";

        // 总结任务内部错误文案
        // static constexpr std::string_view TransContentEmpty = "转写内容为空";
        static constexpr std::string_view CreateSummarizerFailed = "创建Summarizer失败";
        static constexpr std::string_view InterruptedWaitingLms = "等待LMS就绪时被中断";  // code=1, 非错误
        static constexpr std::string_view SummaryTimeout = "总结超时";
    }  // namespace SummaryMsg

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_AUDIO_ENUMS_H
