//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <vector>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/audio_enums.h"
#include "dao_managers/meeting_dao_manager.h"

namespace qifeng_ca {

    MeetingDaoManager &MeetingDaoManager::GetInstance() {
        static MeetingDaoManager Instance;
        return Instance;
    }

    MeetingDaoManager::MeetingDaoManager() : mAudioCache(1024) {
        SLOG_INFO << "MeetingDaoManager initialized with audio cache size: 1024";
    }

    std::string MeetingDaoManager::GenKey(uint64_t accountId, const std::string &audioId) {
        (void)accountId;
        return audioId;
    }

    void MeetingDaoManager::InvalidateAudio(const std::string &audioId) {
        mAudioCache.Remove(audioId);
    }

    models::Audio MeetingDaoManager::GetByAudioId(uint64_t accountId, const std::string &audioId) {
        std::string cacheKey = GenKey(accountId, audioId);
        models::Audio cachedAudio;
        if (mAudioCache.Get(cacheKey, cachedAudio)) {
            SLOG_DEBUG << "Audio cache hit for audio_id: " << audioId;
            return cachedAudio;
        }

        models::Audio audio = mAudioDao.GetByAudioId(accountId, audioId);
        if (audio.mId != 0) {
            mAudioCache.Put(cacheKey, audio);
        }
        return audio;
    }

    models::Audio MeetingDaoManager::GetByAudioIdGlobal(const std::string &audioId) {
        std::string cacheKey = audioId;
        models::Audio cachedAudio;
        if (mAudioCache.Get(cacheKey, cachedAudio)) {
            SLOG_DEBUG << "Audio cache hit (global) for audio_id: " << audioId;
            return cachedAudio;
        }

        models::Audio audio = mAudioDao.GetByAudioIdGlobal(audioId);
        if (audio.mId != 0) {
            mAudioCache.Put(cacheKey, audio);
        }
        return audio;
    }

    models::Audio MeetingDaoManager::GetById(uint64_t accountId, uint64_t id) {
        return mAudioDao.GetById(accountId, id);
    }

    models::Audio MeetingDaoManager::GetByIdGlobal(uint64_t id) {
        return mAudioDao.GetByIdGlobal(id);
    }

    std::vector<std::string> MeetingDaoManager::GetAudioIdsByAccountId(uint64_t accountId) {
        return mAudioDao.GetAudioIdsByAccountId(accountId);
    }

    AudioSearchResult MeetingDaoManager::Search(const AudioSearchFilter &filter) {
        return mAudioDao.Search(filter);
    }

    bool MeetingDaoManager::UpdateAudio(uint64_t accountId, const models::Audio &audio) {
        InvalidateAudio(audio.mAudioId);
        return mAudioDao.UpdateByAudioId(accountId, audio);
    }

    bool MeetingDaoManager::Insert(const models::Audio &audio) {
        InvalidateAudio(audio.mAudioId);
        return mAudioDao.Insert(audio);
    }

    bool MeetingDaoManager::UpdateMarkers(uint64_t accountId, const std::string &audioId,
                                          const std::string &markersJson) {
        InvalidateAudio(audioId);
        return mAudioDao.UpdateMarkersByAudioId(accountId, audioId, markersJson);
    }

    bool MeetingDaoManager::UpdateStatus(uint64_t accountId, const std::string &audioId, int status) {
        InvalidateAudio(audioId);
        return mAudioDao.UpdateStatusByAudioId(accountId, audioId, status);
    }

    bool MeetingDaoManager::UpdateStatus(uint64_t accountId, const std::string &audioId, int status,
                                         const std::string &message) {
        InvalidateAudio(audioId);
        return mAudioDao.UpdateStatusByAudioId(accountId, audioId, status, message);
    }

    bool MeetingDaoManager::UpdateStatus(uint64_t accountId, const std::string &audioId, int status, int isRecording) {
        InvalidateAudio(audioId);
        return mAudioDao.UpdateStatusAndRecording(accountId, audioId, status, isRecording);
    }

    bool MeetingDaoManager::UpdateTotalTime(uint64_t accountId, const std::string &audioId, int totalTime) {
        InvalidateAudio(audioId);
        return mAudioDao.UpdateTotalTime(accountId, audioId, totalTime);
    }

    bool MeetingDaoManager::UpdateTransDuration(uint64_t accountId, const std::string &audioId, int64_t transDuration) {
        InvalidateAudio(audioId);
        return mAudioDao.UpdateTransDuration(accountId, audioId, transDuration);
    }

    bool MeetingDaoManager::UpdateSumDuration(uint64_t accountId, const std::string &audioId, int64_t sumDuration) {
        InvalidateAudio(audioId);
        return mAudioDao.UpdateSumDuration(accountId, audioId, sumDuration);
    }

    bool MeetingDaoManager::UpdateTransProgress(uint64_t accountId, const std::string &audioId, int progress) {
        InvalidateAudio(audioId);
        return mAudioDao.UpdateTransProgress(accountId, audioId, progress);
    }

