/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "hal/record/record_hal.h"

#include "common/logger.h"

#include <algorithm>
#include <alsa/asoundlib.h>
#include <asm-generic/errno.h>
#include <set>

namespace qifeng {
    constexpr uint32_t SampleRate16K = 16000;
    constexpr uint32_t SampleRate32K = 32000;
    constexpr uint32_t SampleRate48K = 48000;

    template <typename T>
    static std::vector<T> BuildCandidates(T preferred, std::initializer_list<T> fallbacks) {
        std::vector<T> candidates;
        if (preferred > 0) {
            candidates.push_back(preferred);
        }

        for (const T value : fallbacks) {
            if (value == 0) {
                continue;
            }

            if (std::find(candidates.begin(), candidates.end(), value) == candidates.end()) {
                candidates.push_back(value);
            }
        }

        return candidates;
    }

    RecordHAL::~RecordHAL() {
        Release();
    }

    bool RecordHAL::Init(const AudioConfig& config) {
        AudioState expected = AudioState::Uninitialized;
        if (!mState.compare_exchange_strong(expected, AudioState::Idle, std::memory_order_acq_rel)) {
            SLOG_WARN << "RecordHAL already initialized, current state: " << static_cast<int>(expected);
            return true;
        }

        if (config.format.sample_rate == 0 || config.format.channels == 0 || config.format.bit_depth == 0) {
            FLOG_ERROR("Invalid audio format config: sample_rate, channels and bit_depth must > 0");
            mState.store(AudioState::Uninitialized, std::memory_order_release);
            return false;
        }

        if (config.behavior.ring_buffer_size == 0) {
            FLOG_ERROR("Invalid audio behavior config: ring_buffer_size must > 0");
            mState.store(AudioState::Uninitialized, std::memory_order_release);
            return false;
        }

        if (config.device.pcm_name.empty()) {
            FLOG_ERROR("Invalid audio device config: pcm_name must not be empty");
            mState.store(AudioState::Uninitialized, std::memory_order_release);
            return false;
        }

        mConfig = config;
        mRingBuffer.emplace(config.behavior.ring_buffer_size);
        mPcmContext = std::make_unique<PcmContext>();
        ResetRuntimeAudioState();

        mThreadExit.store(false, std::memory_order_release);
        mRecordingActive.store(false, std::memory_order_release);

        // 创建常驻采集线程
        mCaptureThread = std::thread(&RecordHAL::CaptureLoop, this);


        SLOG_INFO << "RecordHAL initialized: pcm_device='" << mConfig.device.pcm_name
                  << "' blacklist_size=[" << mConfig.device.blacklist_pcm_names.size()
                  << "] rate=" << mConfig.format.sample_rate
                  << " channels=" << mConfig.format.channels << " bits=" << mConfig.format.bit_depth;

        return true;
    }

    void RecordHAL::Release() {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mThreadExit.store(true, std::memory_order_release);
            mRecordingActive.store(false, std::memory_order_release);
        }
        if (mPcmContext && mPcmContext->handle) {
            snd_pcm_drop(mPcmContext->handle);
        }

        mCv.notify_all();

        if (mCaptureThread.joinable()) {
            mCaptureThread.join();
        }

