//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "dao/models/bms_summary.h"
#include "dao/summary_dao.h"

namespace qifeng_ca {

    constexpr std::string_view SummarySelectSQL =
        "SELECT id, account_id, audio_id, content, is_orig, is_discard, timestamp FROM summary";

    static void MapRowToSummary(const soci::row &row, models::Summary &summary) {
        summary.mId = row.get<uint64_t>(0);
        summary.mAccountId = row.get<uint64_t>(1);
        summary.mAudioId = row.get<std::string>(2);

        soci::indicator ind = row.get_indicator(3);
        if (ind == soci::i_ok) {
            summary.mContent = row.get<std::string>(3);
        }

        summary.mIsOrig = row.get<int>(4);
        summary.mIsDiscard = row.get<int>(5);
        summary.mTimestamp = row.get<uint64_t>(6);
    }

    models::Summary SummaryDao::GetByAudioId(uint64_t accountId, const std::string &audioId) {
        models::Summary summary;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(SummarySelectSQL) +
                                        " WHERE account_id = :account_id AND audio_id = :audio_id "
                                        "AND is_discard = 0 ORDER BY timestamp DESC LIMIT 1",
                 soci::use(accountId, "account_id"), soci::use(audioId, "audio_id"));
            for (const soci::row &row : rs) {
                MapRowToSummary(row, summary);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "SummaryDao::GetByAudioId failed: " << e.what();
        }
        return summary;
    }

    bool SummaryDao::Insert(const models::Summary &summary) {
        try {
            soci::session session = GetSession();
            session << "INSERT INTO summary (account_id, audio_id, content, is_orig, is_discard, timestamp) "
                       "VALUES (:account_id, :audio_id, :content, :is_orig, :is_discard, :timestamp)",
                soci::use(summary.mAccountId, "account_id"), soci::use(summary.mAudioId, "audio_id"),
                soci::use(summary.mContent, "content"), soci::use(summary.mIsOrig, "is_orig"),
                soci::use(summary.mIsDiscard, "is_discard"), soci::use(summary.mTimestamp, "timestamp");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "SummaryDao::Insert failed: " << e.what();
            return false;
        }
    }

    bool SummaryDao::UpdateContent(uint64_t accountId, const std::string &audioId, const std::string &content) {
        try {
            soci::session session = GetSession();
            session << "UPDATE summary SET content = :content "
                       "WHERE account_id = :account_id AND audio_id = :audio_id AND is_discard = 0",
                soci::use(content, "content"), soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "SummaryDao::UpdateContent failed: " << e.what();
            return false;
        }
    }

    bool SummaryDao::DeleteByAudioId(uint64_t accountId, const std::string &audioId) {
        try {
            soci::session session = GetSession();
            session << "DELETE FROM summary WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "SummaryDao::DeleteByAudioId failed: " << e.what();
            return false;
        }
    }

    static SummaryStats DoCountStats(soci::session &session, const std::string &whereClause) {
        SummaryStats stats;
        try {
            std::string sql =
                "SELECT "
                "SUM(CASE WHEN is_discard = 0 AND content IS NOT NULL AND content != '' THEN 1 ELSE 0 END), "
                "SUM(CASE WHEN is_discard = 0 AND (content IS NULL OR content = '') THEN 1 ELSE 0 END), "
                "COUNT(CASE WHEN is_discard = 0 THEN 1 END) "
                "FROM summary";
            if (!whereClause.empty()) {
                sql += " WHERE " + whereClause;
            }

            soci::row r;
            session << sql, soci::into(r);

            if (r.get_indicator(0) == soci::i_ok) {
                stats.mDone = r.get<int>(0);
            }
            if (r.get_indicator(1) == soci::i_ok) {
                stats.mWaiting = r.get<int>(1);
            }
            if (r.get_indicator(2) == soci::i_ok) {
                stats.mTotal = r.get<int>(2);
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "SummaryDao DoCountStats failed: " << e.what();
        }
        return stats;
    }

    SummaryStats SummaryDao::CountStats() {
        try {
            soci::session session = GetSession();
            return DoCountStats(session, "");
        } catch (const std::exception &e) {
            SLOG_ERROR << "SummaryDao::CountStats failed: " << e.what();
            return {};
        }
    }

    SummaryStats SummaryDao::CountStatsByAccount(uint64_t accountId) {
        try {
            soci::session session = GetSession();
            return DoCountStats(session, "account_id = " + std::to_string(accountId));
        } catch (const std::exception &e) {
            SLOG_ERROR << "SummaryDao::CountStatsByAccount failed: " << e.what();
            return {};
        }
    }

}  // namespace qifeng_ca