    bool MeetingDaoManager::UpdateTransStartTime(uint64_t accountId, const std::string &audioId, int64_t startTime) {
        InvalidateAudio(audioId);
        return mAudioDao.UpdateTransStartTime(accountId, audioId, startTime);
    }

    bool MeetingDaoManager::UpdateSumStartTime(uint64_t accountId, const std::string &audioId, int64_t startTime) {
        InvalidateAudio(audioId);
        return mAudioDao.UpdateSumStartTime(accountId, audioId, startTime);
    }

    bool MeetingDaoManager::UpdateSumProgress(uint64_t accountId, const std::string &audioId, int progress,
                                              int64_t planFinishTs) {
        InvalidateAudio(audioId);
        return mAudioDao.UpdateSumProgress(accountId, audioId, progress, planFinishTs);
    }

    bool MeetingDaoManager::ModAudioStatus(uint64_t accountId, const std::string &audioId, int status,
                                           const StModAudio &modAudio) {
        auto audio = GetByAudioId(accountId, audioId);
        if (audio.mId == 0) {
            SLOG_WARN << "ModAudioStatus: audio not found, audioId=" << audioId;
            return false;
        }

        // 重试逻辑: 检查重试次数是否超过上限
        if (modAudio.mRetryFlag && audio.mTransRetryCount >= 3) {
            status = static_cast<int>(AudioStatus::PermanentFailed);
            SLOG_WARN << "ModAudioStatus: retry limit exceeded, audioId=" << audioId;
        }

        // 更新音频对象
        audio.mStatus = status;
        audio.mRemark = modAudio.mMessage;
        if (!modAudio.mKeywords.empty()) {
            audio.mKeywords = modAudio.mKeywords;
        }
        if (modAudio.mRetryFlag) {
            ++audio.mTransRetryCount;
        }
        audio.mUpdateTime = static_cast<int64_t>(GetTimeMs());

        InvalidateAudio(audioId);
        return mAudioDao.UpdateByAudioId(accountId, audio);
    }

    bool MeetingDaoManager::UpdatePlanFinishTime(uint64_t accountId, const std::string &audioId, int64_t planFinishTs) {
        auto audio = GetByAudioId(accountId, audioId);
        if (audio.mId == 0) {
            SLOG_WARN << "UpdatePlanFinishTime: audio not found, audioId=" << audioId;
            return false;
        }

        InvalidateAudio(audioId);
        return mAudioDao.UpdateTransPlanFinishTime(accountId, audioId, planFinishTs);
    }

    bool MeetingDaoManager::CreateNote(uint64_t accountId, const std::string &audioId, uint64_t &noteId) {
        models::Note note;
        note.mAccountId = accountId;
        note.mAudioId = audioId;
        note.mTimestamp = static_cast<int64_t>(GetTimeMs());

        if (!mNoteDao.Insert(note)) {
            SLOG_ERROR << "CreateNoteForAudio: insert note failed, audioId=" << audioId;
            return false;
        }
        noteId = note.mId;

        InvalidateAudio(audioId);
        return true;
    }

    int64_t MeetingDaoManager::GetTotalDuration(uint64_t accountId) {
        return mAudioDao.GetTotalDuration(accountId);
    }

    int64_t MeetingDaoManager::GetTotalDuration() {
        return mAudioDao.GetTotalDuration();
    }

    bool MeetingDaoManager::DeleteRelationsForAudio(uint64_t accountId, const std::string &audioId) {
        bool ok = mTransDao.DeleteByAudioId(accountId, audioId);
        if (!ok) {
            SLOG_ERROR << "Delete trans failed for audio_id: " << audioId;
            return false;
        }

        ok = mSummaryDao.DeleteByAudioId(accountId, audioId);
        if (!ok) {
            SLOG_ERROR << "Delete summary failed for audio_id: " << audioId;
            return false;
        }

        ok = mNoteDao.DeleteByAudioId(accountId, audioId);
        if (!ok) {
            SLOG_WARN << "Delete note failed for audio_id: " << audioId;
        }

        return true;
    }

    bool MeetingDaoManager::DeleteAudioWithRelations(uint64_t accountId, const std::string &audioId) {
        models::Audio audio = GetByAudioId(accountId, audioId);
        if (audio.mId == 0) {
            SLOG_ERROR << "Audio not found: " << audioId;
            return false;
        }

        if (!DeleteRelationsForAudio(accountId, audioId)) {
            return false;
        }

        if (!mAudioDao.DeleteByAudioId(accountId, audioId)) {
            SLOG_ERROR << "Delete audio failed: " << audioId;
            return false;
        }

        InvalidateAudio(audioId);
        return true;
    }

