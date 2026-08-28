//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

// 该文件实现会议的基本增删查改功能

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/types.h>
#include <vector>

#include "minidocx.hpp"
#include "qifeng_framework/common/logger.h"
#include "utf8/checked.h"

#include "common/audio/audio_utils.h"
#include "common/audio_enums.h"
#include "common/common.h"
#include "common/config/meeting_config.h"
#include "common/proto_utils.h"
#include "common/status.h"
#include "common/summary_kind_def.h"
#include "common/utils/file_opt.h"
#include "common/utils/symlink_manager.h"
#include "core/meeting/meeting_access.h"
#include "core/meeting/meeting_check.h"
#include "core/meeting/meeting_db_service.h"
#include "dao/models/bms_audio.h"
#include "dao/models/bms_summary.h"
#include "dao/models/bms_trans.h"
#include "dao_managers/meeting_dao_manager.h"
#include "dao_managers/user_dao_manager.h"
#include "internal/recording_manager.h"
#include "schedule/pcm/pcm_engine.h"

namespace qifeng_ca {

    static Status ValidateEditRequest(const MeetingEditRequest &req) {
        auto &cfg = MeetingConfig::GetInstance();
        if (req.theme().empty()) {
            return Status {-1, "会议主题不能为空"};
        }
        if (static_cast<size_t>(utf8::distance(req.theme().begin(), req.theme().end())) >
            static_cast<size_t>(cfg.GetMaxThemeLen())) {
            return Status {-1, "主题长度不能超过" + std::to_string(cfg.GetMaxThemeLen())};
        }
        if (!IsThemeContentValid(req.theme())) {
            return Status {-1, "主题只能包含中文、英文、数字和_-."};
        }
        if (req.has_moderator() && static_cast<size_t>(utf8::distance(req.moderator().begin(), req.moderator().end())) >
                                       static_cast<size_t>(cfg.GetMaxModeratorLen())) {
            return Status {-1, "主持人长度不能超过" + std::to_string(cfg.GetMaxModeratorLen())};
        }
        if (req.has_places() && static_cast<size_t>(utf8::distance(req.places().begin(), req.places().end())) >
                                    static_cast<size_t>(cfg.GetMaxPlacesLen())) {
            return Status {-1, "地点长度不能超过" + std::to_string(cfg.GetMaxPlacesLen())};
        }
        if (req.has_remark() && static_cast<size_t>(utf8::distance(req.remark().begin(), req.remark().end())) >
                                    static_cast<size_t>(cfg.GetMaxRemarkLen())) {
            return Status {-1, "备注长度不能超过" + std::to_string(cfg.GetMaxRemarkLen())};
        }
        return Status {};
    }

    static int GetAudioPriority(int status) {
        switch (static_cast<AudioStatus>(status)) {
            case AudioStatus::Summarying:
                return 4;
            case AudioStatus::Transing:
                return 3;
            case AudioStatus::WaitSummary:
                return 2;
            case AudioStatus::WaitTrans:
                return 1;
            case AudioStatus::Meeting:
            case AudioStatus::TransException:
            case AudioStatus::PermanentFailed:
            case AudioStatus::SummaryComplete:
            default:
                return 0;
        }
    }

    // 按 seg_flag + 说话人分段, 组装 DetailItem 列表
    static void BuildDetailItems(const std::vector<models::Trans> &transList,
                                 google::protobuf::RepeatedPtrField<DetailItem>* details) {
        DetailItem* current = nullptr;
        for (const auto &t : transList) {
            bool segFlag = t.mSegFlag == 1;
            std::string speakerKey = t.mSpeakerName.empty()
                                         ? "Speaker " + (t.mSpeaker > 0 ? std::to_string(t.mSpeaker) : "未知")
                                         : t.mSpeakerName;

            // seg_flag 或 说话人变化 触发新的 DetailItem
            if (current == nullptr || segFlag || current->speaker() != speakerKey) {
                current = details->Add();
                current->set_speaker(speakerKey);
                current->set_start_time(t.mStartTime);
                current->set_end_time(t.mEndTime);
            } else {
                current->set_end_time(t.mEndTime);
            }

            auto* word = current->add_words();
            word->set_id(t.mId);
            word->set_content(t.mContent);
            word->set_start_time(t.mStartTime);
            word->set_end_time(t.mEndTime);
            word->set_seg_flag(segFlag);
        }
    }

