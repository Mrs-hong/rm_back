//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_RECORDING_MANAGER_H
#define QIFENG_CA_INCLUDE_INTERNAL_RECORDING_MANAGER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace qifeng_ca {

    // 录音数据保证每次只能有一个实例运行
    // RecordingManager 逻辑简单，只提供全局的实时录音信息查询能力
    class RecordingManager {
    public:
        static RecordingManager &GetInstance();

        bool StartRecording(const std::string &audioId, uint64_t accountId, const std::string &taskId = "");
        bool StopRecording(const std::string &audioId);
        bool IsRecording() const;

        // 暂停/继续录音: IsRecording()保持true, 其他任务仍不能执行
        void SetPaused(bool paused);
        bool IsPaused() const;

        // 无音频输入状态: 录制中(非暂停)连续无声超过阈值时置true, 恢复发声置false
        void SetNoAudioInput(bool noAudio);
        bool IsNoAudioInput() const;

        std::string GetActiveTaskId() const;
        std::string GetActiveAudioId() const;
        uint64_t GetActiveAccountId() const;
        std::string GetWavFilePath() const;
        void SetWavFilePath(const std::string &path);
        std::string GetActiveMeetingName() const;
        void SetActiveMeetingName(const std::string &name);
        int64_t GetActiveStartTime() const;

    private:
        RecordingManager() = default;

        void ClearActiveAudio();

        mutable std::mutex mMutex;
        std::string mTaskId;
        std::string mActiveAudioId;
        uint64_t mActiveAccountId {0};
        std::string mWavFilePath;
        std::string mActiveMeetingName;
        int64_t mActiveStartTime {0};
        std::atomic<bool> mRecording {false};
        std::atomic<bool> mPaused {false};
        std::atomic<bool> mNoAudioInput {false};
    };

    inline bool IsRecording() {
        return RecordingManager::GetInstance().IsRecording();
    }

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_RECORDING_MANAGER_H
