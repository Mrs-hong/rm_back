//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "qifeng_framework/common/logger.h"

#include "aas/aas_callback.h"
#include "common/audio/audio_utils.h"

namespace qifeng_ca {

    struct ResampleContext {
        std::ifstream &mInFile;
        std::ofstream &mOutFile;
        qifeng::aas::FormatConfig mSrcFmt;
        qifeng::aas::FormatConfig mDstFmt;
        uint32_t &mTotalDataSize;
        size_t mChunkSize {};
    };

    struct ResampleJob {
        std::string mDstPath;
        std::ifstream &mInFile;
        std::ofstream &mOutFile;
        const WavHeaderInfo &mHeaderInfo;
        const AudioUtilsConfig &mDstConfig;
    };

    // WAV 常见规范: 采样率 8000/16000/22050/24000/32000/44100/48000, 通道 1/2, 位深 8/16/24/32
    static bool IsWavParamsValid(int sampleRate, int channels, int bitDepth) {
        switch (sampleRate) {
            case 8000:
            case 16000:
            case 22050:
            case 24000:
            case 32000:
            case 44100:
            case 48000:
                break;
            default:
                return false;
        }
        if (channels != 1 && channels != 2) {
            return false;
        }
        switch (bitDepth) {
            case 8:
            case 16:
            case 24:
            case 32:
                return true;
            default:
                return false;
        }
    }

    // header 中 data size 为 0 或与实际文件大小不符时, 用实际文件数据大小兜底
    static void AdjustDataSizeIfNeeded(WavHeaderInfo &info, uint64_t actualDataSize, const std::string &filePath) {
        if (info.mDataSize == 0 || info.mDataSize != actualDataSize) {
            SLOG_WARN << "AudioUtils: header dataSize=" << info.mDataSize << " replaced by actual=" << actualDataSize
                      << " path=" << filePath;
            info.mDataSize = actualDataSize;
        }
    }

    bool AudioUtils::IsWavFormatMatch(const std::string &filePath, const AudioUtilsConfig &targetConfig) {
        WavHeaderInfo info;
        if (!ParseWavHeader(filePath, info)) {
            return false;
        }

        bool rateMatch = (static_cast<uint32_t>(info.mSampleRate) == targetConfig.mSampleRate);
        bool chMatch = (static_cast<uint32_t>(info.mChannels) == targetConfig.mChannels);
        bool depthMatch = (static_cast<uint32_t>(info.mBitDepth) == targetConfig.mBitDepth);
        return rateMatch && chMatch && depthMatch;
    }

    bool AudioUtils::GetWavHeaderInfo(const std::string &filePath, WavHeaderInfo &outInfo) {
        if (!std::filesystem::exists(filePath)) {
            SLOG_ERROR << "AudioUtils: file not exist, path=" << filePath;
            return false;
        }

        // 获取实际文件大小, 用于兜底 header 中 data size 为 0 的情况
        uintmax_t fileSize = std::filesystem::file_size(filePath);
        if (fileSize < WavHeaderSize) {
            SLOG_ERROR << "AudioUtils: file too small for WAV header, path=" << filePath;
            return false;
        }

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            SLOG_ERROR << "AudioUtils: cannot open file, path=" << filePath;
            return false;
        }

        std::array<uint8_t, WavHeaderSize> header {};
        file.read(reinterpret_cast<char*>(header.data()), WavHeaderSize);  // NOLINT
        file.close();

