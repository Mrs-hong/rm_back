//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <string>
#include <use.h>

#include "common/audio_enums.h"
#include "common/common.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/audio_enums.h"
#include "dao/audio_dao.h"
#include "dao/models/bms_audio.h"

namespace qifeng_ca {

    constexpr std::string_view AudioSelectSQL =
        "SELECT id, audio_id, account_id, source, is_recording, theme, moderator, attendees, "
        "places, file_name, recording_time, total_time, status, plan_finish_time, "
        "remark, timestamp, kind, keywords, update_time, markers, sum_model_name, "
        "note_id, trans_duration, sum_duration, "
        "trans_retry_count, trans_last_progress, sum_last_progress, "
        "trans_last_update_time, trans_start_time, sum_start_time, trans_error_code, "
        "message, use_note, re_summury, sum_word_count "
        "FROM audios";

    static void MapBasicAudioFields(const soci::row &row, models::Audio &audio) {
        audio.mId = row.get<uint64_t>(0);
        audio.mAudioId = row.get<std::string>(1);
        audio.mAccountId = row.get<uint64_t>(2);
        audio.mSource = row.get<int>(3);
        audio.mIsRecording = row.get<int>(4);
        audio.mFileName = row.get<std::string>(9);
        audio.mRecordingTime = row.get<int64_t>(10);
        audio.mTotalTime = row.get<int>(11);
        audio.mStatus = row.get<int>(12);
        audio.mPlanFinishTime = row.get<int64_t>(13);
        audio.mTimestamp = row.get<int64_t>(15);
        audio.mKind = row.get<int>(16);
        audio.mUpdateTime = row.get<int64_t>(18);
        audio.mNoteId = row.get<uint64_t>(21);
    }

    static void MapNullableStringFields(const soci::row &row, models::Audio &audio) {
        soci::indicator ind = row.get_indicator(5);
        if (ind == soci::i_ok) {
            audio.mTheme = row.get<std::string>(5);
        }
        ind = row.get_indicator(6);
        if (ind == soci::i_ok) {
            audio.mModerator = row.get<std::string>(6);
        }
        ind = row.get_indicator(7);
        if (ind == soci::i_ok) {
            audio.mAttendees = row.get<std::string>(7);
        }
        ind = row.get_indicator(8);
        if (ind == soci::i_ok) {
            audio.mPlaces = row.get<std::string>(8);
        }
        ind = row.get_indicator(14);
        if (ind == soci::i_ok) {
            audio.mRemark = row.get<std::string>(14);
        }
        ind = row.get_indicator(17);
        if (ind == soci::i_ok) {
            audio.mKeywords = row.get<std::string>(17);
        }
        ind = row.get_indicator(19);
        if (ind == soci::i_ok) {
            audio.mMarkers = row.get<std::string>(19);
        }
        ind = row.get_indicator(20);
        if (ind == soci::i_ok) {
            audio.mSumModelName = row.get<std::string>(20);
        }
    }

    static void MapDurationFields(const soci::row &row, models::Audio &audio) {
        soci::indicator ind = row.get_indicator(22);
        if (ind == soci::i_ok) {
            audio.mTransDuration = row.get<int64_t>(22);
        }
        ind = row.get_indicator(23);
        if (ind == soci::i_ok) {
            audio.mSumDuration = row.get<int64_t>(23);
        }
    }

    static void MapTransFields(const soci::row &row, models::Audio &audio) {
        audio.mTransRetryCount = row.get<int>(24);
        audio.mTransLastProgress = row.get<int>(25);
        audio.mSumLastProgress = row.get<int>(26);
        audio.mTransLastUpdateTime = row.get<int64_t>(27);
        audio.mTransStartTime = row.get<int64_t>(28);
        audio.mSumStartTime = row.get<int64_t>(29);
        audio.mTransErrorCode = row.get<int>(30);
    }