    // 无标题时: 生成单个 TransformItem, detail 按 seg_flag + 说话人分段
    static void FillTransList(const std::vector<models::Trans> &transList, MeetingDetailResponse* resp) {
        if (transList.empty()) {
            return;
        }

        auto* item = resp->add_transform_list();
        item->set_title("会议主题");
        item->set_abstract("会议内容");
        item->set_start_time(transList.front().mStartTime);
        item->set_end_time(transList.back().mEndTime);

        BuildDetailItems(transList, item->mutable_detail());
    }

    static void FillAudioDetail(const models::Audio &audio, MeetingDetailResponse* resp) {
        resp->set_id(std::to_string(audio.mId));
        resp->set_audio_id(audio.mAudioId);
        resp->set_total_time(static_cast<uint32_t>(audio.mTotalTime));
        resp->set_is_rel_time(audio.mIsRecording == 1);
        resp->set_status(audio.mStatus);
        // 路径重定位
        std::string wavPath = SymlinkManager::GetInstance().ResolveData2Path(audio.mFileName);
        resp->set_file_url(wavPath);
        if (!audio.mTheme.empty()) {
            resp->set_theme(audio.mTheme);
        }
        if (!audio.mModerator.empty()) {
            resp->set_moderator(audio.mModerator);
        }
        if (!audio.mAttendees.empty()) {
            resp->set_attendees(audio.mAttendees);
        }
        if (!audio.mPlaces.empty()) {
            resp->set_places(audio.mPlaces);
        }
        if (audio.mKind != 0) {
            resp->set_kind(audio.mKind);
        }
        if (!audio.mRemark.empty()) {
            resp->set_remark(audio.mRemark);
        }
        if (audio.mRecordingTime != 0) {
            resp->set_recording_time(static_cast<uint64_t>(audio.mRecordingTime));
        }
        if (audio.mNoteId != 0) {
            resp->set_note_id(audio.mNoteId);
        }
    }

    static void FillMarkersJson(const models::Audio &audio, MeetingDetailResponse* resp) {
        if (!audio.mMarkers.empty()) {
            resp->set_markers_json(audio.mMarkers);
        }
    }

    void MeetingDBService::FillSearchResponse(const AudioSearchResult &result, MeetingSearchResponse* resp) {  // NOLINT
        resp->set_total(result.mTotal);
        for (const auto &r : result.mRecords) {
            // 忽略删除中的音频
            if (r.mStatus == static_cast<int>(AudioStatus::Deleting)) {
                continue;
            }
            auto* item = resp->add_records();
            item->set_id(std::to_string(r.mId));
            item->set_audio_id(r.mAudioId);
            if (r.mTimestamp != 0) {
                item->set_create_time(r.mTimestamp);
            }
            if (!r.mTheme.empty()) {
                item->set_theme(r.mTheme);
            }
            item->set_keyword(r.mKeywords);
            // 与详情接口对齐: total_time为0时也输出, 避免列表缺失字段与详情显示00:00:00不一致
            item->set_total_time(static_cast<uint32_t>(r.mTotalTime));
            if (r.mRecordingTime != 0) {
                item->set_recording_time(static_cast<uint64_t>(r.mRecordingTime));
            }
            if (!r.mRemark.empty()) {
                item->set_remark(r.mRemark);
            }
            item->set_status(r.mStatus);
            item->set_is_rel_time(r.mIsRecording == 1);
            if (!r.mFileName.empty()) {
                item->set_file_url(r.mFileName);
            }
            if (!r.mMessage.empty()) {
                item->set_message(r.mMessage);
            }
            // 开会人名称与私密会议标记: 由音频创建者用户信息推导(访客group_id=3为私密会议)
            auto owner = UserDaoManager::GetInstance().GetByAccountId(r.mAccountId);
            if (owner.mAccountId != 0) {
                bool isGuest = owner.mGroupId == Authority::GUEST;
                item->set_sponsor_name(isGuest ? "访客" : owner.mUserName);
                item->set_is_private(!isGuest);
                item->set_account_id(r.mAccountId);
            }
            if (r.mStatus == static_cast<int>(AudioStatus::Transing)) {
                item->set_last_progress(r.mTransLastProgress);
                item->set_plan_finish_time(static_cast<uint64_t>(r.mPlanFinishTime));
            } else if (r.mStatus == static_cast<int>(AudioStatus::Summarying)) {
                item->set_last_progress(r.mSumLastProgress);
                item->set_plan_finish_time(static_cast<uint64_t>(r.mPlanFinishTime));
            }
        }
    }

