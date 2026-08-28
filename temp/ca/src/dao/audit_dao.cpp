/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <cstdint>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "dao/audit_dao.h"
#include "dao/models/bms_audit.h"

namespace qifeng_ca {

    constexpr const std::string_view AuditColumns =
        "id, user_id, user_account, user_name, user_group_id, action, action_desc, "
        "api_path, http_method, success, error_message, client_ip, request_data, create_time";

    constexpr const std::string_view AuditSelectSQL =
        "SELECT id, user_id, user_account, user_name, user_group_id, action, action_desc, "
        "api_path, http_method, success, error_message, client_ip, request_data, create_time "
        "FROM audit_log";

    static void MapRowToAuditRecord(const soci::row &row, models::AuditRecord &record) {
        record.mId = row.get<uint64_t>(0);
        record.mUserId = row.get<uint64_t>(1);
        record.mUserAccount = row.get<std::string>(2);

        soci::indicator userNameInd = row.get_indicator(3);
        if (userNameInd == soci::i_ok) {
            record.mUserName = row.get<std::string>(3);
        }

        record.mUserGroupId = row.get<uint64_t>(4);
        record.mAction = row.get<std::string>(5);
        record.mActionDesc = row.get<std::string>(6);

        soci::indicator apiPathInd = row.get_indicator(7);
        if (apiPathInd == soci::i_ok) {
            record.mApiPath = row.get<std::string>(7);
        }

        soci::indicator methodInd = row.get_indicator(8);
        if (methodInd == soci::i_ok) {
            record.mHttpMethod = row.get<std::string>(8);
        }

        record.mSuccess = row.get<int>(9) != 0;

        soci::indicator errorInd = row.get_indicator(10);
        if (errorInd == soci::i_ok) {
            record.mErrorMessage = row.get<std::string>(10);
        }

        soci::indicator ipInd = row.get_indicator(11);
        if (ipInd == soci::i_ok) {
            record.mClientIp = row.get<std::string>(11);
        }

        soci::indicator requestInd = row.get_indicator(12);
        if (requestInd == soci::i_ok) {
            record.mRequestData = row.get<std::string>(12);
        }

        record.mCreateTime = row.get<int64_t>(13);
    }

    // 构建accounts的IN子句, 精准匹配, 使用参数绑定防注入
    static std::string BuildAccountsClause(const std::vector<std::string> &accounts) {
        if (accounts.empty()) {
            return "";
        }
        std::string clause = " AND user_account IN (";
        for (size_t i = 0; i < accounts.size(); ++i) {
            if (i > 0) {
                clause += ", ";
            }
            clause += ":acc" + std::to_string(i);
        }
        clause += ")";
        return clause;
    }

    // 构建actions的IN子句, 使用参数绑定防注入
    static std::string BuildActionsClause(const std::vector<std::string> &actions) {
        if (actions.empty()) {
            return "";
        }
        std::string clause = " AND action IN (";
        for (size_t i = 0; i < actions.size(); ++i) {
            if (i > 0) {
                clause += ", ";
            }
            clause += ":act";
            clause += std::to_string(i);
        }
        clause += ")";
        return clause;
    }

    static std::string BuildWhereClause(const AuditSearchFilter &filter) {
        std::string where = " WHERE 1=1";
        if (filter.mUserId != 0) {
            where += " AND user_id = :user_id";
        }
        where += BuildAccountsClause(filter.mUserAccounts);
        where += BuildActionsClause(filter.mActions);
        if (filter.mSuccessFilter >= 0) {
            where += " AND success = :success";
        }
        if (filter.mStartTime != 0) {
            where += " AND create_time >= :start_time";
        }
        if (filter.mEndTime != 0) {
            where += " AND create_time <= :end_time";
        }
        return where;
    }

    struct SearchBoundValues {
        uint64_t mUserId = 0;
        std::vector<std::string> mAccounts;
        std::vector<std::string> mActions;
        int mSuccess = 0;
        int64_t mStartTime = 0;
        int64_t mEndTime = 0;
        int32_t mPageSize = 20;
        int mOffset = 0;
    };

