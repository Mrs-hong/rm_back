//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <regex>

#include "qifeng_framework/common/logger.h"
#include "utf8/checked.h"

#include "common/audio/audio_hal_utils.h"
#include "common/common.h"
#include "common/config/voiceprint_config.h"
#include "common/status.h"
#include "common/utils/file_opt.h"
#include "common/voiceprint/feature_serialize.h"
#include "core/meeting/meeting_access.h"
#include "core/voiceprint/voiceprint_service.h"
#include "dao/models/bms_speaker.h"
#include "schedule/pcm/pcm_engine.h"
#include "schedule/task/voiceprint_segment_task.h"
#include "schedule/task/voiceprint_task.h"

namespace qifeng_ca {

    static SpeakerDao &GetSpeakerDao() {
        static SpeakerDao Instance;
        return Instance;
    }

    static Status ValidateVoiceprintFields(const std::string &number, const std::string &speakerName,
                                           const std::string &remark) {
        if (number.empty() || speakerName.empty()) {
            return Status {-1, "编号和姓名不能为空"};
        }

        auto &config = VoiceprintConfig::GetInstance();
        if (static_cast<size_t>(utf8::distance(number.begin(), number.end())) >
            static_cast<size_t>(config.GetMaxNumberLen())) {
            return Status {-1, "编号长度不能超过" + std::to_string(config.GetMaxNumberLen())};
        }
        if (static_cast<size_t>(utf8::distance(speakerName.begin(), speakerName.end())) >
            static_cast<size_t>(config.GetMaxSpeakerLen())) {
            return Status {-1, "姓名长度不能超过" + std::to_string(config.GetMaxSpeakerLen())};
        }
        // 姓名不能包含空白字符(空格、制表符、换行等)
        if (speakerName.find_first_of(" \t\r\n\v\f") != std::string::npos) {
            return Status {-1, "姓名不能包含空格"};
        }
        if (static_cast<size_t>(utf8::distance(remark.begin(), remark.end())) >
            static_cast<size_t>(config.GetMaxRemarkLen())) {
            return Status {-1, "备注长度不能超过" + std::to_string(config.GetMaxRemarkLen())};
        }

        try {
            std::regex numberPattern("^[a-zA-Z0-9]+$");
            if (!std::regex_match(number, numberPattern)) {
                return Status {-1, "编号只能包含字母和数字"};
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "ValidateVoiceprintFields regex failed: " << e.what();
            return Status {-1, "编号格式校验异常"};
        }
        return Status {};
    }

    static Status CheckDuplicateSpeaker(uint64_t accountId, SpeakerDao &dao, const std::string &number,
                                        const std::string &speakerName) {
        models::Speaker existing = dao.GetByNumber(accountId, number);
        if (existing.mId != 0) {
            return Status {-1, "声纹编号已存在"};
        }

        existing = dao.GetBySpeakerName(accountId, speakerName);
        if (existing.mId != 0) {
            return Status {-1, "说话人姓名已存在"};
        }
        return Status {};
    }

    Status VoiceprintService::RecordVoiceprint(const RecordVoiceprintRequest &req, RecordVoiceprintResponse* resp) {
        std::lock_guard<std::mutex> lock(mTaskMutex);

        // 同一用户调用就是重置任务
        if (mActiveRecordInfo.mAccountId == req.account_id()) {
            if (mActiveTask) {
                // 先取消任务(等待读循环退出), 再关闭 HAL, 防止 ReadAudio 访问已关闭的 PCM 设备
                PcmEngine::GetInstance().CancelTaskByAudioId(mActiveTask->GetAudioId());
                HalStopAudio();
            }
            mActiveTask.reset();
            mActiveRecordInfo.Reset();
        }

        // 检查是否已有声纹录制任务在运行或排队中(非同一用户的不允许覆盖)
        if (mActiveTask) {
            return Status {-1, "声纹录制任务已在运行"};
        }

        auto status = HalStartAudio("声纹录制中", req.speaker(), AudioType::Normal, false);
        if (!status.IsSuccess()) {
            return status;
        }

        // 构建录制上下文(包含业务字段, 避免回调参数超过4个)
        VoiceprintRecordInfo info;
        info.mAccountId = req.account_id();
        info.mNumber = req.number();
        info.mSpeaker = req.speaker();
        if (req.has_remark()) {
            info.mRemark = req.remark();
        }

        // 创建声纹录制任务, 通过PcmEngine提交到TaskScheduler统一调度
        std::string audioId = GenUUID();
        auto task = std::make_shared<VoiceprintTask>(audioId, info.mAccountId);
        task->SetCallback([this, info](const VoiceprintResult &result) {
            HalStopAudio();  // 关闭灯光等;
            OnVoiceprintRecorded(info, result);
        });
        PcmEngine::GetInstance().Submit(task);

        mActiveTask = task;
        mActiveRecordInfo = info;

        resp->set_file_name(audioId);
        SLOG_INFO << "RecordVoiceprint: task submitted, audioId=" << audioId;
        return {};
    }

    Status VoiceprintService::RollbackVoiceprint(const RollbackVoiceprintRequest &req, Empty* resp) {
        (void)resp;
        std::shared_ptr<VoiceprintTask> task;
        std::string audioId;
        {
            std::lock_guard<std::mutex> lock(mTaskMutex);
            if (!mActiveTask || mActiveTask->IsComplete()) {
                return Status {-1, "没有正在进行的声纹录制任务"};
            }
            task = mActiveTask;
            audioId = task->GetAudioId();
            mActiveTask.reset();
            mActiveRecordInfo.Reset();
        }

        // 先取消任务(等待读循环退出), 再关闭 HAL, 防止 ReadAudio 访问已关闭的 PCM 设备
        PcmEngine::GetInstance().CancelTaskByAudioId(audioId);
        HalStopAudio();

        SLOG_INFO << "RollbackVoiceprint: task cancelled, speaker=" << req.speaker() << " audioId=" << audioId;
        return {};
    }

    Status VoiceprintService::StopVoiceprint(const StopVoiceprintRequest &req, StopVoiceprintResponse* resp) {
        (void)req;
        (void)resp;

        std::shared_ptr<VoiceprintTask> task;
        {
            std::lock_guard<std::mutex> lock(mTaskMutex);
            if (!mActiveTask) {
                return {};
            }
            if (mActiveRecordInfo.mAccountId != req.account_id()) {
                return Status {-1, "声纹录制任务与请求账户不匹配"};
            }
            task = mActiveTask;
        }

        auto st = HandleInactiveTask(task);
        if (st.has_value()) {
            return *st;
        }

        // 任务运行中: StopAndWait 会等待读循环退出后再触发 AAS
        VoiceprintResult result = task->StopAndWait();
        // 读循环已退出, 安全关闭 HAL
        HalStopAudio();
        CleanupActiveTask();
        SLOG_INFO << "StopVoiceprint: success, audioId=" << task->GetAudioId();
        return result.mStatus;
    }

    std::optional<Status> VoiceprintService::HandleInactiveTask(const std::shared_ptr<VoiceprintTask> &task) {
        // 已完成(IsComplete): AAS回调已写DB, 直接返回
        if (task->IsComplete()) {
            SLOG_INFO << "StopVoiceprint: task already completed, audioId=" << task->GetAudioId();
            return Status {};
        }
        // 已超时触发等AAS回调(IsWaitingForAas): StopAndWait 等待AAS结果写DB, 不取消
        if (task->IsWaitingForAas()) {
            SLOG_INFO << "StopVoiceprint: waiting for AAS callback, audioId=" << task->GetAudioId();
            VoiceprintResult result = task->StopAndWait();
            HalStopAudio();
            CleanupActiveTask();
            SLOG_INFO << "StopVoiceprint: aas callback done, audioId=" << task->GetAudioId()
                      << " status=" << result.mStatus.ToString();
            return result.mStatus;
        }
        // 队列中尚未启动(!IsRunning): 无录制数据, 直接取消
        if (!task->IsRunning()) {
            SLOG_INFO << "StopVoiceprint: task still in queue, cancelling, audioId=" << task->GetAudioId();
            // 先取消任务, 再关闭 HAL, 防止 ReadAudio 访问已关闭的 PCM 设备
            PcmEngine::GetInstance().CancelTaskByAudioId(task->GetAudioId());
            HalStopAudio();
            CleanupActiveTask();
            return Status {};
        }
        // 运行态: 交由调用方走 StopAndWait 主流程
        return std::nullopt;
    }

    void VoiceprintService::CleanupActiveTask() {
        std::lock_guard<std::mutex> lock(mTaskMutex);
        mActiveTask.reset();
        mActiveRecordInfo.Reset();
    }

    Status VoiceprintService::SaveVoiceprintToDb(const VoiceprintRecordInfo &info, const VoiceprintResult &result) {
        if (!result.mStatus.IsSuccess()) {
            return result.mStatus;
        }

        models::Speaker sp;
        sp.mAccountId = info.mAccountId;
        sp.mNumber = info.mNumber;
        sp.mSpeakerName = info.mSpeaker;
        sp.mRemark = info.mRemark;
        sp.mFileName = result.mFilePath;
        sp.mFeatures = SerializeEmbedding(result.mSvEmbedding);
        sp.mDim = static_cast<int32_t>(result.mSvEmbedding.size());
        sp.mRecordingTime = static_cast<int64_t>(GetTimeMs());
        sp.mLastModifyTime = sp.mRecordingTime;
        sp.mStatus = 1;
        sp.mSpeakerId = result.mSvEmbeddingMd5;

        if (!GetSpeakerDao().Insert(sp)) {
            return Status {-1, "声纹保存数据库失败"};
        }
        return Status {};
    }

    void VoiceprintService::OnVoiceprintRecorded(const VoiceprintRecordInfo &info, const VoiceprintResult &result) {
        if (!result.mStatus.IsSuccess()) {
            SLOG_ERROR << "OnVoiceprintRecorded: voiceprint extraction failed"
                       << " status=" << result.mStatus.ToString();
            return;
        }

        // 超时/取消自动完成: 直接写入数据库
        Status dbStatus = SaveVoiceprintToDb(info, result);
        if (dbStatus.GetCode() != 0) {
            SLOG_ERROR << "OnVoiceprintRecorded: save failed, status=" << dbStatus.ToString();
        }
    }

    void VoiceprintService::OnSegmentVoiceprintResult(const VoiceprintRecordInfo &info,
                                                      const VoiceprintResult &result) {
        // 分段声纹提取完成回调: 将AAS返回的声纹特征写入数据库
        if (!result.mStatus.IsSuccess()) {
            SLOG_ERROR << "OnSegmentVoiceprintResult: voiceprint extraction failed"
                       << " status=" << result.mStatus.ToString();
            return;
        }

        Status dbStatus = SaveVoiceprintToDb(info, result);
        if (dbStatus.GetCode() != 0) {
            SLOG_ERROR << "OnSegmentVoiceprintResult: save failed, status=" << dbStatus.ToString();
        }
    }

    Status VoiceprintService::CheckVoiceprintInfo(const CheckVoiceprintInfoRequest &req, Empty* resp) {
        (void)resp;
        uint64_t accountId = req.account_id();

        Status validStatus = ValidateVoiceprintFields(req.number(), req.speaker(), "");
        if (validStatus.GetCode() != 0) {
            return validStatus;
        }

        return CheckDuplicateSpeaker(accountId, GetSpeakerDao(), req.number(), req.speaker());
    }

    // 将proto Segment转为AudioSegment(过滤无效时间段)
    static std::vector<AudioSegment> ConvertSegments(const AddVoiceprintRequest &req) {
        std::vector<AudioSegment> segments;
        for (const auto &seg : req.segments()) {
            AudioSegment audioSeg;
            audioSeg.mStartMs = seg.start();
            audioSeg.mEndMs = seg.end();
            if (audioSeg.mEndMs <= audioSeg.mStartMs) {
                SLOG_WARN << "ConvertSegments: invalid segment, start=" << audioSeg.mStartMs
                          << " end=" << audioSeg.mEndMs;
                continue;
            }
            segments.push_back(audioSeg);
        }
        return segments;
    }

    Status VoiceprintService::AddVoiceprint(const AddVoiceprintRequest &req, Empty* resp) {
        (void)resp;
        uint64_t accountId = req.account_id();
        std::string remark = req.has_remark() ? req.remark() : "";

        Status validStatus = ValidateVoiceprintFields(req.number(), req.speaker(), remark);
        if (validStatus.GetCode() != 0) {
            return validStatus;
        }
        Status dupStatus = CheckDuplicateSpeaker(accountId, GetSpeakerDao(), req.number(), req.speaker());
        if (dupStatus.GetCode() != 0) {
            return dupStatus;
        }

        // 校验必须提供audioId和至少一个时间段
        if (req.audio_id().empty()) {
            return Status {-1, "audio_id不能为空"};
        }
        // RBAC 权限校验
        auto [audio, status] = GetAccessibleAudio(accountId, req.audio_id());
        if (status.GetCode() != 0) {
            return status;
        }
        if (req.segments_size() == 0) {
            return Status {-1, "至少需要一个音频时间段"};
        }

        // 构建分段信息(从proto Segment转为AudioSegment)
        std::vector<AudioSegment> segments = ConvertSegments(req);
        if (segments.empty()) {
            return Status {-1, "没有有效的时间段"};
        }

        // 构建回调上下文(避免函数参数超过4个)
        VoiceprintRecordInfo info;
        info.mAccountId = accountId;
        info.mSpeaker = req.speaker();
        info.mRemark = remark;
        info.mNumber = req.number();

        // 创建分段声纹提取任务, 通过PcmEngine提交到TaskScheduler统一调度
        // (VoiceprintSegmentTask为IsTemporaryConcurrent, 可与其他任务并行, 不受槽位限制)
        auto task = std::make_shared<VoiceprintSegmentTask>(req.audio_id(), accountId, audio.mAccountId);
        task->SetSegments(segments);
        task->SetCallback([this, info](const VoiceprintResult &result) { OnSegmentVoiceprintResult(info, result); });
        PcmEngine::GetInstance().Submit(task);

        SLOG_INFO << "AddVoiceprint: segment task submitted, audioId=" << req.audio_id()
                  << " segments=" << segments.size();
        return Status {};
    }

    void VoiceprintService::FillSearchResponse(const SpeakerSearchResult &result, VoiceprintSearchResponse* resp) {
        resp->set_total(result.mTotal);
        for (const auto &r : result.mRecords) {
            auto* item = resp->add_records();
            item->set_id(r.mId);
            if (!r.mNumber.empty()) {
                item->set_number(r.mNumber);
            }
            if (r.mRecordingTime != 0) {
                item->set_recording_time(r.mRecordingTime);
            }
            if (!r.mFileName.empty()) {
                item->set_file_name(r.mFileName);
            }
            if (!r.mSpeakerName.empty()) {
                item->set_speaker(r.mSpeakerName);
            }
            if (!r.mRemark.empty()) {
                item->set_remark(r.mRemark);
            }
            if (r.mStatus != 0) {
                item->set_status(r.mStatus);
            }
        }
    }

    Status VoiceprintService::GetVoiceprintList(const VoiceprintSearchRequest &req, VoiceprintSearchResponse* resp) {
        if (req.page_size() <= 0 || req.page_size() > 100) {
            return Status {-1, "page_size参数异常，范围1-100"};
        }
        if (req.current() <= 0) {
            return Status {-1, "current参数异常"};
        }

        SpeakerSearchFilter filter;
        filter.mAccountId = req.account_id();
        filter.mCurrent = req.current();
        filter.mPageSize = req.page_size();
        if (req.has_keyword()) {
            filter.mKeyword = SanitizeKeyword(req.keyword());
        }

        SpeakerSearchResult result = GetSpeakerDao().Search(filter);
        FillSearchResponse(result, resp);
        return Status {};
    }

    Status VoiceprintService::EditVoiceprint(const EditVoiceprintRequest &req, Empty* resp) {
        (void)resp;
        uint64_t accountId = req.account_id();
        std::string remark = req.has_remark() ? req.remark() : "";

        Status validStatus = ValidateVoiceprintFields(req.number(), req.speaker(), remark);
        if (validStatus.GetCode() != 0) {
            return validStatus;
        }

        models::Speaker sp = GetSpeakerDao().GetById(accountId, req.id());
        if (sp.mId == 0) {
            return Status {-1, "声纹不存在"};
        }

        sp.mNumber = req.number();
        sp.mSpeakerName = req.speaker();
        sp.mRemark = remark;
        sp.mLastModifyTime = static_cast<int64_t>(GetTimeMs());

        if (!GetSpeakerDao().Update(accountId, sp)) {
            return Status {-1, "编辑声纹失败"};
        }

        return Status {};
    }

    Status VoiceprintService::DeleteVoiceprint(const DeleteVoiceprintRequest &req, Empty* resp) {
        (void)resp;
        uint64_t accountId = req.account_id();

        models::Speaker sp = GetSpeakerDao().GetById(accountId, req.id());
        if (sp.mId == 0) {
            return Status {-1, "声纹不存在"};
        }

        if (!GetSpeakerDao().DeleteById(accountId, req.id())) {
            return Status {-1, "删除声纹失败"};
        }
        // 删除声纹文件
        if (!FileOpt::RemoveFile(sp.mFileName)) {
            SLOG_ERROR << "DeleteVoiceprint: remove voiceprint file failed, path=" << sp.mFileName;
        }

        return Status {};
    }

}  // namespace qifeng_ca