    // 校验单条转写数据的合法性
    static Status ValidateSingleWord(const SaveTransWord &word, int64_t prevStartTime) {
        // speaker长度校验: 1~64
        const auto &cfg = MeetingConfig::GetInstance();
        auto speakerLen = static_cast<int32_t>(utf8::distance(word.speaker().begin(), word.speaker().end()));
        if (speakerLen < 1 || speakerLen > cfg.GetMaxSpeakerLen()) {
            return Status {-1, "说话人名称长度必须在1-" + std::to_string(cfg.GetMaxSpeakerLen()) + "之间"};
        }

        // content长度校验: <=512
        auto contentLen = static_cast<int32_t>(utf8::distance(word.content().begin(), word.content().end()));
        if (contentLen > cfg.GetMaxContentLen()) {
            return Status {-1, "转写内容长度不能超过" + std::to_string(cfg.GetMaxContentLen())};
        }

        // 时间合法性校验
        int64_t startTime = static_cast<int64_t>(word.start_time());
        int64_t endTime = static_cast<int64_t>(word.end_time());
        if (startTime >= endTime || endTime < 0 || startTime < 0 || startTime > 2147483647 || endTime > 2147483647) {
            return Status {-1, "转写时间参数不合法"};
        }

        // 新增数据(id为0)时检查时间顺序
        if (word.id() == 0 && prevStartTime >= 0 && startTime <= prevStartTime) {
            return Status {-1, "时间顺序错误: startTime应大于前一条"};
        }

        return Status {};
    }

    // 将proto SaveTransWord转换为models::Trans
    static models::Trans ConvertWordToTrans(const SaveTransWord &word, uint64_t accountId, const std::string &audioId) {
        models::Trans trans;
        trans.mId = word.id();
        trans.mAccountId = accountId;
        trans.mAudioId = audioId;
        trans.mStartTime = static_cast<int32_t>(word.start_time());
        trans.mEndTime = static_cast<int32_t>(word.end_time());
        trans.mSpeakerName = word.speaker();
        trans.mContent = word.content();
        trans.mIsOrig = 0;
        trans.mIsDiscard = 0;
        trans.mSegFlag = word.seg_flag() ? 1 : 0;
        return trans;
    }

