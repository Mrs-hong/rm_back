//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//
#include <cstdint>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "internal/recording_manager.h"
#include "schedule/pcm/pcm_engine.h"

namespace qifeng_ca {

    RecordingManager &RecordingManager::GetInstance() {
        static RecordingManager Instance;
        return Instance;
    }

    bool RecordingManager::StartRecording(const std::string &audioId, uint64_t accountId, const std::string &taskId) {
        std::lock_guard<std::mutex> lock(mMutex);

        if (mRecording.load(std::memory_order_acquire)) {
            SLOG_WARN << "RecordingManager: already recording";
            return false;
        }

        mTaskId = taskId;
        mActiveAudioId = audioId;
        mActiveAccountId = accountId;
        mActiveStartTime = static_cast<int64_t>(GetTimeMs() / 1000);
        mRecording.store(true, std::memory_order_release);
        SLOG_INFO << "RecordingManager: start recording, audioId=" << audioId;
        return true;
    }

    bool RecordingManager::StopRecording(const std::string &audioId) {
        std::lock_guard<std::mutex> lock(mMutex);

        if (!mRecording.load(std::memory_order_acquire)) {
            SLOG_WARN << "RecordingManager: not recording";
            return false;
        }

        if (!mActiveAudioId.empty() && mActiveAudioId != audioId) {
            SLOG_WARN << "RecordingManager: audioId mismatch, active=" << mActiveAudioId << " request=" << audioId;
            return false;
        }

        ClearActiveAudio();
        SLOG_INFO << "RecordingManager: stop recording";
        return true;
    }

    bool RecordingManager::IsRecording() const {
        return mRecording.load(std::memory_order_acquire);
    }

    void RecordingManager::SetPaused(bool paused) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mRecording.load(std::memory_order_acquire)) {
            SLOG_WARN << "RecordingManager: not recording, cannot set paused=" << paused;
            return;
        }
        mPaused.store(paused, std::memory_order_release);
        SLOG_INFO << "RecordingManager: set paused=" << paused << ", audioId=" << mActiveAudioId;
    }

    bool RecordingManager::IsPaused() const {
        return mPaused.load(std::memory_order_acquire);
    }

    std::string RecordingManager::GetActiveTaskId() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mTaskId;
    }
    std::string RecordingManager::GetActiveAudioId() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mActiveAudioId;
    }

    uint64_t RecordingManager::GetActiveAccountId() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mActiveAccountId;
    }

    std::string RecordingManager::GetWavFilePath() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mWavFilePath;
    }

    void RecordingManager::SetWavFilePath(const std::string &path) {
        std::lock_guard<std::mutex> lock(mMutex);
        mWavFilePath = path;
    }

    std::string RecordingManager::GetActiveMeetingName() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mActiveMeetingName;
    }

    void RecordingManager::SetActiveMeetingName(const std::string &name) {
        std::lock_guard<std::mutex> lock(mMutex);
        mActiveMeetingName = name;
    }

    int64_t RecordingManager::GetActiveStartTime() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mActiveStartTime;
    }

    void RecordingManager::ClearActiveAudio() {
        mActiveAudioId = "";
        mActiveAccountId = 0;
        mWavFilePath = "";
        mActiveMeetingName = "";
        mActiveStartTime = 0;
        mTaskId = "";
        mRecording.store(false, std::memory_order_release);
        mPaused.store(false, std::memory_order_release);
        mNoAudioInput.store(false, std::memory_order_release);
    }

    void RecordingManager::SetNoAudioInput(bool noAudio) {
        mNoAudioInput.store(noAudio, std::memory_order_release);
    }

    bool RecordingManager::IsNoAudioInput() const {
        return mNoAudioInput.load(std::memory_order_acquire);
    }

}  // namespace qifeng_ca
