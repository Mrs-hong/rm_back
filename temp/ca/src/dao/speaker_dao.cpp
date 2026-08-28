//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "dao/models/bms_speaker.h"
#include "dao/speaker_dao.h"

namespace qifeng_ca {

    constexpr const std::string_view SpeakerSelectSQL =
        "SELECT id, account_id, number, file_name, speaker, features, dim, "
        "recording_time, total_time, status, remark, last_modify_time, speaker_id FROM speaker";

    static void MapRowToSpeaker(const soci::row &row, models::Speaker &sp) {
        sp.mId = row.get<uint64_t>(0);
        sp.mAccountId = row.get<uint64_t>(1);
        sp.mNumber = row.get<std::string>(2);

        soci::indicator fileNameInd = row.get_indicator(3);
        if (fileNameInd == soci::i_ok) {
            sp.mFileName = row.get<std::string>(3);
        }

        sp.mSpeakerName = row.get<std::string>(4);

        soci::indicator featuresInd = row.get_indicator(5);
        if (featuresInd == soci::i_ok) {
            try {
                sp.mFeatures = row.get<std::string>(5);
            } catch (const std::exception &e) {
                SLOG_WARN << "SpeakerDao: features column read failed (may be NULL BLOB), "
                          << "using empty string: " << e.what();
            }
        }

        sp.mDim = row.get<int>(6);
        sp.mRecordingTime = row.get<int64_t>(7);
        sp.mTotalTime = row.get<int>(8);
        sp.mStatus = row.get<int>(9);

        soci::indicator remarkInd = row.get_indicator(10);
        if (remarkInd == soci::i_ok) {
            sp.mRemark = row.get<std::string>(10);
        }

        sp.mLastModifyTime = row.get<int64_t>(11);

        soci::indicator speakerIdInd = row.get_indicator(12);
        if (speakerIdInd == soci::i_ok) {
            sp.mSpeakerId = row.get<std::string>(12);
        }
    }