    Status MeetingDBService::GetRecordingList(const MeetingSearchRequest &req, MeetingSearchResponse* resp) {
        if (req.page_size() <= 0 || req.page_size() > 1000) {
            return Status {-1, "page_size参数异常"};
        }
        if (req.current() <= 0) {
            return Status {-1, "current参数异常"};
        }

        AudioSearchFilter filter;
        filter.mAccountId = req.account_id();
        models::User user = UserDaoManager::GetInstance().GetByAccountId(req.account_id());
        filter.mGroupId = user.mGroupId;
        filter.mCurrent = req.current();
        filter.mPageSize = req.page_size();
        if (req.has_keyword()) {
            filter.mKeyword = req.keyword();
        }
        if (req.has_start_time()) {
            filter.mStartTime = req.start_time();
        }
        if (req.has_end_time()) {
            filter.mEndTime = req.end_time();
        }
        for (int i = 0; i < req.status_size(); ++i) {
            filter.mStatusList.push_back(req.status(i));
        }
        for (int i = 0; i < req.sponsor_account_ids_size(); ++i) {
            filter.mSponsorAccountIds.push_back(req.sponsor_account_ids(i));
        }

        AudioSearchResult result = MeetingDaoManager::GetInstance().Search(filter);
        FillSearchResponse(result, resp);
        return Status {};
    }

    Status MeetingDBService::GetRecordingDetail(const MeetingDetailRequest &req, MeetingDetailResponse* resp) {
        if (req.audio_id().empty()) {
            return Status {-1, "录音audio_id不能为空"};
        }

        auto [audio, accessStatus] = GetAccessibleAudio(req.account_id(), req.audio_id());
        if (!accessStatus.IsSuccess()) {
            return accessStatus;
        }

        std::vector<models::Trans> transList;
        models::Summary summary;
        MeetingDaoManager::GetInstance().GetRecordingDetailData(audio.mAccountId, audio.mAudioId, transList, summary);

        if (!summary.mContent.empty()) {
            resp->set_summary(summary.mContent);
        }
        /* 对content是空时进行过滤 */
        transList.erase(std::remove_if(transList.begin(), transList.end(),
                                       [](const models::Trans &t) { return t.mContent.empty(); }),
                        transList.end());

        FillAudioDetail(audio, resp);
        FillMarkersJson(audio, resp);
        FillTransList(transList, resp);

        return Status {};
    }

    Status MeetingDBService::EditRecording(const MeetingEditRequest &req, Empty* resp) {
        (void)resp;
        Status validStatus = ValidateEditRequest(req);
        if (validStatus.GetCode() != 0) {
            return validStatus;
        }

        if (req.audio_id().empty()) {
            return Status {-1, "录音audio_id不能为空"};
        }

        auto [audio, accessStatus] = GetAccessibleAudio(req.account_id(), req.audio_id());
        if (!accessStatus.IsSuccess()) {
            return accessStatus;
        }

        audio.mTheme = req.theme();
        if (req.has_moderator()) {
            audio.mModerator = req.moderator();
        }
        if (req.has_attendees()) {
            audio.mAttendees = req.attendees();
        }
        audio.mRecordingTime = req.recording_time();
        if (req.has_places()) {
            audio.mPlaces = req.places();
        }
        if (req.has_kind()) {
            audio.mKind = req.kind();
        }
        if (req.has_remark()) {
            audio.mRemark = req.remark();
        }
        audio.mUpdateTime = static_cast<int64_t>(GetTimeMs());

        if (!MeetingDaoManager::GetInstance().UpdateAudio(audio.mAccountId, audio)) {
            return Status {-1, "编辑录音失败"};
        }
        if (req.audio_id() == RecordingManager::GetInstance().GetActiveAudioId()) {
            RecordingManager::GetInstance().SetActiveMeetingName(audio.mTheme);
        }
        return Status {};
    }

    Status MeetingDBService::DeleteAudioCascade(uint64_t accountId, const std::string &audioId) {
        models::Audio audio = MeetingDaoManager::GetInstance().GetByAudioId(accountId, audioId);
        SLOG_INFO << "DeleteAudioCascade: accountId=" << accountId << " audioId=" << audioId;
        if (audio.mId == 0) {
            return Status {-1, "录音不存在"};
        }

        PcmEngine::GetInstance().CancelTaskByAudioId(audioId);

        FileOpt::RemoveFile(audio.mFileName);
        auto fileName = SymlinkManager::GetInstance().ResolveData2Path(audio.mFileName);
        FileOpt::RemoveFile(fileName);
        if (!MeetingDaoManager::GetInstance().DeleteAudioWithRelations(accountId, audio.mAudioId)) {
            return Status {-1, "删除录音失败"};
        }
        return Status {};
    }

