/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_RECORD_RECORD_HAL_H
#define HAL_RECORD_RECORD_HAL_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <alsa/asoundlib.h>

#include "hal/ring_buffer.h"

namespace qifeng {
    struct AudioDeviceConfig {
        std::string pcm_name = "hw:CARD=M702,DEV=0";
        std::vector<std::string> blacklist_pcm_names {};
    };

    struct AudioFormatConfig {
        uint32_t sample_rate = 0;
        uint16_t channels = 0;
        uint16_t bit_depth = 0;
    };

    struct AudioBehaviorConfig {
        uint32_t latency_ms = 100;
        uint32_t ring_buffer_size = 12 * 1024 * 1024;
    };

    struct AudioConfig {
        AudioDeviceConfig device {};
        AudioFormatConfig format {};
        AudioBehaviorConfig behavior {};
    };

    enum class AudioState : uint8_t { Uninitialized = 0, Idle, Recording, Unavailable };

    struct PcmContext {
        snd_pcm_t* handle = nullptr;
        snd_pcm_uframes_t frames = 0;
        size_t frame_bytes = 0;
    };

    /**
     * @brief 录音硬件抽象层
     *
     * 基于ALSA的PCM录音，内部维护常驻采集线程和环形缓冲区
     */
    class RecordHAL {
    public:
        RecordHAL() = default;

        ~RecordHAL();

        RecordHAL(const RecordHAL&) = delete;
        RecordHAL& operator=(const RecordHAL&) = delete;
        RecordHAL(RecordHAL&&) = delete;
        RecordHAL& operator=(RecordHAL&&) = delete;

        /**
         * @brief 初始化录音HAL
         * @param config 音频配置
         * @return 初始化成功返回true
         */
        bool Init(const AudioConfig& config);

        /**
         * @brief 释放资源，停止采集线程
         */
        void Release();

        /**
         * @brief 开始录音
         * @return 启动成功返回true
         */
        bool StartRecording();

        /**
         * @brief 停止录音
         * @return 停止成功返回true
         */
        bool StopRecording();

        /**
         * @brief 读取录音数据
         * @param buffer 输出缓冲区，不足max_size时自动扩容
         * @param max_size 最大读取字节数
         * @return 实际读取的字节数
         */
        size_t Read(std::vector<uint8_t>& buffer, size_t max_size);

        /**
         * @brief 获取录音状态
         * @return 当前音频状态
         */
        AudioState GetState() const;

        /**
         * @brief 获取当前设备实际音频参数
         * @return 设备未就绪时返回默认值0
         */
        AudioFormatConfig GetActualAudioFormat() const;

        /**
         * @brief 检测是否有物理麦克风连接
         * @return 检测到 hw: 前缀的采集设备返回true, 否则false
         */
        bool HasMicrophone() const;

    private:
        void CaptureLoop();

        void ReadPcmData();

        bool OpenDevice();

        void CloseDevice();

        std::vector<std::string> EnumerateCaptureDevices() const;

        bool IsBlacklistedDevice(const std::string& pcmName) const;

        // 首选配置的 pcm_name，打开失败再回退到自动枚举(已排除黑名单)，按出现顺序去重
        std::vector<std::string> BuildDeviceCandidates() const;

        std::vector<uint32_t> GetCandidateSampleRates() const;

        std::vector<uint16_t> GetCandidateChannels() const;

        std::vector<uint16_t> GetCandidateBitDepths() const;

        std::vector<AudioFormatConfig> GetCandidateFormats() const;

        bool TryOpenDevice(const std::string& pcmName, const AudioFormatConfig& format);

        void LogOpenedDevice(const std::string& pcmName, const AudioFormatConfig& requestedFormat,
                             const AudioFormatConfig& actualAudioFormat) const;

        bool InitHwParams(std::unique_ptr<snd_pcm_hw_params_t, decltype(&snd_pcm_hw_params_free)>& hwParams);

        snd_pcm_format_t ConvertFormat(uint16_t bit_depth);

        uint16_t ConvertBitDepth(snd_pcm_format_t format) const;

        bool ConfigureAudioFormat(std::unique_ptr<snd_pcm_hw_params_t, decltype(&snd_pcm_hw_params_free)>& hwParams,
                                  const AudioFormatConfig& format, unsigned int& actualSampleRate);

        bool ConfigureBufferLayout(std::unique_ptr<snd_pcm_hw_params_t, decltype(&snd_pcm_hw_params_free)>& hwParams,
                                   const AudioFormatConfig& format);

        bool ApplyHwParams(std::unique_ptr<snd_pcm_hw_params_t, decltype(&snd_pcm_hw_params_free)>& hwParams);

        bool RecoverPcm(int err);

        void ResetRuntimeAudioState();

    private:
        mutable std::mutex mMutex;
        std::condition_variable mCv;

        std::atomic<AudioState> mState {AudioState::Uninitialized};
        AudioConfig mConfig;
        AudioFormatConfig mActualAudioFormat {};

        std::unique_ptr<PcmContext> mPcmContext {};
        std::optional<RingBuffer<uint8_t>> mRingBuffer;
        std::string mActivePcmName;

        std::atomic<bool> mThreadExit {false};
        std::atomic<bool> mRecordingActive {false};
        std::thread mCaptureThread;
    };

}  // namespace qifeng

#endif  // HAL_RECORD_RECORD_HAL_H
