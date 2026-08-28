//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <filesystem>
#include <fstream>

#include "aas/aas_callback.h"
#include "qifeng_framework/aas/audio_load.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"

#include "common/audio/audio_utils.h"
#include "common/config/hal_config.h"
#include "common/config/voiceprint_config.h"
#include "common/utils/file_name_generator.h"
#include "common/utils/file_opt.h"
#include "common/utils/symlink_manager.h"
#include "internal/hal/hal_bridge.h"
#include "schedule/task/voiceprint_segment_task.h"

namespace qifeng_ca {

    VoiceprintSegmentTask::VoiceprintSegmentTask(const std::string &audioId, uint64_t accountId, uint64_t fileAccoutId)
        : BaseTask(audioId, accountId) {
        mFileAccoutId = fileAccoutId == 0 ? accountId : fileAccoutId;
    }

    VoiceprintSegmentTask::~VoiceprintSegmentTask() = default;

    void VoiceprintSegmentTask::Start() {
        SetRunning(true);
        if (mSegments.empty()) {
            SLOG_ERROR << "VoiceprintSegmentTask: no segments provided";
            NotifyResult({Status {-1, "未提供音频时间段"}, {}, ""});
            return;
        }
        if (!ExtractAndMergeSegments(mPcmBuffer)) {
            SetComplete();
            NotifyResult({Status {-1, "音频分段读取失败"}, {}, ""});
            return;
        }
        if (mPcmBuffer.empty()) {
            SetComplete();
            NotifyResult({Status {-1, "合并后音频数据为空"}, {}, ""});
            return;
        }
        SubmitToAas();
    }

    void VoiceprintSegmentTask::Stop() {
        mRunning.store(false);
    }

    bool VoiceprintSegmentTask::OpenOutputWavFile(const AudioUtilsConfig &config) {
        auto filePath = GetVoiceprintFileName(mAccountId, mAudioId);
        if (!FileOpt::CreateDstDirectory(filePath)) {
            SLOG_ERROR << "VoiceprintTask: create voiceprint dir failed, path=" << filePath;
            return false;
        }

        mFilePath = filePath;

        mOutputWavFile.open(mFilePath, std::ios::binary);
        if (!mOutputWavFile.is_open()) {
            SLOG_ERROR << "VoiceprintTask: open wav file failed, path=" << mFilePath;
            mFilePath.clear();
            return false;
        }

        AudioUtils::WriteWavHeader(mOutputWavFile, config);
        return true;
    }

    void VoiceprintSegmentTask::FinalizeOutputWavFile() {
        if (!mOutputWavFile.is_open()) {
            return;
        }
        AudioUtils::FinalizeWavFile(mOutputWavFile, mOutputWavSize);
        mOutputWavFile.close();
    }

    bool VoiceprintSegmentTask::ExtractAndMergeSegments(std::vector<uint8_t> &outPcm) {
        // 获取WAV文件路径
        auto &symMgr = SymlinkManager::GetInstance();
        std::string wavPath = symMgr.ResolveData2Path(GetAudioFilePath(mFileAccoutId, mAudioId));
        if (!std::filesystem::exists(wavPath)) {
            SLOG_ERROR << "VoiceprintSegmentTask: wav file not found, path=" << wavPath;
            return false;
        }

        // 解析WAV头获取格式信息
        WavHeaderInfo headerInfo;
        if (!AudioUtils::ParseWavHeader(wavPath, headerInfo)) {
            SLOG_ERROR << "VoiceprintSegmentTask: parse wav header failed";
            return false;
        }
        mWavSampleRate = headerInfo.mSampleRate;
        mWavChannels = headerInfo.mChannels;
        mWavBitDepth = headerInfo.mBitDepth;

        // if (!OpenOutputWavFile({static_cast<uint32_t>(mWavSampleRate), static_cast<uint16_t>(mWavChannels),
        //                         static_cast<uint16_t>(mWavBitDepth)})) {
        //     return false;
        // }
        AudioUtilsConfig config {static_cast<uint32_t>(headerInfo.mSampleRate),
                                 static_cast<uint16_t>(headerInfo.mChannels),
                                 static_cast<uint16_t>(headerInfo.mBitDepth)};
        size_t bytesPerMs = AudioUtils::CalculateBytesPerMs(config);
        size_t maxBytes = static_cast<size_t>(MaxVoiceprintDurationSec) * 1000 * bytesPerMs;
        // ScopeExit finalizeWav([this]() { FinalizeOutputWavFile(); });

        // 逐段读取并合并PCM数据
        outPcm.clear();
        for (const auto &seg : mSegments) {
            std::vector<uint8_t> segPcm;
            if (!ReadSegmentPcm(seg, segPcm)) {
                SLOG_WARN << "VoiceprintSegmentTask: read segment failed, start=" << seg.mStartMs
                          << " end=" << seg.mEndMs;
                continue;
            }
            // mOutputWavFile.write(reinterpret_cast<const char*>(segPcm.data()),  // NOLINT
            //                      static_cast<std::streamsize>(segPcm.size()));
            // mOutputWavSize += static_cast<uint32_t>(segPcm.size());
            if (outPcm.size() < maxBytes) {
                outPcm.insert(outPcm.end(), segPcm.begin(), segPcm.end());
            }
        }

        SLOG_INFO << "VoiceprintSegmentTask: merged segments, totalPcmSize=" << outPcm.size();
        return !outPcm.empty();
    }