    Status MeetingDBService::DeleteRecording(const MeetingDeleteRequest &req, Empty* resp) {
        (void)resp;

        if (req.has_audio_id() && !req.audio_id().empty()) {
            auto [audio, accessStatus] = GetAccessibleAudio(req.account_id(), req.audio_id());
            if (!accessStatus.IsSuccess()) {
                return accessStatus;
            }
            return DeleteAudioCascade(audio.mAccountId, req.audio_id());
        }

        if (req.audio_ids_size() > 0) {
            std::vector<std::string> audioIds;
            for (int i = 0; i < req.audio_ids_size(); ++i) {
                audioIds.push_back(req.audio_ids(i));
            }

            for (const auto &audioId : audioIds) {
                auto [audio, accessStatus] = GetAccessibleAudio(req.account_id(), audioId);
                if (!accessStatus.IsSuccess()) {
                    SLOG_WARN << "DeleteRecording: access denied, audioId=" << audioId
                              << " status=" << accessStatus.ToString();
                    continue;
                }
                Status st = DeleteAudioCascade(audio.mAccountId, audioId);
                if (!st.IsSuccess()) {
                    SLOG_WARN << "DeleteRecording: cascade delete failed, audioId=" << audioId
                              << " status=" << st.ToString();
                }
            }
            return Status {};
        }

        return Status {-1, "请指定要删除的录音"};
    }

    Status MeetingDBService::GetTotalTime(uint64_t accountId, MeetingTotalTimeResponse* resp) {
        int64_t total = MeetingDaoManager::GetInstance().GetTotalDuration(accountId);
        resp->set_total_time(total);
        return Status {};
    }

    Status MeetingDBService::UpdateMarkers(const MarkerUpdateRequest &req, Empty* resp) {
        (void)resp;
        if (req.audio_id().empty()) {
            return Status {-1, "录音audio_id不能为空"};
        }

        if (!req.has_marker_list()) {
            return Status {-1, "打点标记数据不能为空"};
        }

        const MarkerList &markerList = req.marker_list();
        if (markerList.markers_size() > 20) {
            return Status {-1, "打点标记最多20个"};
        }

        for (int i = 0; i < markerList.markers_size(); ++i) {
            const MarkerData &marker = markerList.markers(i);
            if (marker.time() < 0 || marker.time() > 2147483647) {
                return Status {-1, "时间戳非法"};
            }
            if (marker.has_title() &&
                static_cast<size_t>(utf8::distance(marker.title().begin(), marker.title().end())) > 20) {
                return Status {-1, "标记标题最长20字符"};
            }
        }

        std::string markersJson = proto_utils::MessageToJson(markerList);
        if (markersJson.empty() && markerList.markers_size() > 0) {
            return Status {-1, "序列化打点标记失败"};
        }

        auto [audio, accessStatus] = GetAccessibleAudio(req.account_id(), req.audio_id());
        if (!accessStatus.IsSuccess()) {
            return accessStatus;
        }

        if (!MeetingDaoManager::GetInstance().UpdateMarkers(audio.mAccountId, req.audio_id(), markersJson)) {
            return Status {-1, "更新打点标记失败"};
        }
        return Status {};
    }

    const models::Audio* MeetingDBService::FindActiveAudio(const std::vector<models::Audio> &records) {
        const models::Audio* best = nullptr;
        int bestPriority = 0;

        for (const auto &audio : records) {
            int priority = GetAudioPriority(audio.mStatus);
            if (priority > bestPriority) {
                bestPriority = priority;
                best = &audio;
            }
        }
        return best;
    }

