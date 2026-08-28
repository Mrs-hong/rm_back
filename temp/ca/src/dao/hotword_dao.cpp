/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <cstdint>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "common/common.h"
#include "dao/hotword_dao.h"
#include "dao/models/bms_hotword.h"

namespace qifeng_ca {

    constexpr const std::string_view HotwordSelectSQL =
        "SELECT id, account_id, word, remark, status, create_time, update_time FROM hot_words";

    static void MapRowToHotWord(const soci::row &row, models::HotWord &hw) {
        hw.mId = row.get<uint64_t>(0);
        hw.mAccountId = row.get<uint64_t>(1);
        hw.mWord = row.get<std::string>(2);

        soci::indicator remarkInd = row.get_indicator(3);
        if (remarkInd == soci::i_ok) {
            hw.mRemark = row.get<std::string>(3);
        }

        hw.mStatus = row.get<int>(4);
        hw.mCreateTime = row.get<int64_t>(5);
        hw.mUpdateTime = row.get<int64_t>(6);
    }

    models::HotWord HotwordDao::GetById(uint64_t accountId, uint64_t id) {
        SLOG_DEBUG << "HotwordDao::GetById - accountId: " << accountId << ", id: " << id;
        models::HotWord hw;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(HotwordSelectSQL) << " WHERE account_id = :account_id AND id = :id",
                 soci::use(accountId, "account_id"), soci::use(id, "id"));
            for (const soci::row &row : rs) {
                MapRowToHotWord(row, hw);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "HotwordDao::GetById failed: " << e.what();
        }
        return hw;
    }

    models::HotWord HotwordDao::GetByWord(uint64_t accountId, const std::string &word) {
        SLOG_DEBUG << "HotwordDao::GetByWord - accountId: " << accountId << ", word: " << word;
        models::HotWord hw;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(HotwordSelectSQL) << " WHERE account_id = :account_id AND word = :word",
                 soci::use(accountId, "account_id"), soci::use(word, "word"));
            for (const soci::row &row : rs) {
                MapRowToHotWord(row, hw);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "HotwordDao::GetByWord failed: " << e.what();
        }
        return hw;
    }

    int64_t HotwordDao::Count(uint64_t accountId) {
        int64_t cnt = 0;
        try {
            soci::session session = GetSession();
            session << "SELECT COUNT(*) FROM hot_words WHERE account_id = :account_id",
                soci::use(accountId, "account_id"), soci::into(cnt);
        } catch (const std::exception &e) {
            SLOG_ERROR << "HotwordDao::Count failed: " << e.what();
        }
        return cnt;
    }

    bool HotwordDao::Insert(const models::HotWord &hotword) {
        SLOG_DEBUG << "HotwordDao::Insert - accountId: " << hotword.mAccountId << ", word: " << hotword.mWord;
        try {
            soci::session session = GetSession();
            session << "INSERT INTO hot_words (account_id, word, remark, status, create_time, update_time) "
                       "VALUES (:account_id, :word, :remark, :status, :create_time, :update_time)",
                soci::use(hotword.mAccountId, "account_id"), soci::use(hotword.mWord, "word"),
                soci::use(hotword.mRemark, "remark"), soci::use(hotword.mStatus, "status"),
                soci::use(hotword.mCreateTime, "create_time"), soci::use(hotword.mUpdateTime, "update_time");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "HotwordDao::Insert failed: " << e.what();
            return false;
        }
    }

    bool HotwordDao::Update(uint64_t accountId, const models::HotWord &hotword) {
        SLOG_DEBUG << "HotwordDao::Update - accountId: " << accountId << ", id: " << hotword.mId;
        try {
            soci::session session = GetSession();
            session << "UPDATE hot_words SET status = :status, remark = :remark, update_time = :update_time "
                       "WHERE account_id = :account_id AND id = :id",
                soci::use(hotword.mStatus, "status"), soci::use(hotword.mRemark, "remark"),
                soci::use(hotword.mUpdateTime, "update_time"), soci::use(accountId, "account_id"),
                soci::use(hotword.mId, "id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "HotwordDao::Update failed: " << e.what();
            return false;
        }
    }

    bool HotwordDao::DeleteByIds(uint64_t accountId, const std::vector<uint64_t> &ids) {
        SLOG_DEBUG << "HotwordDao::DeleteByIds - accountId: " << accountId << ", count: " << ids.size();
        if (ids.empty()) {
            return true;
        }
        try {
            soci::session session = GetSession();
            for (auto id : ids) {
                session << "DELETE FROM hot_words WHERE account_id = :account_id AND id = :id",
                    soci::use(accountId, "account_id"), soci::use(id, "id");
            }
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "HotwordDao::DeleteByIds failed: " << e.what();
            return false;
        }
    }

    static std::string BuildHotwordWhereClause(const HotwordSearchFilter &filter) {
        std::string where = " WHERE account_id = :account_id";
        if (!filter.mKeyword.empty()) {
            where += " AND (word LIKE :keyword OR remark LIKE :keyword_remark)";
        }
        if (filter.mStatus == 1 || filter.mStatus == 2) {
            where += " AND status = :status";
        }
        if (filter.mStartTime != 0) {
            where += " AND create_time >= :start_time";
        }
        if (filter.mEndTime != 0) {
            where += " AND create_time <= :end_time";
        }
        return where;
    }

    struct HotwordBoundValues {
        uint64_t mAccountId = 0;
        std::string mLikeKeyword;
        std::string mLikeKeywordRemark;
        int32_t mStatus = 0;
        int64_t mStartTime = 0;
        int64_t mEndTime = 0;
        int32_t mPageSize = 20;
        int mOffset = 0;
    };

    static void PrepareHotwordBoundValues(const HotwordSearchFilter &filter, HotwordBoundValues &vals) {
        vals.mAccountId = filter.mAccountId;
        if (!filter.mKeyword.empty()) {
            vals.mLikeKeyword = "%" + SanitizeKeyword(filter.mKeyword) + "%";
            vals.mLikeKeywordRemark = "%" + SanitizeKeyword(filter.mKeyword) + "%";
        }
        if (filter.mStatus == 1 || filter.mStatus == 2) {
            vals.mStatus = filter.mStatus;
        }
        if (filter.mStartTime != 0) {
            vals.mStartTime = filter.mStartTime;
        }
        if (filter.mEndTime != 0) {
            vals.mEndTime = filter.mEndTime;
        }
        vals.mPageSize = filter.mPageSize;
        vals.mOffset = (filter.mCurrent - 1) * filter.mPageSize;
    }

    static void BindHotwordFilterParams(soci::statement &stmt, const HotwordSearchFilter &filter,
                                        const HotwordBoundValues &vals) {
        stmt.exchange(soci::use(vals.mAccountId, "account_id"));
        if (!filter.mKeyword.empty()) {
            stmt.exchange(soci::use(vals.mLikeKeyword, "keyword"));
            stmt.exchange(soci::use(vals.mLikeKeywordRemark, "keyword_remark"));
        }
        if (filter.mStatus == 1 || filter.mStatus == 2) {
            stmt.exchange(soci::use(vals.mStatus, "status"));
        }
        if (filter.mStartTime != 0) {
            stmt.exchange(soci::use(vals.mStartTime, "start_time"));
        }
        if (filter.mEndTime != 0) {
            stmt.exchange(soci::use(vals.mEndTime, "end_time"));
        }
    }

    static void FetchHotwordRecords(soci::statement &stmt, std::vector<models::HotWord> &records) {
        soci::row row;
        stmt.exchange(soci::into(row));
        stmt.define_and_bind();
        if (!stmt.execute(true)) {
            return;
        }

        while (true) {
            models::HotWord hw;
            MapRowToHotWord(row, hw);
            records.push_back(std::move(hw));
            if (!stmt.fetch()) {
                break;
            }
        }
    }

    HotwordSearchResult HotwordDao::Search(const HotwordSearchFilter &filter) {
        SLOG_DEBUG << "HotwordDao::Search - accountId: " << filter.mAccountId << ", keyword: " << filter.mKeyword
                   << ", status: " << filter.mStatus;
        HotwordSearchResult result;
        try {
            soci::session session = GetSession();
            std::string whereClause = BuildHotwordWhereClause(filter);
            HotwordBoundValues vals;
            PrepareHotwordBoundValues(filter, vals);

            std::string countSql = "SELECT COUNT(*) FROM hot_words" + whereClause;
            soci::statement countStmt = session.prepare << countSql;
            BindHotwordFilterParams(countStmt, filter, vals);
            countStmt.exchange(soci::into(result.mTotal));
            countStmt.define_and_bind();
            countStmt.execute(true);

            std::string querySql =
                std::string(HotwordSelectSQL) + whereClause + " ORDER BY create_time DESC LIMIT :limit OFFSET :offset";
            soci::statement stmt = session.prepare << querySql;
            BindHotwordFilterParams(stmt, filter, vals);
            stmt.exchange(soci::use(vals.mPageSize, "limit"));
            stmt.exchange(soci::use(vals.mOffset, "offset"));
            FetchHotwordRecords(stmt, result.mRecords);
        } catch (const std::exception &e) {
            SLOG_ERROR << "HotwordDao::Search failed: " << e.what();
        }
        return result;
    }

}  // namespace qifeng_ca