    static void MapRefreshSummaryFields(const soci::row &row, models::Audio &audio) {
        soci::indicator ind = row.get_indicator(31);
        if (ind == soci::i_ok) {
            audio.mMessage = row.get<std::string>(31);
        }
        audio.mUseNote = row.get<int>(32);
        audio.mReSummury = row.get<int>(33);
        audio.mSumWordCount = row.get<int>(34);
    }

    static void MapRowToAudio(const soci::row &row, models::Audio &audio) {
        MapBasicAudioFields(row, audio);
        MapNullableStringFields(row, audio);
        MapDurationFields(row, audio);
        MapTransFields(row, audio);
        MapRefreshSummaryFields(row, audio);
    }

    models::Audio AudioDao::GetById(uint64_t accountId, uint64_t id) {
        models::Audio audio;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(AudioSelectSQL) << " WHERE account_id = :account_id AND id = :id",
                 soci::use(accountId, "account_id"), soci::use(id, "id"));
            for (const soci::row &row : rs) {
                MapRowToAudio(row, audio);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::GetById failed: " << e.what();
        }
        return audio;
    }

    models::Audio AudioDao::GetByAudioId(uint64_t accountId, const std::string &audioId) {
        models::Audio audio;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs = (session.prepare << std::string(AudioSelectSQL)
                                                          << " WHERE account_id = :account_id AND audio_id = :audio_id",
                                          soci::use(accountId, "account_id"), soci::use(audioId, "audio_id"));
            for (const soci::row &row : rs) {
                MapRowToAudio(row, audio);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::GetByAudioId failed: " << e.what();
        }
        return audio;
    }

    models::Audio AudioDao::GetByAudioIdGlobal(const std::string &audioId) {
        models::Audio audio;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(AudioSelectSQL) << " WHERE audio_id = :audio_id",
                 soci::use(audioId, "audio_id"));
            for (const soci::row &row : rs) {
                MapRowToAudio(row, audio);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::GetByAudioIdGlobal failed: " << e.what();
        }
        return audio;
    }

    models::Audio AudioDao::GetByIdGlobal(uint64_t id) {
        models::Audio audio;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(AudioSelectSQL) << " WHERE id = :id", soci::use(id, "id"));
            for (const soci::row &row : rs) {
                MapRowToAudio(row, audio);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::GetByIdGlobal failed: " << e.what();
        }
        return audio;
    }

    models::Audio AudioDao::GetByFileName(uint64_t accountId, const std::string &fileName) {
        models::Audio audio;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(AudioSelectSQL)
                                 << " WHERE account_id = :account_id AND file_name = :file_name",
                 soci::use(accountId, "account_id"), soci::use(fileName, "file_name"));
            for (const soci::row &row : rs) {
                MapRowToAudio(row, audio);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::GetByFileName failed: " << e.what();
        }
        return audio;
    }

    std::vector<std::string> AudioDao::GetAudioIdsByAccountId(uint64_t accountId) {
        std::vector<std::string> audioIds;
        try {
            soci::session session = GetSession();
            soci::rowset<std::string> rs =
                (session.prepare << "SELECT audio_id FROM audios WHERE account_id = :account_id",
                 soci::use(accountId, "account_id"));
            for (const auto &audioId : rs) {
                audioIds.push_back(audioId);
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::GetAudioIdsByAccountId failed: " << e.what();
        }
        return audioIds;
    }

    bool AudioDao::Insert(const models::Audio &audio) {
        try {
            soci::session session = GetSession();
            session << "INSERT INTO audios (audio_id, account_id, source, is_recording, theme, "
                       "moderator, attendees, places, file_name, recording_time, "
                       "total_time, status, plan_finish_time, remark, timestamp, "
                       "kind, keywords, update_time, markers, sum_model_name, note_id, "
                       "trans_duration, sum_duration, "
                       "trans_retry_count, trans_last_progress, sum_last_progress, "
                       "trans_last_update_time, trans_start_time, sum_start_time, trans_error_code) VALUES "
                       "(:audio_id, :account_id, :source, :is_recording, :theme, "
                       ":moderator, :attendees, :places, :file_name, :recording_time, "
                       ":total_time, :status, :plan_finish_time, :remark, :timestamp, "
                       ":kind, :keywords, :update_time, :markers, :sum_model_name, :note_id, "
                       ":trans_duration, :sum_duration, "
                       ":trans_retry_count, :trans_last_progress, :sum_last_progress, "
                       ":trans_last_update_time, :trans_start_time, :sum_start_time, :trans_error_code)",
                soci::use(audio.mAudioId, "audio_id"), soci::use(audio.mAccountId, "account_id"),
                soci::use(audio.mSource, "source"), soci::use(audio.mIsRecording, "is_recording"),
                soci::use(audio.mTheme, "theme"), soci::use(audio.mModerator, "moderator"),
                soci::use(audio.mAttendees, "attendees"), soci::use(audio.mPlaces, "places"),
                soci::use(audio.mFileName, "file_name"), soci::use(audio.mRecordingTime, "recording_time"),
                soci::use(audio.mTotalTime, "total_time"), soci::use(audio.mStatus, "status"),
                soci::use(audio.mPlanFinishTime, "plan_finish_time"), soci::use(audio.mRemark, "remark"),
                soci::use(audio.mTimestamp, "timestamp"), soci::use(audio.mKind, "kind"),
                soci::use(audio.mKeywords, "keywords"), soci::use(audio.mUpdateTime, "update_time"),
                soci::use(audio.mMarkers, "markers"), soci::use(audio.mSumModelName, "sum_model_name"),
                soci::use(audio.mNoteId, "note_id"), soci::use(audio.mTransDuration, "trans_duration"),
                soci::use(audio.mSumDuration, "sum_duration"), soci::use(audio.mTransRetryCount, "trans_retry_count"),
                soci::use(audio.mTransLastProgress, "trans_last_progress"),
                soci::use(audio.mSumLastProgress, "sum_last_progress"),
                soci::use(audio.mTransLastUpdateTime, "trans_last_update_time"),
                soci::use(audio.mTransStartTime, "trans_start_time"), soci::use(audio.mSumStartTime, "sum_start_time"),
                soci::use(audio.mTransErrorCode, "trans_error_code");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::Insert failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::Update(uint64_t accountId, const models::Audio &audio) {
        try {
            soci::session session = GetSession();
            session << "UPDATE audios SET theme = :theme, moderator = :moderator, "
                       "attendees = :attendees, places = :places, recording_time = :recording_time, "
                       "kind = :kind, remark = :remark, update_time = :update_time "
                       "WHERE account_id = :account_id AND id = :id",
                soci::use(audio.mTheme, "theme"), soci::use(audio.mModerator, "moderator"),
                soci::use(audio.mAttendees, "attendees"), soci::use(audio.mPlaces, "places"),
                soci::use(audio.mRecordingTime, "recording_time"), soci::use(audio.mKind, "kind"),
                soci::use(audio.mRemark, "remark"), soci::use(audio.mUpdateTime, "update_time"),
                soci::use(accountId, "account_id"), soci::use(audio.mId, "id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::Update failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateByAudioId(uint64_t accountId, const models::Audio &audio) {
        try {
            soci::session session = GetSession();
            session << "UPDATE audios SET theme = :theme, moderator = :moderator, "
                       "attendees = :attendees, places = :places, recording_time = :recording_time, "
                       "is_recording = :is_recording, kind = :kind, remark = :remark, "
                       "update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(audio.mTheme, "theme"), soci::use(audio.mModerator, "moderator"),
                soci::use(audio.mAttendees, "attendees"), soci::use(audio.mPlaces, "places"),
                soci::use(audio.mRecordingTime, "recording_time"), soci::use(audio.mIsRecording, "is_recording"),
                soci::use(audio.mKind, "kind"), soci::use(audio.mRemark, "remark"),
                soci::use(audio.mUpdateTime, "update_time"), soci::use(accountId, "account_id"),
                soci::use(audio.mAudioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateByAudioId failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::DeleteById(uint64_t accountId, uint64_t id) {
        try {
            soci::session session = GetSession();
            session << "DELETE FROM audios WHERE account_id = :account_id AND id = :id",
                soci::use(accountId, "account_id"), soci::use(id, "id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::DeleteById failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::DeleteByIds(uint64_t accountId, const std::vector<uint64_t> &ids) {
        if (ids.empty()) {
            return true;
        }
        try {
            soci::session session = GetSession();
            for (const auto &id : ids) {
                session << "DELETE FROM audios WHERE account_id = :account_id AND id = :id",
                    soci::use(accountId, "account_id"), soci::use(id, "id");
            }
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::DeleteByIds failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::DeleteByAudioId(uint64_t accountId, const std::string &audioId) {
        try {
            soci::session session = GetSession();
            session << "DELETE FROM audios WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::DeleteByAudioId failed: " << e.what();
            return false;
        }
    }

    int64_t AudioDao::GetTotalDuration(uint64_t accountId) {
        int64_t total = 0;
        try {
            soci::session session = GetSession();
            session << "SELECT COALESCE(SUM(total_time), 0) FROM audios WHERE account_id = :account_id",
                soci::use(accountId, "account_id"), soci::into(total);
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::GetTotalDuration failed: " << e.what();
        }
        return total;
    }

    int64_t AudioDao::GetTotalDuration() {
        int64_t total = 0;
        try {
            soci::session session = GetSession();
            session << "SELECT COALESCE(SUM(total_time), 0) FROM audios", soci::into(total);
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::GetTotalDuration(global) failed: " << e.what();
        }
        return total;
    }

    static std::string BuildAudioWhereClause(const AudioSearchFilter &filter) {  // NOLINT
        // 忽略删除中的音频
        std::string where = " WHERE status != 100";
        if (!filter.mIgnoreAccountId) {
            // 按用户组控制音频可见范围:
            //   管理员(ADMINISTRATOR)=全部可见, 访客(GUEST)=仅访客, 普通用户=本人+访客
            if (filter.mGroupId == Authority::ADMINISTRATOR) {
                // 管理员可见全部音频，不加account_id过滤
            } else if (filter.mGroupId == Authority::GUEST) {
                where += " AND account_id = :guest_account_id";
            } else {
                where += " AND account_id IN (:account_id, :guest_account_id)";
            }
        }

        if (!filter.mKeyword.empty()) {
            if (where.empty()) {
                where = " WHERE ";
            } else {
                where += " AND ";
            }
            where += "(theme LIKE :keyword OR remark LIKE :keyword_remark OR keywords LIKE :keyword_kw)";
        }
        if (filter.mStartTime > 0) {
            if (where.empty()) {
                where = " WHERE ";
            } else {
                where += " AND ";
            }
            where += "timestamp >= :start_time";
        }
        if (filter.mEndTime > 0) {
            if (where.empty()) {
                where = " WHERE ";
            } else {
                where += " AND ";
            }
            where += "timestamp <= :end_time";
        }
        if (!filter.mStatusList.empty()) {
            if (where.empty()) {
                where = " WHERE ";
            } else {
                where += " AND ";
            }
            where += "status IN (";
            for (size_t i = 0; i < filter.mStatusList.size(); ++i) {
                if (i > 0) {
                    where += ",";
                }
                where += std::to_string(filter.mStatusList[i]);
            }
            where += ")";
        }
        if (filter.mIsRecording >= 0) {
            if (where.empty()) {
                where = " WHERE ";
            } else {
                where += " AND ";
            }
            where += "is_recording = :is_recording";
        }
        if (!filter.mSponsorAccountIds.empty()) {
            if (where.empty()) {
                where = " WHERE ";
            } else {
                where += " AND ";
            }
            where += "account_id IN (";
            for (size_t i = 0; i < filter.mSponsorAccountIds.size(); ++i) {
                if (i > 0) {
                    where += ",";
                }
                where += std::to_string(filter.mSponsorAccountIds[i]);
            }
            where += ")";
        }
        return where;
    }

    struct AudioBoundValues {
        uint64_t mAccountId = 0;
        uint64_t mGuestAccountId = 0;
        std::string mLikeKeyword;
        std::string mLikeKeywordRemark;
        std::string mLikeKeywordKw;
        int64_t mStartTime = 0;
        int64_t mEndTime = 0;
        int mPageSize = 20;
        int mOffset = 0;
        int mIsRecording = -1;
    };

    static void PrepareAudioBoundValues(const AudioSearchFilter &filter, AudioBoundValues &vals) {
        vals.mAccountId = filter.mAccountId;
        vals.mGuestAccountId = GetGuestAccountId();
        if (!filter.mKeyword.empty()) {
            vals.mLikeKeyword = "%" + SanitizeKeyword(filter.mKeyword) + "%";
            vals.mLikeKeywordRemark = "%" + SanitizeKeyword(filter.mKeyword) + "%";
            vals.mLikeKeywordKw = "%" + SanitizeKeyword(filter.mKeyword) + "%";
        }
        vals.mStartTime = filter.mStartTime;
        vals.mEndTime = filter.mEndTime;
        vals.mPageSize = filter.mPageSize;
        vals.mOffset = (filter.mCurrent - 1) * filter.mPageSize;
        vals.mIsRecording = filter.mIsRecording;
    }

    static void BindAudioFilterParams(soci::statement &stmt, const AudioSearchFilter &filter,
                                      const AudioBoundValues &vals) {
        if (!filter.mIgnoreAccountId) {
            // 与 BuildAudioWhereClause 保持一致:
            //   管理员=不过滤, 访客=仅访客, 普通用户=本人+访客
            if (filter.mGroupId == Authority::GUEST) {
                stmt.exchange(soci::use(vals.mGuestAccountId, "guest_account_id"));
            } else if (filter.mGroupId != Authority::ADMINISTRATOR) {
                stmt.exchange(soci::use(vals.mAccountId, "account_id"));
                stmt.exchange(soci::use(vals.mGuestAccountId, "guest_account_id"));
            }
        }
        if (!filter.mKeyword.empty()) {
            stmt.exchange(soci::use(vals.mLikeKeyword, "keyword"));
            stmt.exchange(soci::use(vals.mLikeKeywordRemark, "keyword_remark"));
            stmt.exchange(soci::use(vals.mLikeKeywordKw, "keyword_kw"));
        }
        if (filter.mStartTime > 0) {
            stmt.exchange(soci::use(vals.mStartTime, "start_time"));
        }
        if (filter.mEndTime > 0) {
            stmt.exchange(soci::use(vals.mEndTime, "end_time"));
        }
        if (filter.mIsRecording >= 0) {
            stmt.exchange(soci::use(vals.mIsRecording, "is_recording"));
        }
    }

    AudioSearchResult AudioDao::Search(const AudioSearchFilter &filter) {
        AudioSearchResult result;
        try {
            soci::session session = GetSession();
            std::string whereClause = BuildAudioWhereClause(filter);
            AudioBoundValues vals;
            PrepareAudioBoundValues(filter, vals);

            std::string countSql = "SELECT COUNT(*) FROM audios" + whereClause;
            soci::statement countStmt = session.prepare << countSql;
            BindAudioFilterParams(countStmt, filter, vals);
            countStmt.exchange(soci::into(result.mTotal));
            countStmt.define_and_bind();
            countStmt.execute(true);

            std::string querySql = std::string(AudioSelectSQL) + whereClause +
                                   " ORDER BY is_recording DESC, timestamp DESC LIMIT :limit OFFSET :offset";
            soci::statement stmt = session.prepare << querySql;
            BindAudioFilterParams(stmt, filter, vals);
            stmt.exchange(soci::use(vals.mPageSize, "limit"));
            stmt.exchange(soci::use(vals.mOffset, "offset"));

            soci::row row;
            stmt.exchange(soci::into(row));
            stmt.define_and_bind();
            if (!stmt.execute(true)) {
                return result;
            }
            while (true) {
                models::Audio audio;
                MapRowToAudio(row, audio);
                result.mRecords.push_back(std::move(audio));
                if (!stmt.fetch()) {
                    break;
                }
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::Search failed: " << e.what();
        }
        return result;
    }

    bool AudioDao::UpdateMarkers(uint64_t accountId, uint64_t id, const std::string &markersJson) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET markers = :markers, update_time = :update_time "
                       "WHERE account_id = :account_id AND id = :id",
                soci::use(markersJson, "markers"), soci::use(timeMs, "update_time"), soci::use(accountId, "account_id"),
                soci::use(id, "id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateMarkers failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateMarkersByAudioId(uint64_t accountId, const std::string &audioId,
                                          const std::string &markersJson) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET markers = :markers, update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(markersJson, "markers"), soci::use(timeMs, "update_time"), soci::use(accountId, "account_id"),
                soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateMarkersByAudioId failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateStatus(uint64_t accountId, uint64_t id, int status) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET status = :status, update_time = :update_time "
                       "WHERE account_id = :account_id AND id = :id",
                soci::use(status, "status"), soci::use(timeMs, "update_time"), soci::use(accountId, "account_id"),
                soci::use(id, "id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateStatus failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateStatusByAudioId(uint64_t accountId, const std::string &audioId, int status) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET status = :status, update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(status, "status"), soci::use(timeMs, "update_time"), soci::use(accountId, "account_id"),
                soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateStatusByAudioId failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateStatusByAudioId(uint64_t accountId, const std::string &audioId, int status,
                                         const std::string &message) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET status = :status, message = :message, update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(status, "status"), soci::use(message, "message"), soci::use(timeMs, "update_time"),
                soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateStatusByAudioId failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateTransProgress(uint64_t accountId, const std::string &audioId, int progress) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET trans_last_progress = :progress, "
                       "trans_last_update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(progress, "progress"), soci::use(timeMs, "update_time"), soci::use(accountId, "account_id"),
                soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateTransProgress failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateTransStartTime(uint64_t accountId, const std::string &audioId, int64_t startTime) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET trans_start_time = :start_time, update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(startTime, "start_time"), soci::use(timeMs, "update_time"),
                soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateTransStartTime failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateSumStartTime(uint64_t accountId, const std::string &audioId, int64_t startTime) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET sum_start_time = :start_time, update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(startTime, "start_time"), soci::use(timeMs, "update_time"),
                soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateSumStartTime failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateTransPlanFinishTime(uint64_t accountId, const std::string &audioId, int64_t planFinishTs) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET plan_finish_time = :plan_finish_time, "
                       "update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(planFinishTs, "plan_finish_time"), soci::use(timeMs, "update_time"),
                soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateTransPlanFinishTime failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateSumProgress(uint64_t accountId, const std::string &audioId, int progress,
                                     int64_t planFinishTs) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET sum_last_progress = :progress, plan_finish_time = :plan_finish, "
                       "update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(progress, "progress"), soci::use(planFinishTs, "plan_finish"),
                soci::use(timeMs, "update_time"), soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateSumProgress failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateIsRecording(uint64_t accountId, const std::string &audioId, int isRecording) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET is_recording = :is_recording, update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(isRecording, "is_recording"), soci::use(timeMs, "update_time"),
                soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateIsRecording failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateStatusAndRecording(uint64_t accountId, const std::string &audioId, int status,
                                            int isRecording) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET status = :status, is_recording = :is_recording, update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(status, "status"), soci::use(isRecording, "is_recording"), soci::use(timeMs, "update_time"),
                soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateStatusAndRecording failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateTotalTime(uint64_t accountId, const std::string &audioId, int totalTime) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET total_time = :total_time, update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(totalTime, "total_time"), soci::use(timeMs, "update_time"),
                soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateTotalTime failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateTransDuration(uint64_t accountId, const std::string &audioId, int64_t transDuration) {
        try {
            soci::session session = GetSession();
            session << "UPDATE audios SET trans_duration = :trans_duration "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(transDuration, "trans_duration"), soci::use(accountId, "account_id"),
                soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateTransDuration failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateSumDuration(uint64_t accountId, const std::string &audioId, int64_t sumDuration) {
        try {
            soci::session session = GetSession();
            session << "UPDATE audios SET sum_duration = :sum_duration "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(sumDuration, "sum_duration"), soci::use(accountId, "account_id"),
                soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateSumDuration failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateNoteId(uint64_t accountId, const std::string &audioId, uint64_t noteId) {
        try {
            soci::session session = GetSession();
            session << "UPDATE audios SET note_id = :note_id WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(noteId, "note_id"), soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateNoteId failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateKeywords(uint64_t accountId, const std::string &audioId, const std::string &keywords) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET keywords = :keywords, update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(keywords, "keywords"), soci::use(timeMs, "update_time"), soci::use(accountId, "account_id"),
                soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateKeywords failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateForRefreshSummary(uint64_t accountId, const std::string &audioId,
                                           const RefreshSummaryParams &params) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            int status = static_cast<int>(AudioStatus::WaitSummary);
            int useNoteVal = params.mUseNote ? 1 : 0;
            int reSummury = 1;
            const std::string message = std::string(AudioStatusMsg::WaitReMeetingSummary);
            soci::session session = GetSession();
            session << "UPDATE audios SET status = :status, message = :message, kind = :kind, "
                       "use_note = :use_note, re_summury = :re_summury, sum_word_count = :sum_word_count, "
                       "sum_last_progress = 0, sum_start_time = 0, plan_finish_time = 0, sum_duration = 0, "
                       "update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(status, "status"), soci::use(message, "message"), soci::use(params.mKind, "kind"),
                soci::use(useNoteVal, "use_note"), soci::use(reSummury, "re_summury"),
                soci::use(params.mWordCount, "sum_word_count"), soci::use(timeMs, "update_time"),
                soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateForRefreshSummary failed: " << e.what();
            return false;
        }
    }

    bool AudioDao::UpdateFileName(uint64_t accountId, const std::string &audioId, const std::string &fileName) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE audios SET file_name = :file_name, update_time = :update_time "
                       "WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(fileName, "file_name"), soci::use(timeMs, "update_time"), soci::use(accountId, "account_id"),
                soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::UpdateFileName failed: " << e.what();
            return false;
        }
    }

    int64_t AudioDao::CountByStatus(const std::vector<int> &statusList) {
        try {
            soci::session session = GetSession();
            int64_t count = 0;
            // statusList 为空: 统计所有非录音中(Meeting状态)的音频
            if (statusList.empty()) {
                int meeting = static_cast<int>(AudioStatus::Meeting);
                soci::statement stmt = (session.prepare << "SELECT COUNT(*) FROM audios WHERE status != :status",
                                        soci::use(meeting), soci::into(count));
                stmt.define_and_bind();
                stmt.execute(true);
                return count;
            }

            // 按指定状态列表统计
            std::string sql = "SELECT COUNT(*) FROM audios WHERE status IN (";
            for (size_t i = 0; i < statusList.size(); ++i) {
                if (i > 0) {
                    sql += ",";
                }
                sql += std::to_string(statusList[i]);
            }
            sql += ")";
            soci::statement stmt = (session.prepare << sql, soci::into(count));
            stmt.execute(true);
            return count;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AudioDao::CountByStatus failed: " << e.what();
            return 0;
        }
    }

}  // namespace qifeng_ca