    models::Speaker SpeakerDao::GetById(uint64_t accountId, uint64_t id) {
        SLOG_DEBUG << "SpeakerDao::GetById - accountId: " << accountId << ", id: " << id;
        models::Speaker sp;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(SpeakerSelectSQL) << " WHERE account_id = :account_id AND id = :id",
                 soci::use(accountId, "account_id"), soci::use(id, "id"));
            for (const soci::row &row : rs) {
                MapRowToSpeaker(row, sp);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "SpeakerDao::GetById failed: " << e.what();
        }
        return sp;
    }

    models::Speaker SpeakerDao::GetByNumber(uint64_t accountId, const std::string &number) {
        SLOG_DEBUG << "SpeakerDao::GetByNumber - accountId: " << accountId << ", number: " << number;
        models::Speaker sp;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs = (session.prepare << std::string(SpeakerSelectSQL)
                                                          << " WHERE account_id = :account_id AND number = :number",
                                          soci::use(accountId, "account_id"), soci::use(number, "number"));
            for (const soci::row &row : rs) {
                MapRowToSpeaker(row, sp);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "SpeakerDao::GetByNumber failed: " << e.what();
        }
        return sp;
    }

    models::Speaker SpeakerDao::GetBySpeakerName(uint64_t accountId, const std::string &speakerName) {
        SLOG_DEBUG << "SpeakerDao::GetBySpeakerName - accountId: " << accountId << ", speaker: " << speakerName;
        models::Speaker sp;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs = (session.prepare << std::string(SpeakerSelectSQL)
                                                          << " WHERE account_id = :account_id AND speaker = :speaker",
                                          soci::use(accountId, "account_id"), soci::use(speakerName, "speaker"));
            for (const soci::row &row : rs) {
                MapRowToSpeaker(row, sp);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "SpeakerDao::GetBySpeakerName failed: " << e.what();
        }
        return sp;
    }

    models::Speaker SpeakerDao::GetBySpeakerId(uint64_t accountId, const std::string &speakerId) {
        SLOG_DEBUG << "SpeakerDao::GetBySpeakerId - accountId: " << accountId << ", speakerId: " << speakerId;
        models::Speaker sp;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(SpeakerSelectSQL)
                                 << " WHERE account_id = :account_id AND speaker_id = :speaker_id",
                 soci::use(accountId, "account_id"), soci::use(speakerId, "speaker_id"));
            for (const soci::row &row : rs) {
                MapRowToSpeaker(row, sp);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "SpeakerDao::GetBySpeakerId failed: " << e.what();
        }
        return sp;
    }

    std::vector<models::Speaker> SpeakerDao::ListByAccountId(uint64_t accountId) {
        SLOG_DEBUG << "SpeakerDao::ListByAccountId - accountId: " << accountId;
        std::vector<models::Speaker> result;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(SpeakerSelectSQL) << " WHERE account_id = :account_id ORDER BY id",
                 soci::use(accountId, "account_id"));
            for (const soci::row &row : rs) {
                models::Speaker sp;
                MapRowToSpeaker(row, sp);
                result.push_back(std::move(sp));
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "SpeakerDao::ListByAccountId failed: " << e.what();
        }
        return result;
    }

    bool SpeakerDao::Insert(const models::Speaker &speaker) {
        SLOG_DEBUG << "SpeakerDao::Insert - accountId: " << speaker.mAccountId << ", number: " << speaker.mNumber;
        try {
            soci::session session = GetSession();
            session << "INSERT INTO speaker (account_id, number, file_name, speaker, features, dim, "
                       "recording_time, total_time, status, remark, last_modify_time, speaker_id) "
                       "VALUES (:account_id, :number, :file_name, :speaker, :features, :dim, "
                       ":recording_time, :total_time, :status, :remark, :last_modify_time, :speaker_id)",
                soci::use(speaker.mAccountId, "account_id"), soci::use(speaker.mNumber, "number"),
                soci::use(speaker.mFileName, "file_name"), soci::use(speaker.mSpeakerName, "speaker"),
                soci::use(speaker.mFeatures, "features"), soci::use(speaker.mDim, "dim"),
                soci::use(speaker.mRecordingTime, "recording_time"), soci::use(speaker.mTotalTime, "total_time"),
                soci::use(speaker.mStatus, "status"), soci::use(speaker.mRemark, "remark"),
                soci::use(speaker.mLastModifyTime, "last_modify_time"), soci::use(speaker.mSpeakerId, "speaker_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "SpeakerDao::Insert failed: " << e.what();
            return false;
        }
    }

    bool SpeakerDao::Update(uint64_t accountId, const models::Speaker &speaker) {
        SLOG_DEBUG << "SpeakerDao::Update - accountId: " << accountId << ", id: " << speaker.mId;
        try {
            soci::session session = GetSession();
            session << "UPDATE speaker SET number = :number, speaker = :speaker, "
                       "remark = :remark, last_modify_time = :last_modify_time "
                       "WHERE account_id = :account_id AND id = :id",
                soci::use(speaker.mNumber, "number"), soci::use(speaker.mSpeakerName, "speaker"),
                soci::use(speaker.mRemark, "remark"), soci::use(speaker.mLastModifyTime, "last_modify_time"),
                soci::use(accountId, "account_id"), soci::use(speaker.mId, "id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "SpeakerDao::Update failed: " << e.what();
            return false;
        }
    }

    bool SpeakerDao::DeleteById(uint64_t accountId, uint64_t id) {
        SLOG_DEBUG << "SpeakerDao::DeleteById - accountId: " << accountId << ", id: " << id;
        try {
            soci::session session = GetSession();
            session << "DELETE FROM speaker WHERE account_id = :account_id AND id = :id",
                soci::use(accountId, "account_id"), soci::use(id, "id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "SpeakerDao::DeleteById failed: " << e.what();
            return false;
        }
    }

    static std::string BuildSpeakerWhereClause(const SpeakerSearchFilter &filter) {
        std::string where = " WHERE account_id = :account_id";
        if (!filter.mKeyword.empty()) {
            where += " AND (number LIKE :keyword OR speaker LIKE :keyword_speaker "
                     "OR remark LIKE :keyword_remark)";
        }
        return where;
    }

    struct SpeakerBoundValues {
        uint64_t mAccountId = 0;
        std::string mLikeKeyword;
        std::string mLikeKeywordSpeaker;
        std::string mLikeKeywordRemark;
        int32_t mPageSize = 20;
        int mOffset = 0;
    };

    static void PrepareSpeakerBoundValues(const SpeakerSearchFilter &filter, SpeakerBoundValues &vals) {
        vals.mAccountId = filter.mAccountId;
        if (!filter.mKeyword.empty()) {
            vals.mLikeKeyword = "%" + filter.mKeyword + "%";
            vals.mLikeKeywordSpeaker = "%" + filter.mKeyword + "%";
            vals.mLikeKeywordRemark = "%" + filter.mKeyword + "%";
        }
        vals.mPageSize = filter.mPageSize;
        vals.mOffset = (filter.mCurrent - 1) * filter.mPageSize;
    }

    static void BindSpeakerFilterParams(soci::statement &stmt, const SpeakerSearchFilter &filter,
                                        const SpeakerBoundValues &vals) {
        stmt.exchange(soci::use(vals.mAccountId, "account_id"));
        if (!filter.mKeyword.empty()) {
            stmt.exchange(soci::use(vals.mLikeKeyword, "keyword"));
            stmt.exchange(soci::use(vals.mLikeKeywordSpeaker, "keyword_speaker"));
            stmt.exchange(soci::use(vals.mLikeKeywordRemark, "keyword_remark"));
        }
    }

    static void FetchSpeakerRecords(soci::statement &stmt, std::vector<models::Speaker> &records) {
        soci::row row;
        stmt.exchange(soci::into(row));
        stmt.define_and_bind();
        stmt.execute(true);

        while (true) {
            models::Speaker sp;
            MapRowToSpeaker(row, sp);
            records.push_back(std::move(sp));
            if (!stmt.fetch()) {
                break;
            }
        }
    }

    SpeakerSearchResult SpeakerDao::Search(const SpeakerSearchFilter &filter) {
        SLOG_DEBUG << "SpeakerDao::Search - accountId: " << filter.mAccountId << ", keyword: " << filter.mKeyword;
        SpeakerSearchResult result;
        try {
            soci::session session = GetSession();
            std::string whereClause = BuildSpeakerWhereClause(filter);
            SpeakerBoundValues vals;
            PrepareSpeakerBoundValues(filter, vals);

            std::string countSql = "SELECT COUNT(*) FROM speaker" + whereClause;
            soci::statement countStmt = session.prepare << countSql;
            BindSpeakerFilterParams(countStmt, filter, vals);
            countStmt.exchange(soci::into(result.mTotal));
            countStmt.define_and_bind();
            countStmt.execute(true);

            std::string querySql = std::string(SpeakerSelectSQL) + whereClause +
                                   " ORDER BY recording_time DESC LIMIT :limit OFFSET :offset";
            soci::statement stmt = session.prepare << querySql;
            BindSpeakerFilterParams(stmt, filter, vals);
            stmt.exchange(soci::use(vals.mPageSize, "limit"));
            stmt.exchange(soci::use(vals.mOffset, "offset"));
            FetchSpeakerRecords(stmt, result.mRecords);
        } catch (const std::exception &e) {
            SLOG_ERROR << "SpeakerDao::Search failed: " << e.what();
        }
        return result;
    }

}  // namespace qifeng_ca