        if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F') {
            SLOG_ERROR << "AudioUtils: not a WAV file, path=" << filePath;
            return false;
        }

        outInfo.mChannels = static_cast<int>(header[22]);
        outInfo.mSampleRate = static_cast<int>(header[24]) | (static_cast<int>(header[25]) << 8) |
                              (static_cast<int>(header[26]) << 16) | (static_cast<int>(header[27]) << 24);
        outInfo.mBitDepth = static_cast<int>(header[34]);
        outInfo.mDataSize = static_cast<uint32_t>(header[40]) | (static_cast<uint32_t>(header[41]) << 8) |
                            (static_cast<uint32_t>(header[42]) << 16) | (static_cast<uint32_t>(header[43]) << 24);

        // 如果 header 中 data size 为 0 或超过实际文件大小, 用实际文件数据大小兜底
        uint64_t actualDataSize = fileSize - WavHeaderSize;
        AdjustDataSizeIfNeeded(outInfo, actualDataSize, filePath);

        // 校验采样率、通道数、位深是否符合规范
        if (!IsWavParamsValid(outInfo.mSampleRate, outInfo.mChannels, outInfo.mBitDepth)) {
            SLOG_ERROR << "AudioUtils: invalid wav params, sampleRate=" << outInfo.mSampleRate
                       << " channels=" << outInfo.mChannels << " bitDepth=" << outInfo.mBitDepth
                       << " path=" << filePath;
            return false;
        }

        return true;
    }

    // 相比GetWavHeaderInfo增加了文件大小验证
    bool AudioUtils::ParseWavHeader(const std::string &filePath, WavHeaderInfo &outInfo) {
        if (!std::filesystem::exists(filePath)) {
            SLOG_ERROR << "AudioUtils: file not exist, path=" << filePath;
            return false;
        }

        auto fileSize = std::filesystem::file_size(filePath);
        if (fileSize <= WavHeaderSize) {
            SLOG_ERROR << "AudioUtils: file too small, size=" << fileSize;
            return false;
        }

        return GetWavHeaderInfo(filePath, outInfo);
    }

    bool AudioUtils::HasStandardWavHeader(const std::string &filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        std::array<char, 12> riff {};
        file.read(riff.data(), 12);
        if (!file || std::memcmp(riff.data(), "RIFF", 4) != 0 || std::memcmp(riff.data() + 8, "WAVE", 4) != 0) {
            return false;
        }
        // 遍历chunks, 判断data chunk是否起始于offset 36(即标准44字节头)
        size_t pos = 12;
        while (file) {
            std::array<char, 8> chunkHeader {};
            file.read(chunkHeader.data(), 8);
            if (!file) {
                return false;
            }
            uint32_t chunkSize = 0;
            std::memcpy(&chunkSize, chunkHeader.data() + 4, 4);
            if (std::memcmp(chunkHeader.data(), "data", 4) == 0) {
                return pos == 36;
            }
            // 跳过chunk数据(word aligned: 奇数size补1字节)
            size_t padded = (chunkSize + 1) & ~static_cast<uint32_t>(1);
            file.seekg(static_cast<std::streamoff>(padded), std::ios::cur);
            pos += 8 + padded;
        }
        return false;
    }

    void AudioUtils::WriteWavHeader(std::ostream &outFile, const AudioUtilsConfig &config) {  // NOLINT
        int byteRate = static_cast<int>(CalculateOneSecondBytes(config));
        int blockAlign = static_cast<int>(config.mChannels) * (static_cast<int>(config.mBitDepth) / 8);

        std::array<uint8_t, WavHeaderSize> hdr {};
        // RIFF 标识
        hdr[0] = 'R';
        hdr[1] = 'I';
        hdr[2] = 'F';
        hdr[3] = 'F';

        // 文件大小 = 44 (头) + 数据大小
        // 写入文件时补充
        hdr[4] = static_cast<uint8_t>(WavHeaderSize);
        hdr[5] = static_cast<uint8_t>(WavHeaderSize >> 8);
        hdr[6] = static_cast<uint8_t>(WavHeaderSize >> 16);
        hdr[7] = static_cast<uint8_t>(WavHeaderSize >> 24);

        // WAVE 标识
        hdr[8] = 'W';
        hdr[9] = 'A';
        hdr[10] = 'V';
        hdr[11] = 'E';

        // fmt 标识
        hdr[12] = 'f';
        hdr[13] = 'm';
        hdr[14] = 't';
        hdr[15] = ' ';

        // fmt 块大小
        hdr[16] = 16;  // PCM 格式
        hdr[17] = 0;
        hdr[18] = 0;
        hdr[19] = 0;

        // 格式类型 (PCM)
        hdr[20] = 1;
        hdr[21] = 0;

        // 通道数
        hdr[22] = static_cast<uint8_t>(config.mChannels);
        hdr[23] = 0;

        // 采样率
        hdr[24] = static_cast<uint8_t>(config.mSampleRate);
        hdr[25] = static_cast<uint8_t>(config.mSampleRate >> 8);
        hdr[26] = static_cast<uint8_t>(config.mSampleRate >> 16);
        hdr[27] = static_cast<uint8_t>(config.mSampleRate >> 24);

        // 字节率 = 采样率 * 通道数 * (位深度 / 8)
        hdr[28] = static_cast<uint8_t>(byteRate);
        hdr[29] = static_cast<uint8_t>(byteRate >> 8);
        hdr[30] = static_cast<uint8_t>(byteRate >> 16);
        hdr[31] = static_cast<uint8_t>(byteRate >> 24);

        // 块对齐 = 通道数 * (位深度 / 8)
        hdr[32] = static_cast<uint8_t>(blockAlign);
        hdr[33] = 0;

        // 位深度
        hdr[34] = static_cast<uint8_t>(config.mBitDepth);
        hdr[35] = 0;

        // data 标识
        hdr[36] = 'd';
        hdr[37] = 'a';
        hdr[38] = 't';
        hdr[39] = 'a';

        // 数据大小
        hdr[40] = 0;
        hdr[41] = 0;
        hdr[42] = 0;
        hdr[43] = 0;

        outFile.write(reinterpret_cast<const char*>(hdr.data()), WavHeaderSize);  // NOLINT
    }

    void AudioUtils::FinalizeWavFile(std::ostream &outFile, uint64_t dataSize) {
        uint64_t riffSize = WavHeaderSize + dataSize - 8;  // 减去头部的8字节
        uint32_t riffSize32 = static_cast<uint32_t>(riffSize);
        uint32_t dataSize32 = static_cast<uint32_t>(dataSize);

        outFile.seekp(4);
        outFile.put(static_cast<uint8_t>(riffSize32));
        outFile.put(static_cast<uint8_t>(riffSize32 >> 8));
        outFile.put(static_cast<uint8_t>(riffSize32 >> 16));
        outFile.put(static_cast<uint8_t>(riffSize32 >> 24));

        outFile.seekp(40);
        outFile.put(static_cast<uint8_t>(dataSize32));
        outFile.put(static_cast<uint8_t>(dataSize32 >> 8));
        outFile.put(static_cast<uint8_t>(dataSize32 >> 16));
        outFile.put(static_cast<uint8_t>(dataSize32 >> 24));
    }

    size_t AudioUtils::CalculateOneSecondBytes(const AudioUtilsConfig &config) {
        // 4*192k(48000 * 2 * 16 / 2) 字节，4倍大小作为硬上限
        // 按照 60s 计算最多使用45M内存，内存可控
        static constexpr size_t MaxOneSecondBytes = 4L * 192000;
        if (config.mSampleRate == 0 || config.mChannels == 0 || config.mBitDepth == 0) {
            return 0;
        }
        size_t bytesPerSecond = static_cast<size_t>(config.mSampleRate) * static_cast<size_t>(config.mChannels) *
                                (static_cast<size_t>(config.mBitDepth) / 8);
        return std::min(bytesPerSecond, MaxOneSecondBytes);
    }

    size_t AudioUtils::CalculateBytesPerMs(const AudioUtilsConfig &config) {
        return CalculateOneSecondBytes(config) / 1000;
    }

    int32_t AudioUtils::CalculateDurationMs(size_t dataSize, const AudioUtilsConfig &config) {
        size_t bytesPerMs = CalculateBytesPerMs(config);
        if (bytesPerMs == 0) {
            return 0;
        }
        return static_cast<int32_t>(dataSize / bytesPerMs);
    }

    int32_t AudioUtils::CalculateTotalTimeFromWav(const std::string &wavPath) {
        WavHeaderInfo headerInfo;
        if (!AudioUtils::GetWavHeaderInfo(wavPath, headerInfo)) {
            return 0;
        }
        int bytesPerSample = headerInfo.mBitDepth / 8;
        int bytesPerFrame = bytesPerSample * headerInfo.mChannels;
        if (bytesPerFrame <= 0 || headerInfo.mSampleRate <= 0) {
            return 0;
        }
        return static_cast<int32_t>((static_cast<uint64_t>(headerInfo.mDataSize) * 1000) /
                                    (static_cast<uint64_t>(bytesPerFrame) * headerInfo.mSampleRate));
    }

}  // namespace qifeng_ca