    void MeetingDBService::FillActiveAudioResponse(const models::Audio &audio, GetActiveRecordingResponse* resp) {
        resp->set_meeting_name(audio.mTheme);
        resp->set_status(audio.mStatus);

        if (audio.mPlanFinishTime != 0) {
            resp->set_plan_finish_time(audio.mPlanFinishTime);
        }
    }

    Status MeetingDBService::GetActiveRecording(uint64_t accountId, GetActiveRecordingResponse* resp) {
        AudioSearchFilter filter;
        filter.mAccountId = accountId;
        models::User user = UserDaoManager::GetInstance().GetByAccountId(accountId);
        filter.mGroupId = user.mGroupId;
        filter.mCurrent = 1;
        filter.mPageSize = 50;
        filter.mStatusList = {static_cast<int>(AudioStatus::WaitTrans), static_cast<int>(AudioStatus::Transing),
                              static_cast<int>(AudioStatus::WaitSummary), static_cast<int>(AudioStatus::Summarying)};

        AudioSearchResult result = MeetingDaoManager::GetInstance().Search(filter);

        const models::Audio* activeAudio = FindActiveAudio(result.mRecords);
        if (activeAudio != nullptr) {
            FillActiveAudioResponse(*activeAudio, resp);
        }
        return Status {};
    }

    Status MeetingDBService::ResetTranscribeTask(const ResetTranscribeTaskRequest &req, Empty* resp) {
        (void)resp;
        if (req.audio_ids_size() == 0) {
            return Status {-1, "请指定需要重置的任务"};
        }

        for (int i = 0; i < req.audio_ids_size(); ++i) {
            const std::string &audioId = req.audio_ids(i);
            auto [audio, accessStatus] = GetAccessibleAudio(req.account_id(), audioId);
            if (!accessStatus.IsSuccess()) {
                SLOG_WARN << "ResetTranscribeTask access denied, skip: " << audioId
                          << " status=" << accessStatus.ToString();
                continue;
            }
            if (audio.mStatus != static_cast<int>(AudioStatus::TransException)) {
                SLOG_WARN << "Only TransException(98) can be reset, skip: " << audioId;
                continue;
            }

            // TODO(yf): 需要调用PCM模块重置转写任务(TranscribeTaskManager.reset_transcribe_tasks_api)
            if (!MeetingDaoManager::GetInstance().UpdateStatus(audio.mAccountId, audioId,
                                                               static_cast<int>(AudioStatus::WaitTrans))) {
                SLOG_WARN << "Reset transcribe status failed: " << audioId;
            }
        }
        return Status {};
    }

    Status MeetingDBService::GetRecordInfo(uint64_t accountId, const std::string &audioId, RecordInfoResponse* resp) {
        if (audioId.empty()) {
            return Status {-1, "录音audio_id不能为空"};
        }

        auto [audio, accessStatus] = GetAccessibleAudio(accountId, audioId);
        if (!accessStatus.IsSuccess()) {
            return accessStatus;
        }

        std::string fileName = audio.mFileName;
        if (fileName.empty()) {
            return Status {-1, "录音文件名为空"};
        }

        // 路径重定向
        std::string wavPath = SymlinkManager::GetInstance().ResolveData2Path(audio.mFileName);
        if (!std::filesystem::exists(wavPath)) {
            return Status {-1, "录音文件不存在"};
        }

        WavHeaderInfo headerInfo;
        if (!AudioUtils::GetWavHeaderInfo(wavPath, headerInfo)) {
            return Status {-1, "解析录音文件失败"};
        }

        resp->set_file(wavPath);
        auto* params = resp->mutable_params();
        params->set_sample_rate(headerInfo.mSampleRate);
        params->set_channels(headerInfo.mChannels);
        params->set_bit_depth(headerInfo.mBitDepth);
        resp->set_total_time(audio.mTotalTime);

        bool isRecording = RecordingManager::GetInstance().IsRecording() &&
                           RecordingManager::GetInstance().GetActiveAudioId() == audioId;
        resp->set_is_recording(isRecording);
        resp->set_is_pause(RecordingManager::GetInstance().IsPaused());

        return Status {};
    }