    static void PrepareBoundValues(const AuditSearchFilter &filter, SearchBoundValues &vals) {
        if (filter.mUserId != 0) {
            vals.mUserId = filter.mUserId;
        }
        // 精准匹配: 直接绑定原值, 参数绑定已防注入, 无需LIKE转义
        vals.mAccounts = filter.mUserAccounts;
        vals.mActions = filter.mActions;
        if (filter.mSuccessFilter >= 0) {
            vals.mSuccess = filter.mSuccessFilter;
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

    static void BindFilterParams(soci::statement &stmt, const AuditSearchFilter &filter,
                                 const SearchBoundValues &vals) {
        if (filter.mUserId != 0) {
            stmt.exchange(soci::use(vals.mUserId, "user_id"));
        }
        for (size_t i = 0; i < vals.mAccounts.size(); ++i) {
            stmt.exchange(soci::use(vals.mAccounts[i], "acc" + std::to_string(i)));
        }
        for (size_t i = 0; i < vals.mActions.size(); ++i) {
            stmt.exchange(soci::use(vals.mActions[i], "act" + std::to_string(i)));
        }
        if (filter.mSuccessFilter >= 0) {
            stmt.exchange(soci::use(vals.mSuccess, "success"));
        }
        if (filter.mStartTime != 0) {
            stmt.exchange(soci::use(vals.mStartTime, "start_time"));
        }
        if (filter.mEndTime != 0) {
            stmt.exchange(soci::use(vals.mEndTime, "end_time"));
        }
    }

    static void FetchAuditRecords(soci::statement &stmt, std::vector<models::AuditRecord> &records) {
        soci::row row;
        stmt.exchange(soci::into(row));
        stmt.define_and_bind();
        if (!stmt.execute(true)) {
            return;
        }

        while (true) {
            models::AuditRecord record;
            MapRowToAuditRecord(row, record);
            records.push_back(std::move(record));
            if (!stmt.fetch()) {
                break;
            }
        }
    }

    bool AuditDao::Insert(const models::AuditRecord &record) {
        SLOG_DEBUG << "AuditDao::Insert - userId: " << record.mUserId << ", action: " << record.mAction
                   << ", apiPath: " << record.mApiPath;
        try {
            soci::session session = GetSession();
            int successVal = record.mSuccess ? 1 : 0;

            session << "INSERT INTO audit_log (user_id, user_account, user_name, user_group_id, action, action_desc, "
                       "api_path, http_method, success, error_message, client_ip, request_data, create_time) "
                       "VALUES (:user_id, :user_account, :user_name, :user_group_id, :action, :action_desc, "
                       ":api_path, :http_method, :success, :error_message, :client_ip, :request_data, :create_time)",
                soci::use(record.mUserId, "user_id"), soci::use(record.mUserAccount, "user_account"),
                soci::use(record.mUserName, "user_name"), soci::use(record.mUserGroupId, "user_group_id"),
                soci::use(record.mAction, "action"), soci::use(record.mActionDesc, "action_desc"),
                soci::use(record.mApiPath, "api_path"), soci::use(record.mHttpMethod, "http_method"),
                soci::use(successVal, "success"), soci::use(record.mErrorMessage, "error_message"),
                soci::use(record.mClientIp, "client_ip"), soci::use(record.mRequestData, "request_data"),
                soci::use(record.mCreateTime, "create_time");

            SLOG_INFO << "Audit record inserted - userId: " << record.mUserId << ", action: " << record.mAction;
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AuditDao::Insert failed: " << e.what();
            return false;
        }
    }

    models::AuditRecord AuditDao::GetById(uint64_t id) {
        SLOG_DEBUG << "AuditDao::GetById - id: " << id;
        models::AuditRecord record;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs = (session.prepare << AuditSelectSQL << " WHERE id = :id", soci::use(id, "id"));

            for (const soci::row &row : rs) {
                MapRowToAuditRecord(row, record);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "AuditDao::GetById failed: " << e.what();
        }
        return record;
    }

    AuditSearchResult AuditDao::Search(const AuditSearchFilter &filter) {
        SLOG_DEBUG << "AuditDao::Search - userId: " << filter.mUserId << ", accounts: " << filter.mUserAccounts.size()
                   << ", actions: " << filter.mActions.size();
        AuditSearchResult result;
        try {
            soci::session session = GetSession();
            std::string whereClause = BuildWhereClause(filter);
            SearchBoundValues vals;
            PrepareBoundValues(filter, vals);

            std::string countSql = "SELECT COUNT(*) FROM audit_log" + whereClause;
            soci::statement countStmt = session.prepare << countSql;
            BindFilterParams(countStmt, filter, vals);
            countStmt.exchange(soci::into(result.mTotal));
            countStmt.define_and_bind();
            countStmt.execute(true);

            std::string querySql =
                std::string(AuditSelectSQL) + whereClause + " ORDER BY create_time DESC LIMIT :limit OFFSET :offset";

            soci::statement stmt = session.prepare << querySql;
            BindFilterParams(stmt, filter, vals);
            stmt.exchange(soci::use(vals.mPageSize, "limit"));
            stmt.exchange(soci::use(vals.mOffset, "offset"));
            FetchAuditRecords(stmt, result.mRecords);
        } catch (const std::exception &e) {
            SLOG_ERROR << "AuditDao::Search failed: " << e.what();
        }
        return result;
    }

    bool AuditDao::DeleteByTime(int64_t beforeTime) {
        SLOG_DEBUG << "AuditDao::DeleteByTime - beforeTime: " << beforeTime;
        try {
            soci::session session = GetSession();
            soci::statement stmt = (session.prepare << "DELETE FROM audit_log WHERE create_time < :before_time",
                                    soci::use(beforeTime, "before_time"));
            stmt.execute(true);

            int deleted = static_cast<int>(stmt.get_affected_rows());
            SLOG_INFO << "AuditDao::DeleteByTime - deleted " << deleted << " records";
            return deleted > 0;
        } catch (const std::exception &e) {
            SLOG_ERROR << "AuditDao::DeleteByTime failed: " << e.what();
            return false;
        }
    }

    int64_t AuditDao::CountByUserId(uint64_t userId) {
        SLOG_DEBUG << "AuditDao::CountByUserId - userId: " << userId;
        int64_t count = 0;
        try {
            soci::session session = GetSession();
            session << "SELECT COUNT(*) FROM audit_log WHERE user_id = :user_id", soci::use(userId, "user_id"),
                soci::into(count);
        } catch (const std::exception &e) {
            SLOG_ERROR << "AuditDao::CountByUserId failed: " << e.what();
        }
        return count;
    }

}  // namespace qifeng_ca