    bool MeetingDaoManager::DeleteAudioWithRelationsByAudioIds(uint64_t accountId,
                                                               const std::vector<std::string> &audioIds) {
        if (audioIds.empty()) {
            return true;
        }

        for (const auto &audioId : audioIds) {
            models::Audio audio = GetByAudioId(accountId, audioId);
            if (audio.mId == 0) {
                SLOG_WARN << "Audio not found, skip: " << audioId;
                continue;
            }

            if (!DeleteRelationsForAudio(accountId, audioId)) {
                SLOG_WARN << "Delete relations failed for audio: " << audioId;
                continue;
            }

            if (!mAudioDao.DeleteByAudioId(accountId, audioId)) {
                SLOG_WARN << "Delete audio failed: " << audioId;
            }

            InvalidateAudio(audioId);
        }
        return true;
    }

    bool MeetingDaoManager::SaveTransformWords(uint64_t accountId, const std::string &audioId,
                                               const std::vector<models::Trans> &transList,
                                               std::vector<uint64_t> &newIds) {
        if (transList.empty()) {
            return true;
        }

        models::Audio audio = GetByAudioId(accountId, audioId);
        if (audio.mId == 0) {
            SLOG_ERROR << "Audio not found: " << audioId;
            return false;
        }

        for (const auto &trans : transList) {
            models::Trans newTrans = trans;
            newTrans.mAccountId = accountId;
            newTrans.mAudioId = audioId;

            bool ok = false;
            if (newTrans.mId != 0) {
                // 已有记录，更新起始/结束时间、段标志、文本内容、说话人
                ok = mTransDao.UpdateContent(newTrans);
                if (!ok) {
                    SLOG_ERROR << "Update trans failed for audio: " << audioId << " id: " << newTrans.mId;
                    return false;
                }
            } else {
                // 新记录，插入并记录自增id
                ok = mTransDao.Insert(newTrans);
                if (!ok) {
                    SLOG_ERROR << "Insert trans failed for audio: " << audioId;
                    return false;
                }
                newIds.push_back(newTrans.mId);
            }
        }
        return true;
    }

    std::vector<models::Trans> MeetingDaoManager::GetTransContent(uint64_t accountId, const std::string &audioId) {
        return mTransDao.GetByAudioId(accountId, audioId);
    }

    bool MeetingDaoManager::DeleteTransByAudioId(uint64_t accountId, const std::string &audioId) {
        return mTransDao.DeleteByAudioId(accountId, audioId);
    }

    int64_t MeetingDaoManager::GetLatestTransEndTime(uint64_t accountId, const std::string &audioId) {
        auto transList = mTransDao.GetByAudioId(accountId, audioId);
        if (transList.empty()) {
            return 0;
        }

        int64_t maxEndTime = 0;
        for (const auto &trans : transList) {
            if (trans.mEndTime > maxEndTime) {
                maxEndTime = trans.mEndTime;
            }
        }
        return maxEndTime;
    }

    bool MeetingDaoManager::SaveSummaryContent(uint64_t accountId, const std::string &audioId,
                                               const std::string &content) {
        models::Summary existing = mSummaryDao.GetByAudioId(accountId, audioId);
        if (existing.mId != 0) {
            return mSummaryDao.UpdateContent(accountId, audioId, content);
        }

        models::Summary summary;
        summary.mAccountId = accountId;
        summary.mAudioId = audioId;
        summary.mContent = content;
        summary.mTimestamp = static_cast<int64_t>(GetTimeMs());
        return mSummaryDao.Insert(summary);
    }

    bool MeetingDaoManager::UpdateForRefreshSummary(uint64_t accountId, const std::string &audioId,
                                                    const RefreshSummaryParams &params) {
        bool ok = mAudioDao.UpdateForRefreshSummary(accountId, audioId, params);
        if (ok) {
            InvalidateAudio(audioId);
        }
        return ok;
    }

    bool MeetingDaoManager::UpdateKeywords(uint64_t accountId, const std::string &audioId,
                                           const std::string &keywords) {
        bool ok = mAudioDao.UpdateKeywords(accountId, audioId, keywords);
        if (ok) {
            InvalidateAudio(audioId);
        }
        return ok;
    }

    bool MeetingDaoManager::UpdateFileName(uint64_t accountId, const std::string &audioId,
                                           const std::string &fileName) {
        bool ok = mAudioDao.UpdateFileName(accountId, audioId, fileName);
        if (ok) {
            InvalidateAudio(audioId);
        }
        return ok;
    }

    bool MeetingDaoManager::GetRecordingDetailData(uint64_t accountId, const std::string &audioId,
                                                   std::vector<models::Trans> &transList, models::Summary &summary) {
        transList = mTransDao.GetByAudioId(accountId, audioId);
        summary = mSummaryDao.GetByAudioId(accountId, audioId);
        return true;
    }

    models::Note MeetingDaoManager::GetNoteByAudioId(uint64_t accountId, const std::string &audioId) {
        return mNoteDao.GetByAudioId(accountId, audioId);
    }

}  // namespace qifeng_ca
