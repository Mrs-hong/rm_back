/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <cmath>

#include "common/utils/voice_energy.h"

namespace qifeng_ca {

    void VoiceEnergyCalculator::Init(int sampleRate, int channels, int bytesPerSample) {
        mSampleRate = sampleRate;
        mChannels = channels;
        mBytesPerSample = bytesPerSample / 8;
        mBitDepth = mBytesPerSample * 8;
        mFrameSamples = sampleRate * EnergyFrameDurationMs / 1000;
        mFrameBytes = mFrameSamples * mBytesPerSample * channels;
        mAccumulatedData.reserve(static_cast<size_t>(mFrameBytes) * 2);
    }

    void VoiceEnergyCalculator::ProcessAudioData(const uint8_t* data, size_t len) {
        if (data == nullptr || len == 0) {
            return;
        }
        mAccumulatedData.insert(mAccumulatedData.end(), data, data + len);

        while (static_cast<int>(mAccumulatedData.size()) >= mFrameBytes) {
            CalculateFrameEnergy(mAccumulatedData.data(), static_cast<size_t>(mFrameBytes));
            mAccumulatedData.erase(mAccumulatedData.begin(), mAccumulatedData.begin() + mFrameBytes);
        }
    }

    void VoiceEnergyCalculator::CalculateFrameEnergy(const uint8_t* data, size_t len) {
        int samples = static_cast<int>(len) / mBytesPerSample / mChannels;
        if (samples == 0) {
            return;
        }

        double sumSquares = 0.0;
        int sampleCount = 0;
        for (int i = 0; i < samples; ++i) {
            for (int ch = 0; ch < mChannels; ++ch) {
                int offset = (i * mChannels + ch) * mBytesPerSample;
                if (offset + mBytesPerSample > static_cast<int>(len)) {
                    break;
                }
                int32_t sample = DecodeSample(data + offset);
                sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
                ++sampleCount;
            }
        }

        if (sampleCount == 0) {
            return;
        }

        double rms = std::sqrt(sumSquares / static_cast<double>(sampleCount));
        double oldRms = mEnergyRingBuffer[static_cast<size_t>(mEnergyWriteIndex)];
        mEnergyRingBuffer[static_cast<size_t>(mEnergyWriteIndex)] = rms;

        int32_t normalized = NormalizeSingleFrame(rms);
        mNormalizedBuffer[static_cast<size_t>(mEnergyWriteIndex)] = normalized;

        if (rms > mMaxRms) {
            mMaxRms = rms;
        } else if (oldRms >= mMaxRms && oldRms > 0.0) {
            RecalculateMaxRms();
        }

        mEnergyWriteIndex = (mEnergyWriteIndex + 1) % EnergyFrameCount;
        if (mValidFrameCount < EnergyFrameCount) {
            ++mValidFrameCount;
        }
        ++mTotalFrameCount;
    }

    int32_t VoiceEnergyCalculator::DecodeSample(const uint8_t* data) const {
        int32_t sample = 0;
        if (mBytesPerSample == 1) {
            sample = static_cast<int32_t>(data[0]) - 128;
        } else if (mBytesPerSample == 2) {
            auto b0 = static_cast<int32_t>(data[0]);
            auto b1 = static_cast<int32_t>(data[1]);
            sample = b0 | (b1 << 8);
            if ((sample & 0x8000) != 0) {
                sample -= 0x10000;
            }
        } else if (mBytesPerSample == 3) {
            auto b0 = static_cast<uint32_t>(data[0]);
            auto b1 = static_cast<uint32_t>(data[1]);
            auto b2 = static_cast<uint32_t>(data[2]);
            uint32_t usample = b0 | (b1 << 8) | (b2 << 16);
            sample =
                ((usample & 0x800000) != 0) ? static_cast<int32_t>(usample) - 0x1000000 : static_cast<int32_t>(usample);
        } else if (mBytesPerSample == 4) {
            auto b0 = static_cast<int32_t>(data[0]);
            auto b1 = static_cast<int32_t>(data[1]);
            auto b2 = static_cast<int32_t>(data[2]);
            auto b3 = static_cast<int32_t>(data[3]);
            sample = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
        }
        return sample;
    }

    double VoiceEnergyCalculator::GetMaxSampleValue() const {
        switch (mBitDepth) {
            case 8:
                return 128.0;
            case 16:
                return 32768.0;
            case 24:
                return 8388608.0;
            case 32:
                return 2147483648.0;
            default:
                return 32768.0;
        }
    }

    int VoiceEnergyCalculator::NormalizeSingleFrame(double rms) const {
        if (rms < 1.0) {
            return 0;
        }
        double maxSampleValue = GetMaxSampleValue();
        double normalized = rms / maxSampleValue;

        int energyLevel = 0;
        if (normalized >= 0.0001) {
            double db = 20.0 * std::log10(normalized);
            if (db < -60.0) {
                energyLevel = 0;
            } else if (db > 0.0) {
                // TODO(yf): 调整大声音能量范围
                // // 超过 0dB 的部分继续映射，最多到 +20dB 对应 100
                // double extraDb = std::min(db, 20.0);
                // energyLevel = static_cast<int>(80.0 + extraDb);  // 80~100
                energyLevel = EnergyMaxValue;
            } else {
                // TODO(yf): 调整大声音能量范围
                // energyLevel = static_cast<int>(std::round((db + 60.0) / 60.0 * 80.0));  // 0~80
                energyLevel = static_cast<int>(std::round((db + 60.0) / 60.0 * EnergyMaxValue));
            }
        }

        if (energyLevel < 0) {
            energyLevel = 0;
        }
        if (energyLevel > EnergyMaxValue) {
            energyLevel = EnergyMaxValue;
        }
        return energyLevel;
    }

    void VoiceEnergyCalculator::RecalculateMaxRms() {
        mMaxRms = 0.0;
        for (int i = 0; i < mValidFrameCount; ++i) {
            int idx = (mEnergyWriteIndex + EnergyFrameCount - 1 - i) % EnergyFrameCount;
            if (mEnergyRingBuffer[idx] > mMaxRms) {
                mMaxRms = mEnergyRingBuffer[idx];
            }
        }
        for (int i = 0; i < mValidFrameCount; ++i) {
            int idx = (mEnergyWriteIndex + EnergyFrameCount - 1 - i) % EnergyFrameCount;
            mNormalizedBuffer[idx] = NormalizeSingleFrame(mEnergyRingBuffer[idx]);
        }
    }

    void VoiceEnergyCalculator::Reset() {
        mEnergyRingBuffer.fill(0.0);
        mNormalizedBuffer.fill(0);
        mEnergyWriteIndex = 0;
        mValidFrameCount = 0;
        mMaxRms = 0.0;
        mAccumulatedData.clear();
        mTotalFrameCount = 0;
    }

    std::vector<int32_t> VoiceEnergyCalculator::GetEnergyArray() const {
        std::vector<int32_t> result(static_cast<size_t>(EnergyFrameCount), 0);
        if (mValidFrameCount == 0) {
            return result;
        }
        int readIdx = mEnergyWriteIndex;
        for (int i = 0; i < mValidFrameCount; ++i) {
            readIdx = (readIdx + EnergyFrameCount - 1) % EnergyFrameCount;
            result[static_cast<size_t>(i)] = mNormalizedBuffer[readIdx];
        }
        return result;
    }

}  // namespace qifeng_ca
