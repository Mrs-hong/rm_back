//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <filesystem>
#include <system_error>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/utils/dual_wav_writer.h"
#include "common/utils/file_opt.h"

namespace qifeng_ca {

    DualWavWriter::~DualWavWriter() {
        if (IsOpen()) {
            Finalize();
        }
    }

    bool DualWavWriter::OpenSingleFile(const std::string &path, std::ofstream &file) {
        if (!FileOpt::CreateDstDirectory(path)) {
            SLOG_ERROR << "DualWavWriter: create dir failed, path=" << path;
            return false;
        }

        bool fileExists = std::filesystem::exists(path);
        auto openMode = std::ios::binary | std::ios::out;
        if (fileExists) {
            openMode |= std::ios::in;
        }

        file.open(path, openMode);
        if (!file.is_open()) {
            SLOG_ERROR << "DualWavWriter: open failed, path=" << path;
            return false;
        }
        if (fileExists) {
            file.seekp(0);
        }
        AudioUtils::WriteWavHeader(file, mAudioConfig);
        return true;
    }

    bool DualWavWriter::Open(const std::string &primaryPath, const AudioUtilsConfig &audioConfig) {
        mPrimaryPath = primaryPath;
        mAudioConfig = audioConfig;

        if (!OpenSingleFile(mPrimaryPath, mPrimaryFile)) {
            return false;
        }
        SLOG_INFO << "DualWavWriter: primary opened, path=" << mPrimaryPath;

        // /data2可用时打开软连接副本
        auto &symMgr = SymlinkManager::GetInstance();
        if (symMgr.IsData2Available()) {
            mSymlinkPath = symMgr.ToSymlinkPath(mPrimaryPath);
            if (!OpenSingleFile(mSymlinkPath, mSymlinkFile)) {
                SLOG_WARN << "DualWavWriter: symlink open failed, path=" << mSymlinkPath;
            } else {
                SLOG_INFO << "DualWavWriter: symlink opened, path=" << mSymlinkPath;
            }
        }
        return true;
    }

    bool DualWavWriter::Write(const uint8_t* data, size_t len) {
        if (!IsOpen()) {
            return false;
        }

        mPrimaryFile.write(reinterpret_cast<const char*>(data),
                           static_cast<std::streamsize>(len));  // NOLINT
        mDataSize += static_cast<uint64_t>(len);

        if (mSymlinkFile.is_open()) {
            mSymlinkFile.write(reinterpret_cast<const char*>(data),
                               static_cast<std::streamsize>(len));  // NOLINT
            mSymlinkDataSize += static_cast<uint32_t>(len);
        }
        return true;
    }

    void DualWavWriter::FinalizeSingleFile(std::ofstream &file, uint32_t dataSize) {
        if (!file.is_open()) {
            return;
        }
        AudioUtils::FinalizeWavFile(file, dataSize);
        file.close();
    }

    bool DualWavWriter::Finalize() {
        FinalizeSingleFile(mPrimaryFile, mDataSize);
        if (mSymlinkFile.is_open()) {
            FinalizeSingleFile(mSymlinkFile, mSymlinkDataSize);
        }
        SLOG_INFO << "DualWavWriter: finalized, dataSize=" << mDataSize << " symlinkDataSize=" << mSymlinkDataSize;
        return true;
    }

}  // namespace qifeng_ca
