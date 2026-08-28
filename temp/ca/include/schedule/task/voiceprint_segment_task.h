//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_SCHEDULE_TASK_VOICEPRINT_SEGMENT_TASK_H
#define QIFENG_CA_INCLUDE_SCHEDULE_TASK_VOICEPRINT_SEGMENT_TASK_H

#include <cstdint>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "common/audio/audio_utils.h"
#include "common/status.h"
#include "schedule/task/base_task.h"
#include "schedule/task/voiceprint_task.h"

namespace qifeng_ca {

    // 音频时间段(毫秒)
    struct AudioSegment {
        int32_t mStartMs {0};
        int32_t mEndMs {0};
    };

    // 分段声纹提取任务(可抢占): 读取WAV -> 按Segment分段提取 -> 合并截断(60-120s) -> AAS StartSVJob -> 回调
    class VoiceprintSegmentTask : public BaseTask {
    public:
        VoiceprintSegmentTask(const std::string &audioId, uint64_t accountId, uint64_t fileAccoutId = 0);

        ~VoiceprintSegmentTask() override;

        VoiceprintSegmentTask(const VoiceprintSegmentTask &) = delete;
        VoiceprintSegmentTask(VoiceprintSegmentTask &&) = delete;
        VoiceprintSegmentTask &operator=(const VoiceprintSegmentTask &) = delete;
        VoiceprintSegmentTask &operator=(VoiceprintSegmentTask &&) = delete;

        // BaseTask接口
        void Start() override;
        void Stop() override;
        int Priority() const override { return static_cast<int>(Priority::VoiceprintSegment); }
        bool IsTemporaryConcurrent() const override { return true; }
        std::string_view GetTaskType() const override { return "VoiceprintSegment"; }

        // 设置分段和回调(在Start前调用)
        void SetSegments(const std::vector<AudioSegment> &segments) { mSegments = segments; }
        void SetCallback(const VoiceprintCallback &cb) { mCallback = cb; }

    private:
        // 获取自身shared_ptr(用于AAS回调)
        std::shared_ptr<VoiceprintSegmentTask> Self() {
            return std::static_pointer_cast<VoiceprintSegmentTask>(shared_from_this());
        }

        // 从WAV文件中按Segment分段读取PCM数据, 边读边流式写入输出文件, 同时累积到outPcm供AAS使用
        bool ExtractAndMergeSegments(std::vector<uint8_t> &outPcm);

        // 从WAV文件中读取指定时间段的PCM数据
        bool ReadSegmentPcm(const AudioSegment &seg, std::vector<uint8_t> &outPcm);

        // 将合并后的PCM数据提交到AAS声纹提取
        void SubmitToAas();

        // 打开输出WAV文件并写入头(含占位数据大小)
        bool OpenOutputWavFile(const AudioUtilsConfig &config);

        // 完成输出WAV文件(更新头中的实际数据大小)
        void FinalizeOutputWavFile();

        // AAS回调: 接收声纹提取结果
        void OnAasResult(qifeng::aas::AasResult aasResult);

        // 通知结果
        void NotifyResult(const VoiceprintResult &result);

    private:
        // WAV文件信息
        int mWavSampleRate {0};
        int mWavChannels {0};
        int mWavBitDepth {0};

        // 音频文件所属用户ID，RBAC中不用用户的声纹可以添加其他账号的音频到不同账号下
        uint64_t mFileAccoutId {0};

        // 合并后的PCM数据
        std::vector<uint8_t> mPcmBuffer;

        // WAV文件路径
        std::string mFilePath;

        // 输出WAV文件流(流式写入, 避免全量缓存)
        std::ofstream mOutputWavFile;
        // 输出WAV文件已写入的PCM数据大小
        uint32_t mOutputWavSize {0};

        // 输入分段
        std::vector<AudioSegment> mSegments;
        // 回调
        VoiceprintCallback mCallback;

        // 声纹音频最短时长(秒)
        static constexpr int MinVoiceprintDurationSec = 60;
        // 声纹音频最长时长(秒)
        static constexpr int MaxVoiceprintDurationSec = 120;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_SCHEDULE_TASK_VOICEPRINT_SEGMENT_TASK_H
