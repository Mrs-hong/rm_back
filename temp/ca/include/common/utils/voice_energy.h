/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#ifndef QIFENG_CA_INCLUDE_COMMON_UTILS_VOICE_ENERGY_H
#define QIFENG_CA_INCLUDE_COMMON_UTILS_VOICE_ENERGY_H

#include <array>
#include <cstdint>
#include <vector>

namespace qifeng_ca {

    // 音频能量计算器(转的python原逻辑)
    // 使用RMS+环形缓冲区+dB归一化计算音频能量
    class VoiceEnergyCalculator {
    public:
        VoiceEnergyCalculator() = default;

        void Init(int sampleRate, int channels, int bytesPerSample);
        void ProcessAudioData(const uint8_t* data, size_t len);
        void Reset();
        std::vector<int32_t> GetEnergyArray() const;
        // 获取累计计算的总帧数(不受环形缓冲区容量限制, 用于统计本次新增帧数)
        int GetTotalFrameCount() const { return mTotalFrameCount; }

    private:
        void CalculateFrameEnergy(const uint8_t* data, size_t len);
        int32_t DecodeSample(const uint8_t* data) const;
        double GetMaxSampleValue() const;
        int NormalizeSingleFrame(double rms) const;
        void RecalculateMaxRms();

        static constexpr int EnergyFrameDurationMs = 100;
        static constexpr int EnergyWindowSeconds = 20;
        static constexpr int EnergyFrameCount = EnergyWindowSeconds * 1000 / EnergyFrameDurationMs;
        static constexpr int EnergyMaxValue = 100;

        int mSampleRate {48000};
        int mChannels {2};
        int mBytesPerSample {2};
        int mBitDepth {16};
        int mFrameSamples {0};
        int mFrameBytes {0};

        std::array<double, EnergyFrameCount> mEnergyRingBuffer {};
        int mEnergyWriteIndex {0};
        int mValidFrameCount {0};

        std::array<int32_t, EnergyFrameCount> mNormalizedBuffer {};
        double mMaxRms {0.0};

        std::vector<uint8_t> mAccumulatedData;

        // 累计计算的总帧数(不受环形缓冲区容量限制)
        int mTotalFrameCount {0};
    };

}  // namespace qifeng_ca
#endif  // QIFENG_CA_INCLUDE_COMMON_UTILS_VOICE_ENERGY_H