        CloseDevice();
        mRingBuffer.reset();
        mPcmContext.reset();
        ResetRuntimeAudioState();
        mState.store(AudioState::Uninitialized, std::memory_order_release);
    }

    bool RecordHAL::StartRecording() {
        {
            std::lock_guard<std::mutex> lock(mMutex);

            const AudioState state = mState.load(std::memory_order_acquire);  // NOLINT(cppcoreguidelines-init-variables)
            if (state == AudioState::Recording) {
                SLOG_WARN << "RecordHAL already recording";
                return false;
            }

            if (state == AudioState::Uninitialized) {
                SLOG_ERROR << "RecordHAL not initialized, current state: " << static_cast<int>(state);
                return false;
            }

            mState.store(AudioState::Recording, std::memory_order_release);
        }

        // 设备探测为阻塞式 ALSA 调用，且 OpenDevice 内部 helper 会自行加锁 mMutex，
        if (!OpenDevice()) {
            mState.store(AudioState::Unavailable, std::memory_order_release);
            SLOG_ERROR << "Failed to open pcm device";
            return false;
        }

        bool committed = false;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            // OpenDevice 期间被 Stop 抢占（置 Idle）则放弃，避免覆盖 Stop 意图
            if (mState.load(std::memory_order_acquire) == AudioState::Recording) {
                mRingBuffer->Reset();
                mRecordingActive.store(true, std::memory_order_release);
                committed = true;
            }
        }

        if (!committed) {
            CloseDevice();
            SLOG_WARN << "RecordHAL start aborted during startup";
            return false;
        }

        mCv.notify_one();
        SLOG_INFO << "RecordHAL started recording";
        return true;
    }

    bool RecordHAL::StopRecording() {
        std::lock_guard<std::mutex> lock(mMutex);

        const AudioState state = mState.load(std::memory_order_acquire);  // NOLINT(cppcoreguidelines-init-variables)
        if (state != AudioState::Recording && state != AudioState::Unavailable) {
            return true;
        }

        mRecordingActive.store(false, std::memory_order_release);
        mState.store(AudioState::Idle, std::memory_order_release);
        if (mPcmContext && mPcmContext->handle) {
            snd_pcm_drop(mPcmContext->handle);
        }
        mCv.notify_one();

        SLOG_INFO << "RecordHAL stopped recording";
        return true;
    }

    size_t RecordHAL::Read(std::vector<uint8_t>& buffer, size_t max_size) {
        if (!mRingBuffer.has_value()) {
            return 0;
        }

        if (max_size == 0) {
            return 0;
        }

        if (buffer.size() < max_size) {
            buffer.resize(max_size);
        }

        size_t bytesRead = mRingBuffer->Read(buffer.data(), max_size);
        return bytesRead;
    }

    AudioState RecordHAL::GetState() const {
        return mState.load(std::memory_order_acquire);
    }

    AudioFormatConfig RecordHAL::GetActualAudioFormat() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mActualAudioFormat;
    }

    bool RecordHAL::HasMicrophone() const {
        return !EnumerateCaptureDevices().empty();
    }

    void RecordHAL::CaptureLoop() {
        SLOG_INFO << "CaptureLoop started";

        while (!mThreadExit.load(std::memory_order_acquire)) {
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mCv.wait(lock, [this] {
                    return mRecordingActive.load(std::memory_order_acquire) ||
                           mThreadExit.load(std::memory_order_acquire);
                });

                if (mThreadExit.load(std::memory_order_acquire)) {
                    break;
                }
            }

            ReadPcmData();
            CloseDevice();
        }

        SLOG_INFO << "CaptureLoop exited";
    }

    void RecordHAL::ReadPcmData() {
        const size_t chunkFrames = mPcmContext->frames;
        const size_t chunkBytes = chunkFrames * mPcmContext->frame_bytes;
        std::vector<uint8_t> readBuffer(chunkBytes);

        while (mRecordingActive.load(std::memory_order_acquire) && !mThreadExit.load(std::memory_order_acquire)) {
            snd_pcm_sframes_t framesRead =
                snd_pcm_readi(mPcmContext->handle, readBuffer.data(), static_cast<snd_pcm_uframes_t>(chunkFrames));
            if (framesRead < 0) {
                int err = static_cast<int>(framesRead);

                if (!mRecordingActive.load(std::memory_order_acquire)) {
                    break;
                }

                SLOG_WARN << "PCM read error: " << snd_strerror(err);

                if (!RecoverPcm(err)) {
                    SLOG_ERROR << "Failed to recover PCM, stopping capture";
                    mRecordingActive.store(false, std::memory_order_release);
                    mState.store(AudioState::Unavailable, std::memory_order_release);
                    break;
                }
                continue;
            }

            if (framesRead == 0) {
                continue;
            }

            size_t bytesToWrite = static_cast<size_t>(framesRead) * mPcmContext->frame_bytes;
            size_t written = mRingBuffer->Write(readBuffer.data(), bytesToWrite);
            if (written < bytesToWrite) {
                SLOG_WARN << "Ring buffer overflow: wrote " << written << "/" << bytesToWrite << " bytes";
            }
        }
    }

    bool RecordHAL::OpenDevice() {
        if (mPcmContext->handle) {
            return true;
        }

        ResetRuntimeAudioState();

        // 优先尝试配置传入的麦克风名称, 全部失败再回退到自动枚举
        const auto devices = BuildDeviceCandidates();
        if (devices.empty()) {
            SLOG_ERROR << "No available PCM capture device found during auto-detect";
            return false;
        }

        const auto formats = GetCandidateFormats();
        for (const auto& pcmName : devices) {
            for (const auto& requestedFormat : formats) {
                if (TryOpenDevice(pcmName, requestedFormat)) {
                    SLOG_INFO << "Selected PCM device '" << pcmName
                              << "' with requested format rate=" << requestedFormat.sample_rate
                              << " channels=" << requestedFormat.channels << " bits=" << requestedFormat.bit_depth;
                    return true;
                }
            }
        }

        SLOG_ERROR << "Failed to auto-detect a working PCM capture device from " << devices.size()
                   << " candidate device(s)";
        return false;
    }

    bool RecordHAL::TryOpenDevice(const std::string& pcmName, const AudioFormatConfig& format) {
        if (!mPcmContext) {
            return false;
        }

        int err = snd_pcm_open(&mPcmContext->handle, pcmName.c_str(), SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0) {
            SLOG_WARN << "Failed to open PCM device '" << pcmName << "': " << snd_strerror(err);
            return false;
        }

        snd_pcm_hw_params_t* rawHwParams = nullptr;
        err = snd_pcm_hw_params_malloc(&rawHwParams);
        if (err < 0) {
            SLOG_ERROR << "Failed to allocate hw params: " << snd_strerror(err);
            CloseDevice();
            return false;
        }
        std::unique_ptr<snd_pcm_hw_params_t, decltype(&snd_pcm_hw_params_free)> hwParams(rawHwParams,
                                                                                         snd_pcm_hw_params_free);

        if (!InitHwParams(hwParams)) {
            CloseDevice();
            return false;
        }

        unsigned int actualSampleRate = format.sample_rate;
        if (!ConfigureAudioFormat(hwParams, format, actualSampleRate)) {
            CloseDevice();
            return false;
        }

        AudioFormatConfig bufferFormat = format;
        bufferFormat.sample_rate = actualSampleRate;
        if (!ConfigureBufferLayout(hwParams, bufferFormat)) {
            CloseDevice();
            return false;
        }

        if (!ApplyHwParams(hwParams)) {
            CloseDevice();
            return false;
        }

        mActivePcmName = pcmName;

        AudioFormatConfig actualAudioFormat;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            actualAudioFormat = mActualAudioFormat;
        }

        LogOpenedDevice(pcmName, format, actualAudioFormat);
        return true;
    }

    std::vector<std::string> RecordHAL::EnumerateCaptureDevices() const {
        std::vector<std::string> devices;
        std::set<std::string> uniqueDevices;

        void** hints = nullptr;
        const int err = snd_device_name_hint(-1, "pcm", &hints);
        if (err < 0) {
            SLOG_WARN << "Failed to enumerate PCM devices: " << snd_strerror(err);
            return devices;
        }

        for (void** current = hints; current != nullptr && *current != nullptr; ++current) {
            char* name = snd_device_name_get_hint(*current, "NAME");
            char* ioid = snd_device_name_get_hint(*current, "IOID");

            const bool isCapture = (ioid == nullptr) || (std::string(ioid) == "Input");
            if (name != nullptr && isCapture) {
                std::string pcmName(name);
                if (pcmName.rfind("hw:", 0) == 0 && !IsBlacklistedDevice(pcmName) &&
                    uniqueDevices.insert(pcmName).second) {
                    devices.push_back(pcmName);
                }
            }

            if (name != nullptr) {
                free(name);
            }
            if (ioid != nullptr) {
                free(ioid);
            }
        }

        snd_device_name_free_hint(hints);
        return devices;
    }

    bool RecordHAL::IsBlacklistedDevice(const std::string& pcmName) const {
        const auto& blacklist = mConfig.device.blacklist_pcm_names;
        return std::find(blacklist.begin(), blacklist.end(), pcmName) != blacklist.end();
    }

    std::vector<std::string> RecordHAL::BuildDeviceCandidates() const {
        std::vector<std::string> candidates;
        std::set<std::string> seen;

        // 首选配置指定的麦克风名称，不做黑名单过滤：明确指定的设备优先级高于黑名单
        if (!mConfig.device.pcm_name.empty() && seen.insert(mConfig.device.pcm_name).second) {
            candidates.push_back(mConfig.device.pcm_name);
        }

        // 回退到自动枚举的 hw: 设备（枚举时已排除黑名单）
        for (const auto& name : EnumerateCaptureDevices()) {
            if (seen.insert(name).second) {
                candidates.push_back(name);
            }
        }
        return candidates;
    }

    std::vector<uint32_t> RecordHAL::GetCandidateSampleRates() const {
        return BuildCandidates<uint32_t>(mConfig.format.sample_rate, {SampleRate48K, SampleRate32K, SampleRate16K});
    }

    std::vector<uint16_t> RecordHAL::GetCandidateChannels() const {
        return BuildCandidates<uint16_t>(mConfig.format.channels, {2, 1});
    }

    std::vector<uint16_t> RecordHAL::GetCandidateBitDepths() const {
        return BuildCandidates<uint16_t>(mConfig.format.bit_depth, {16});
    }

    std::vector<AudioFormatConfig> RecordHAL::GetCandidateFormats() const {
        std::vector<AudioFormatConfig> formats;
        const auto sampleRates = GetCandidateSampleRates();
        const auto channels = GetCandidateChannels();
        const auto bitDepths = GetCandidateBitDepths();

        for (const auto sampleRate : sampleRates) {
            for (const auto channelCount : channels) {
                for (const auto bitDepth : bitDepths) {
                    formats.emplace_back(AudioFormatConfig {
                        .sample_rate = sampleRate,
                        .channels = channelCount,
                        .bit_depth = bitDepth,
                    });
                }
            }
        }

        return formats;
    }

    void RecordHAL::LogOpenedDevice(const std::string& pcmName, const AudioFormatConfig& requestedFormat,
                                    const AudioFormatConfig& actualAudioFormat) const {
        SLOG_INFO << "PCM device opened: device='" << pcmName << "' requested_rate=" << requestedFormat.sample_rate
                  << " requested_channels=" << requestedFormat.channels
                  << " requested_bits=" << requestedFormat.bit_depth << " actual_rate=" << actualAudioFormat.sample_rate
                  << " actual_channels=" << actualAudioFormat.channels << " actual_bits=" << actualAudioFormat.bit_depth
                  << " period_frames=" << mPcmContext->frames << " frame_bytes=" << mPcmContext->frame_bytes;
    }

    bool RecordHAL::InitHwParams(std::unique_ptr<snd_pcm_hw_params_t, decltype(&snd_pcm_hw_params_free)>& hwParams) {
        const int err = snd_pcm_hw_params_any(mPcmContext->handle, hwParams.get());
        if (err < 0) {
            SLOG_ERROR << "Failed to init hw params: " << snd_strerror(err);
            return false;
        }

        return true;
    }

    bool RecordHAL::ApplyHwParams(std::unique_ptr<snd_pcm_hw_params_t, decltype(&snd_pcm_hw_params_free)>& hwParams) {
        int err = snd_pcm_hw_params(mPcmContext->handle, hwParams.get());
        if (err < 0) {
            SLOG_ERROR << "Failed to apply hw params: " << snd_strerror(err);
            return false;
        }

        int dir = 0;
        snd_pcm_hw_params_get_period_size(hwParams.get(), &mPcmContext->frames, &dir);

        unsigned int actualSampleRate = 0;
        snd_pcm_hw_params_get_rate(hwParams.get(), &actualSampleRate, &dir);

        unsigned int actualChannels = 0;
        snd_pcm_hw_params_get_channels(hwParams.get(), &actualChannels);

        snd_pcm_format_t actualFormat = SND_PCM_FORMAT_UNKNOWN;
        snd_pcm_hw_params_get_format(hwParams.get(), &actualFormat);
        const uint16_t actualBitDepth = ConvertBitDepth(actualFormat);

        {
            std::lock_guard<std::mutex> lock(mMutex);
            mActualAudioFormat = AudioFormatConfig {
                .sample_rate = actualSampleRate,
                .channels = static_cast<uint16_t>(actualChannels),
                .bit_depth = actualBitDepth,
            };
        }
        mPcmContext->frame_bytes =
            static_cast<size_t>(snd_pcm_format_physical_width(actualFormat) / 8) * actualChannels;

        err = snd_pcm_prepare(mPcmContext->handle);
        if (err < 0) {
            SLOG_ERROR << "Failed to prepare PCM: " << snd_strerror(err);
            return false;
        }

        return true;
    }

    void RecordHAL::CloseDevice() {
        if (mPcmContext && mPcmContext->handle) {
            snd_pcm_close(mPcmContext->handle);
            mPcmContext->handle = nullptr;
            SLOG_INFO << "PCM device closed";
        }

        ResetRuntimeAudioState();
    }

    snd_pcm_format_t RecordHAL::ConvertFormat(uint16_t bit_depth) {
        switch (bit_depth) {
            case 8:
                return SND_PCM_FORMAT_S8;
            case 16:
                return SND_PCM_FORMAT_S16_LE;
            case 24:
                return SND_PCM_FORMAT_S24_LE;
            case 32:
                return SND_PCM_FORMAT_S32_LE;
            default:
                return SND_PCM_FORMAT_UNKNOWN;
        }
    }

    uint16_t RecordHAL::ConvertBitDepth(snd_pcm_format_t format) const {
        const int physicalWidth = snd_pcm_format_physical_width(format);
        if (physicalWidth <= 0) {
            return 0;
        }

        return static_cast<uint16_t>(physicalWidth);
    }

    bool
    RecordHAL::ConfigureAudioFormat(std::unique_ptr<snd_pcm_hw_params_t, decltype(&snd_pcm_hw_params_free)>& hwParams,
                                    const AudioFormatConfig& format, unsigned int& actualSampleRate) {
        int err = snd_pcm_hw_params_set_access(mPcmContext->handle, hwParams.get(), SND_PCM_ACCESS_RW_INTERLEAVED);
        if (err < 0) {
            SLOG_ERROR << "Failed to set access type: " << snd_strerror(err);
            return false;
        }

        snd_pcm_format_t pcmFormat = ConvertFormat(format.bit_depth);
        if (pcmFormat == SND_PCM_FORMAT_UNKNOWN) {
            SLOG_ERROR << "Unsupported bit depth: " << format.bit_depth;
            return false;
        }

        err = snd_pcm_hw_params_set_format(mPcmContext->handle, hwParams.get(), pcmFormat);
        if (err < 0) {
            SLOG_ERROR << "Failed to set format: " << snd_strerror(err);
            return false;
        }

        actualSampleRate = format.sample_rate;
        err = snd_pcm_hw_params_set_rate_near(mPcmContext->handle, hwParams.get(), &actualSampleRate, nullptr);
        if (err < 0) {
            SLOG_ERROR << "Failed to set sample rate: " << snd_strerror(err);
            return false;
        }

        if (actualSampleRate != format.sample_rate) {
            SLOG_WARN << "Sample rate adjusted from " << format.sample_rate << " to " << actualSampleRate;
        }

        unsigned int actualChannels = format.channels;
        err = snd_pcm_hw_params_set_channels_near(mPcmContext->handle, hwParams.get(), &actualChannels);
        if (err < 0) {
            SLOG_ERROR << "Failed to set channels: " << snd_strerror(err);
            return false;
        }

        return true;
    }

    bool
    RecordHAL::ConfigureBufferLayout(std::unique_ptr<snd_pcm_hw_params_t, decltype(&snd_pcm_hw_params_free)>& hwParams,
                                     const AudioFormatConfig& format) {
        snd_pcm_uframes_t periodSize =
            static_cast<snd_pcm_uframes_t>(format.sample_rate * mConfig.behavior.latency_ms / 1000);
        if (periodSize < 64) {
            periodSize = 64;
        }

        int dir = 0;
        int err = snd_pcm_hw_params_set_period_size_near(mPcmContext->handle, hwParams.get(), &periodSize, &dir);
        if (err < 0) {
            SLOG_ERROR << "Failed to set period size: " << snd_strerror(err);
            return false;
        }

        snd_pcm_uframes_t bufferSize = periodSize * 4;
        err = snd_pcm_hw_params_set_buffer_size_near(mPcmContext->handle, hwParams.get(), &bufferSize);
        if (err < 0) {
            SLOG_ERROR << "Failed to set buffer size: " << snd_strerror(err);
            return false;
        }

        return true;
    }

    void RecordHAL::ResetRuntimeAudioState() {
        std::lock_guard<std::mutex> lock(mMutex);
        mActualAudioFormat = AudioFormatConfig {};
        mActivePcmName.clear();
        if (mPcmContext) {
            mPcmContext->frames = 0;
            mPcmContext->frame_bytes = 0;
        }
    }

    bool RecordHAL::RecoverPcm(int err) {
        if (!mPcmContext || !mPcmContext->handle) {
            return false;
        }

        // XRUN # 未及时读取数据导致缓冲区溢出
        if (err == -EPIPE) {
            SLOG_WARN << "PCM overrun detected, recovering...";
            int recoverErr = snd_pcm_prepare(mPcmContext->handle);
            if (recoverErr < 0) {
                SLOG_ERROR << "Failed to recover from overrun: " << snd_strerror(recoverErr);
                return false;
            }
            return true;
        }

        // ENODEV # 设备被拔出
        if (err == -ENODEV) {
            SLOG_ERROR << "PCM device not found, device may be removed";
            return false;
        }

        // ESTRPIPE # 设备暂停
        if (err == -ESTRPIPE) {
            SLOG_WARN << "PCM suspend detected, recovering...";
            int recoverErr = snd_pcm_resume(mPcmContext->handle);
            if (recoverErr == -EAGAIN) {
                return false;
            }
            if (recoverErr < 0) {
                recoverErr = snd_pcm_prepare(mPcmContext->handle);
                if (recoverErr < 0) {
                    SLOG_ERROR << "Failed to recover from suspend: " << snd_strerror(recoverErr);
                    return false;
                }
            }
            return true;
        }

        SLOG_ERROR << "Unrecoverable PCM error: " << snd_strerror(err);
        return false;
    }

}  // namespace qifeng
