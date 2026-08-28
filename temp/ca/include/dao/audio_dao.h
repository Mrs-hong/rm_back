//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_AUDIO_DAO_H
#define QIFENG_CA_INCLUDE_DAO_AUDIO_DAO_H

#include <cstdint>
#include <string>
#include <vector>

#include "dao/bms_base_dao.h"
#include "dao/models/bms_audio.h"

namespace qifeng_ca {

    struct AudioSearchFilter {
        uint64_t mAccountId = 0;
        // 当前请求用户组ID，用于按组控制音频可见范围:
        //   ADMINISTRATOR(1)=全部可见, NORMAL_USER(2)=本人+访客, GUEST(3)=仅访客
        uint64_t mGroupId = 0;
        int mCurrent = 1;
        int mPageSize = 20;
        std::string mKeyword;
        int64_t mStartTime = 0;
        int64_t mEndTime = 0;
        std::vector<int> mStatusList;
        int mIsRecording {-1};
        bool mIgnoreAccountId {false};
        std::vector<uint64_t> mSponsorAccountIds;
    };

    struct AudioSearchResult {
        int mTotal = 0;
        std::vector<models::Audio> mRecords;
    };

    struct RefreshSummaryParams {
        int mKind = 0;
        int mWordCount = 0;
        bool mUseNote = false;
    };

    class AudioDao : public BmsBaseDao {
    public:
        AudioDao() = default;
        ~AudioDao() override = default;

        AudioDao(const AudioDao &) = delete;
        AudioDao &operator=(const AudioDao &) = delete;
        AudioDao(AudioDao &&) = delete;
        AudioDao &operator=(AudioDao &&) = delete;

        models::Audio GetById(uint64_t accountId, uint64_t id);

        models::Audio GetByAudioId(uint64_t accountId, const std::string &audioId);

        // 按audio_id全局查询，用于跨账户访问场景
        models::Audio GetByAudioIdGlobal(const std::string &audioId);

        // 按主键ID全局查询
        models::Audio GetByIdGlobal(uint64_t id);

        models::Audio GetByFileName(uint64_t accountId, const std::string &fileName);

        // 获取指定账户下的所有audio_id列表
        std::vector<std::string> GetAudioIdsByAccountId(uint64_t accountId);

        bool Insert(const models::Audio &audio);

        bool Update(uint64_t accountId, const models::Audio &audio);

        bool UpdateByAudioId(uint64_t accountId, const models::Audio &audio);

        bool DeleteById(uint64_t accountId, uint64_t id);

        bool DeleteByIds(uint64_t accountId, const std::vector<uint64_t> &ids);

        bool DeleteByAudioId(uint64_t accountId, const std::string &audioId);

        int64_t GetTotalDuration(uint64_t accountId);

        // 获取所有账户的音频总时长（毫秒）
        int64_t GetTotalDuration();

        AudioSearchResult Search(const AudioSearchFilter &filter);

        bool UpdateMarkers(uint64_t accountId, uint64_t id, const std::string &markersJson);

        bool UpdateMarkersByAudioId(uint64_t accountId, const std::string &audioId, const std::string &markersJson);

        bool UpdateStatus(uint64_t accountId, uint64_t id, int status);

        bool UpdateStatusByAudioId(uint64_t accountId, const std::string &audioId, int status);

        bool UpdateStatusByAudioId(uint64_t accountId, const std::string &audioId, int status,
                                   const std::string &message);

        bool UpdateTransProgress(uint64_t accountId, const std::string &audioId, int progress);

        // 更新转写开始时间（毫秒时间戳）
        bool UpdateTransStartTime(uint64_t accountId, const std::string &audioId, int64_t startTime);

        // 更新总结开始时间（毫秒时间戳）
        bool UpdateSumStartTime(uint64_t accountId, const std::string &audioId, int64_t startTime);

        bool UpdateTransPlanFinishTime(uint64_t accountId, const std::string &audioId, int64_t planFinishTs);

        // 更新总结进度(0-100)及预计完成时间
        bool UpdateSumProgress(uint64_t accountId, const std::string &audioId, int progress, int64_t planFinishTs);

        bool UpdateIsRecording(uint64_t accountId, const std::string &audioId, int isRecording);

        bool UpdateStatusAndRecording(uint64_t accountId, const std::string &audioId, int status, int isRecording);

        bool UpdateTotalTime(uint64_t accountId, const std::string &audioId, int totalTime);

        // 更新转写总时长(毫秒)
        bool UpdateTransDuration(uint64_t accountId, const std::string &audioId, int64_t transDuration);

        // 更新总结总时长(毫秒)
        bool UpdateSumDuration(uint64_t accountId, const std::string &audioId, int64_t sumDuration);

        bool UpdateNoteId(uint64_t accountId, const std::string &audioId, uint64_t noteId);

        bool UpdateKeywords(uint64_t accountId, const std::string &audioId, const std::string &keywords);

        // 刷新摘要: 更新状态为SUMMARYING及相关字段
        bool UpdateForRefreshSummary(uint64_t accountId, const std::string &audioId,
                                     const RefreshSummaryParams &params);

        // 更新音频文件路径(用于迁移后更新DB)
        bool UpdateFileName(uint64_t accountId, const std::string &audioId, const std::string &fileName);

        // 按状态列表统计音频数量(statusList为空时统计全部非录音中)
        int64_t CountByStatus(const std::vector<int> &statusList);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_AUDIO_DAO_H