    bool VoiceprintSegmentTask::ReadSegmentPcm(const AudioSegment &seg, std::vector<uint8_t> &outPcm) {
        auto &symMgr = SymlinkManager::GetInstance();
        std::string wavPath = symMgr.ResolveData2Path(GetAudioFilePath(mFileAccoutId, mAudioId));
        std::ifstream file(wavPath, std::ios::binary);
        if (!file.is_open()) {
            SLOG_ERROR << "VoiceprintSegmentTask: cannot open wav file";
            return false;
        }

        // 计算字节偏移: 跳过WAV头(44字节), 按时间定位
        AudioUtilsConfig wavCfg {static_cast<uint32_t>(mWavSampleRate), static_cast<uint16_t>(mWavChannels),
                                 static_cast<uint16_t>(mWavBitDepth)};
        size_t bytesPerMs = AudioUtils::CalculateBytesPerMs(wavCfg);

        size_t startOffset =
            static_cast<size_t>(AudioUtils::WavHeaderSize) + static_cast<size_t>(seg.mStartMs) * bytesPerMs;
        int32_t durationMs = seg.mEndMs - seg.mStartMs;
        if (durationMs <= 0) {
            return false;
        }
        size_t readSize = static_cast<size_t>(durationMs) * bytesPerMs;

        file.seekg(static_cast<std::streamoff>(startOffset), std::ios::beg);
        outPcm.resize(readSize);
        file.read(reinterpret_cast<char*>(outPcm.data()), static_cast<std::streamsize>(readSize));  // NOLINT
        auto bytesRead = static_cast<size_t>(file.gcount());
        outPcm.resize(bytesRead);
        file.close();

        return !outPcm.empty();
    }

    void VoiceprintSegmentTask::SubmitToAas() {
        auto self = Self();
        qifeng::aas::ResultInfo info;
        info.mData = std::move(mPcmBuffer);
        info.mConfig.mBitDepth = static_cast<uint16_t>(mWavBitDepth);
        info.mConfig.mChannels = static_cast<uint16_t>(mWavChannels);
        info.mConfig.mSampleRate = static_cast<uint32_t>(mWavSampleRate);
        info.mBmsInfo.isOnline = false;

        // 设置AAS回调: StartSVJob完成后异步调用, 传回声纹特征
        info.mCallBack = [self](qifeng::aas::AasResult aasResult, qifeng::aas::BmsInfo) {
            self->OnAasResult(std::move(aasResult));
        };

        int ret = qifeng::aas::StartVoiceprintRegister(info);
        if (ret != 0) {
            SLOG_ERROR << "VoiceprintSegmentTask: StartSVJob failed, ret=" << ret;
            SetComplete();
            NotifyResult({Status {-1, "声纹任务启动失败"}, {}, ""});
            return;
        }
        SLOG_INFO << "VoiceprintSegmentTask: submitted to AAS, audioId=" << mAudioId;
    }

    void VoiceprintSegmentTask::OnAasResult(qifeng::aas::AasResult aasResult) {
        SetComplete();
        SLOG_INFO << "VoiceprintSegmentTask: AAS callback, code=" << aasResult.code;

        if (aasResult.code != 0) {
            NotifyResult({Status {aasResult.code, aasResult.message}, {}, ""});
            return;
        }

        VoiceprintResult vpResult;
        vpResult.mStatus = Status {};
        vpResult.mSvEmbedding = std::move(aasResult.svEmbedding);
        vpResult.mSvEmbeddingMd5 = std::move(aasResult.svEmbeddingMd5);
        vpResult.mFilePath = mFilePath;
        SLOG_DEBUG << "VoiceprintSegmentTask: AAS callback success, filePath=" << vpResult.mFilePath
                   << " svEmbeddingMd5=" << vpResult.mSvEmbeddingMd5
                   << " svEmbeddingSize=" << vpResult.mSvEmbedding.size();
        NotifyResult(vpResult);
    }

    void VoiceprintSegmentTask::NotifyResult(const VoiceprintResult &result) {
        if (mCallback) {
            mCallback(result);
        }
    }

}  // namespace qifeng_ca
