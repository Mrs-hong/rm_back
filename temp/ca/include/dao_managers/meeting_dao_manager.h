//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_MANAGERS_MEETING_DAO_MANAGER_H
#define QIFENG_CA_INCLUDE_DAO_MANAGERS_MEETING_DAO_MANAGER_H

#include <cstdint>
#include <string>
#include <vector>

#include "dao/models/bms_audio.h"
#include "qifeng_framework/common/cache/lru_cache.h"

#include "dao/audio_dao.h"
#include "dao/note_dao.h"
#include "dao/summary_dao.h"
#include "dao/trans_dao.h"

namespace qifeng_ca {

    struct StModAudio {
        bool mRetryFlag;
        const std::string mKeywords;
        const std::string &mMessage;
        explicit StModAudio(const std::string &message) : mRetryFlag(false), mKeywords(""), mMessage(message) {}
        explicit StModAudio(const std::string &message, const std::string keywords, bool retry)
            : mRetryFlag(retry), mKeywords(keywords), mMessage(message) {}
    };

    class MeetingDaoManager {
    public:
        using AudioCacheType = common::cache::LRUCache<std::string, models::Audio>;

        static MeetingDaoManager &GetInstance();

        MeetingDaoManager(const MeetingDaoManager &) = delete;
        MeetingDaoManager &operator=(const MeetingDaoManager &) = delete;
        MeetingDaoManager(MeetingDaoManager &&) = delete;
        MeetingDaoManager &operator=(MeetingDaoManager &&) = delete;

        // ---- 音频查询 ----

        models::Audio GetByAudioId(uint64_t accountId, const std::string &audioId);

        // 按audio_id全局查询, 用于跨账户访问场景
        models::Audio GetByAudioIdGlobal(const std::string &audioId);

        // 按主键ID查询音频记录
        models::Audio GetById(uint64_t accountId, uint64_t id);

        // 按主键ID全局查询
        models::Audio GetByIdGlobal(uint64_t id);

        // 获取指定账户下的所有audio_id列表
        std::vector<std::string> GetAudioIdsByAccountId(uint64_t accountId);

        AudioSearchResult Search(const AudioSearchFilter &filter);

        int64_t GetTotalDuration(uint64_t accountId);

        // 获取所有账户的音频总时长（毫秒）
        int64_t GetTotalDuration();

        bool Insert(const models::Audio &audio);

        // ---- 音频更新 ----

        bool UpdateAudio(uint64_t accountId, const models::Audio &audio);

        bool UpdateMarkers(uint64_t accountId, const std::string &audioId, const std::string &markersJson);

        bool UpdateStatus(uint64_t accountId, const std::string &audioId, int status);

        bool UpdateStatus(uint64_t accountId, const std::string &audioId, int status, const std::string &message);

        bool UpdateStatus(uint64_t accountId, const std::string &audioId, int status, int isRecording);

        bool UpdateTotalTime(uint64_t accountId, const std::string &audioId, int totalTime);

        // 更新转写总时长(毫秒)
        bool UpdateTransDuration(uint64_t accountId, const std::string &audioId, int64_t transDuration);

        // 更新总结总时长(毫秒)
        bool UpdateSumDuration(uint64_t accountId, const std::string &audioId, int64_t sumDuration);

        // 更新转写进度(0-100)
        bool UpdateTransProgress(uint64_t accountId, const std::string &audioId, int progress);

        // 更新转写开始时间（毫秒时间戳）
        bool UpdateTransStartTime(uint64_t accountId, const std::string &audioId, int64_t startTime);

        // 更新总结开始时间（毫秒时间戳）
        bool UpdateSumStartTime(uint64_t accountId, const std::string &audioId, int64_t startTime);

        // 更新总结进度(0-100)及预计完成时间
        bool UpdateSumProgress(uint64_t accountId, const std::string &audioId, int progress, int64_t planFinishTs);

        // 更新音频处理状态(含重试计数/错误信息等, 对应Python mod_audio_status)
        bool ModAudioStatus(uint64_t accountId, const std::string &audioId, int status, const StModAudio &modAudio);

        // 更新预计完成时间(仅首次设置)
        bool UpdatePlanFinishTime(uint64_t accountId, const std::string &audioId, int64_t planFinishTs);

        // 为录音创建空笔记并关联noteId
        bool CreateNote(uint64_t accountId, const std::string &audioId, uint64_t &noteId);

        // ---- 音频删除 ----

        bool DeleteAudioWithRelations(uint64_t accountId, const std::string &audioId);

        bool DeleteAudioWithRelationsByAudioIds(uint64_t accountId, const std::vector<std::string> &audioIds);

        // ---- 转写相关 ----

        bool SaveTransformWords(uint64_t accountId, const std::string &audioId,
                                const std::vector<models::Trans> &transList, std::vector<uint64_t> &newIds);

        // 获取转写内容列表
        std::vector<models::Trans> GetTransContent(uint64_t accountId, const std::string &audioId);

        // 删除指定audioId的转写内容
        bool DeleteTransByAudioId(uint64_t accountId, const std::string &audioId);

        // 获取转写的最后end_time(用于计算进度)
        int64_t GetLatestTransEndTime(uint64_t accountId, const std::string &audioId);

        // ---- 纪要相关 ----

        bool SaveSummaryContent(uint64_t accountId, const std::string &audioId, const std::string &content);

        // 刷新摘要: 更新状态及相关字段
        bool UpdateForRefreshSummary(uint64_t accountId, const std::string &audioId,
                                     const RefreshSummaryParams &params);

        // 更新录音关键词(纪要完成后回写)
        bool UpdateKeywords(uint64_t accountId, const std::string &audioId, const std::string &keywords);

        // 更新音频文件路径(用于双写迁移后更新DB file_name为软连接路径)
        bool UpdateFileName(uint64_t accountId, const std::string &audioId, const std::string &fileName);

        bool GetRecordingDetailData(uint64_t accountId, const std::string &audioId,
                                    std::vector<models::Trans> &transList, models::Summary &summary);

        // 按audioId查询笔记记录
        models::Note GetNoteByAudioId(uint64_t accountId, const std::string &audioId);

    private:
        MeetingDaoManager();
        ~MeetingDaoManager() = default;

        static std::string GenKey(uint64_t accountId, const std::string &audioId);
        void InvalidateAudio(const std::string &audioId);
        bool DeleteRelationsForAudio(uint64_t accountId, const std::string &audioId);

        AudioDao mAudioDao;
        TransDao mTransDao;
        SummaryDao mSummaryDao;
        NoteDao mNoteDao;
        AudioCacheType mAudioCache;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MANAGERS_MEETING_DAO_MANAGER_H
