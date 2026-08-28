//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_UTILS_DUAL_WAV_WRITER_H
#define QIFENG_CA_INCLUDE_COMMON_UTILS_DUAL_WAV_WRITER_H

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

#include "common/audio/audio_utils.h"
#include "common/utils/symlink_manager.h"

namespace qifeng_ca {

    // 双写WAV文件管理器: 同时写入主路径(data)和软连接路径(data/src)
    // 封装了打开、写入、结束写入等操作
    class DualWavWriter {
    public:
        DualWavWriter() = default;
        ~DualWavWriter();

        DualWavWriter(const DualWavWriter &) = delete;
        DualWavWriter &operator=(const DualWavWriter &) = delete;
        DualWavWriter(DualWavWriter &&) = delete;
        DualWavWriter &operator=(DualWavWriter &&) = delete;

        // 打开主路径和软连接路径的WAV文件
        // audioConfig: 用于写入WAV头
        bool Open(const std::string &primaryPath, const AudioUtilsConfig &audioConfig);

        // 写入音频数据(同时写入两路)
        bool Write(const uint8_t* data, size_t len);

        // 结束写入: 回填WAV头并关闭文件
        bool Finalize();

        // 获取主路径数据量
        uint64_t GetDataSize() const { return mDataSize; }

        // 获取主文件路径
        const std::string &GetPrimaryPath() const { return mPrimaryPath; }

        // 获取软连接文件路径(仅在/data2可用时有值)
        const std::string &GetSymlinkPath() const { return mSymlinkPath; }

        // 是否已打开
        bool IsOpen() const { return mPrimaryFile.is_open(); }

        // 软连接文件是否已打开
        bool IsSymlinkOpen() const { return mSymlinkFile.is_open(); }

    private:
        // 打开单路WAV文件
        bool OpenSingleFile(const std::string &path, std::ofstream &file);

        // 结束单路WAV文件
        void FinalizeSingleFile(std::ofstream &file, uint32_t dataSize);

    private:
        std::string mPrimaryPath;
        std::ofstream mPrimaryFile;
        uint64_t mDataSize {0};

        std::string mSymlinkPath;
        std::ofstream mSymlinkFile;
        uint32_t mSymlinkDataSize {0};

        AudioUtilsConfig mAudioConfig {};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_UTILS_DUAL_WAV_WRITER_H