    Status MeetingDBService::GetSummaryKinds(GetSummaryKindResponse* resp) {
        for (const auto &kind : SummaryKindMap) {
            auto* item = resp->add_items();
            item->set_label(std::string(kind.mLabel));
            item->set_value(kind.mValue);
            item->set_code(std::string(kind.mCode));
        }
        return Status {};
    }

    Status MeetingDBService::SaveTransformList(const SaveTransformRequest &req, SaveTransformResponse* resp) {
        uint64_t accountId = req.account_id();
        const std::string &audioId = req.audio_id();
        SLOG_INFO << "SaveTransformList: accountId=" << accountId << " audioId=" << audioId
                  << " words=" << req.words_size();

        // 检查转写数据是否为空
        if (req.words_size() < 1) {
            return Status {-1, "未提供转写数据"};
        }

        // 逐条校验转写数据
        int64_t prevStartTime = -1;
        for (int i = 0; i < req.words_size(); ++i) {
            Status validStatus = ValidateSingleWord(req.words(i), prevStartTime);
            if (validStatus.GetCode() != 0) {
                SLOG_ERROR << "SaveTransformList: validate failed at index=" << i
                           << " status=" << validStatus.ToString();
                return validStatus;
            }
            prevStartTime = static_cast<int64_t>(req.words(i).start_time());
        }

        // 校验访问权限并获取音频所有者ID
        auto [audio, accessStatus] = GetAccessibleAudio(accountId, audioId);
        if (!accessStatus.IsSuccess()) {
            return accessStatus;
        }
        uint64_t ownerId = audio.mAccountId;

        // 转换proto SaveTransWord为models::Trans
        std::vector<models::Trans> transList;
        transList.reserve(static_cast<size_t>(req.words_size()));
        for (int i = 0; i < req.words_size(); ++i) {
            transList.push_back(ConvertWordToTrans(req.words(i), ownerId, audioId));
        }

        // 保存到数据库
        std::vector<uint64_t> newIds;
        auto &daoMgr = MeetingDaoManager::GetInstance();
        bool ok = daoMgr.SaveTransformWords(ownerId, audioId, transList, newIds);
        if (!ok) {
            return Status {-1, "保存转写数据失败"};
        }

        for (uint64_t id : newIds) {
            resp->add_new_word_ids(id);
        }

        SLOG_INFO << "SaveTransformList: success, newIds=" << newIds.size();
        return Status {};
    }

    Status MeetingDBService::SaveRecordingSummary(const SaveSummaryRequest &req) {
        uint64_t accountId = req.account_id();
        const std::string &audioId = req.audio_id();
        SLOG_INFO << "SaveRecordingSummary: accountId=" << accountId << " audioId=" << audioId;

        if (static_cast<size_t>(utf8::distance(req.summary().begin(), req.summary().end())) >
            static_cast<size_t>(MeetingConfig::GetInstance().GetMaxSummaryLen())) {
            return Status {-1, "会议纪要内容过长"};
        }

        // 校验访问权限并获取音频所有者ID
        auto [audio, accessStatus] = GetAccessibleAudio(accountId, audioId);
        if (!accessStatus.IsSuccess()) {
            return accessStatus;
        }

        auto &daoMgr = MeetingDaoManager::GetInstance();
        bool ok = daoMgr.SaveSummaryContent(audio.mAccountId, audioId, req.summary());
        if (!ok) {
            return Status {-1, "保存会议纪要失败"};
        }

        SLOG_INFO << "SaveRecordingSummary: success";
        return Status {};
    }

}  // namespace qifeng_ca
