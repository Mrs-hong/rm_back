//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <string_view>

#include "qifeng_framework/common/logger.h"

#include "dao/models/bms_user.h"
#include "dao/user_dao.h"

namespace qifeng_ca {

    constexpr const std::string_view UserColumns =
        "id, account_id, account, password, user_name, "
        "email, phone, fingerprint_id, group_id, comment, create_time, access_token, refresh_token";

    constexpr const std::string_view UserSelectSQL =
        "SELECT id, account_id, account, password, user_name, "
        "email, phone, fingerprint_id, group_id, comment, create_time, access_token, refresh_token "
        "FROM users";

    struct UserColumnIndicators {
        soci::indicator emailInd = soci::i_null;
        soci::indicator phoneInd = soci::i_null;
        soci::indicator fingerprintIdInd = soci::i_null;
        soci::indicator commentInd = soci::i_null;
        soci::indicator accessTokenInd = soci::i_null;
        soci::indicator refreshTokenInd = soci::i_null;
    };

    static void BindUserColumns(soci::statement &stmt, models::User &user, UserColumnIndicators &inds) {
        stmt.exchange(soci::into(user.mId));
        stmt.exchange(soci::into(user.mAccountId));
        stmt.exchange(soci::into(user.mAccount));
        stmt.exchange(soci::into(user.mPassword));
        stmt.exchange(soci::into(user.mUserName));
        stmt.exchange(soci::into(user.mEmail, inds.emailInd));
        stmt.exchange(soci::into(user.mPhone, inds.phoneInd));
        stmt.exchange(soci::into(user.mFingerprintId, inds.fingerprintIdInd));
        stmt.exchange(soci::into(user.mGroupId));
        stmt.exchange(soci::into(user.mComment, inds.commentInd));
        stmt.exchange(soci::into(user.mCreateTime));
        stmt.exchange(soci::into(user.mAccessToken, inds.accessTokenInd));
        stmt.exchange(soci::into(user.mRefreshToken, inds.refreshTokenInd));
    }

    static void ApplyNullIndicators(const UserColumnIndicators &inds, models::User &user) {
        if (inds.emailInd == soci::i_null) {
            user.mEmail.clear();
        }
        if (inds.phoneInd == soci::i_null) {
            user.mPhone.clear();
        }
        if (inds.fingerprintIdInd == soci::i_null) {
            user.mFingerprintId = 0;
        }
        if (inds.commentInd == soci::i_null) {
            user.mComment.clear();
        }
        if (inds.accessTokenInd == soci::i_null) {
            user.mAccessToken.clear();
        }
        if (inds.refreshTokenInd == soci::i_null) {
            user.mRefreshToken.clear();
        }
    }

    static bool ExecuteUserQuery(soci::statement &stmt, models::User &user, UserColumnIndicators &inds) {
        stmt.define_and_bind();
        bool found = stmt.execute(true);
        if (!found) {
            return false;
        }
        ApplyNullIndicators(inds, user);
        return true;
    }

    static void MapRowToUser(const soci::row &row, models::User &user) {
        user.mId = row.get<uint64_t>(0);
        user.mAccountId = row.get<uint64_t>(1);
        user.mAccount = row.get<std::string>(2);
        (void)row.get<std::string>(3);  // 跳过passwd
        user.mUserName = row.get<std::string>(4);

        soci::indicator emailInd = row.get_indicator(5);
        if (emailInd == soci::i_ok) {
            user.mEmail = row.get<std::string>(5);
        }
        soci::indicator phoneInd = row.get_indicator(6);
        if (phoneInd == soci::i_ok) {
            user.mPhone = row.get<std::string>(6);
        }
        soci::indicator fingerprintIdInd = row.get_indicator(7);
        if (fingerprintIdInd == soci::i_ok) {
            user.mFingerprintId = row.get<uint64_t>(7);
        }
        user.mGroupId = row.get<uint64_t>(8);

        soci::indicator commentInd = row.get_indicator(9);
        if (commentInd == soci::i_ok) {
            user.mComment = row.get<std::string>(9);
        }
        user.mCreateTime = row.get<int64_t>(10);

        soci::indicator accessTokenInd = row.get_indicator(11);
        if (accessTokenInd == soci::i_ok) {
            user.mAccessToken = row.get<std::string>(11);
        }
        soci::indicator refreshTokenInd = row.get_indicator(12);
        if (refreshTokenInd == soci::i_ok) {
            user.mRefreshToken = row.get<std::string>(12);
        }
    }

    static void MapRowToUserGroup(const soci::row &row, models::UserGroup &group) {
        group.mId = row.get<uint64_t>(0);
        group.mName = row.get<std::string>(1);

        soci::indicator descInd = row.get_indicator(2);
        if (descInd == soci::i_ok) {
            group.mDescription = row.get<std::string>(2);
        }
        group.mIsSystem = row.get<int>(3) != 0;

        soci::indicator caInd = row.get_indicator(4);
        if (caInd == soci::i_ok) {
            group.mCreatedAt = row.get<std::string>(4);
        }
        soci::indicator uaInd = row.get_indicator(5);
        if (uaInd == soci::i_ok) {
            group.mUpdatedAt = row.get<std::string>(5);
        }
    }

    // ==================== LoadUser helpers ====================

    static models::User LoadUser(soci::statement &stmt) {
        models::User user;
        UserColumnIndicators inds;
        BindUserColumns(stmt, user, inds);
        if (!ExecuteUserQuery(stmt, user, inds)) {
            return user;
        }
        return user;
    }

    // ==================== user 表操作 ====================

    models::User UserDao::GetByAccount(const std::string &account) {
        SLOG_DEBUG << "GetByAccount: " << account;
        models::User user;
        try {
            soci::session session = GetSession();
            soci::statement stmt =
                (session.prepare << UserSelectSQL << " WHERE account = :account", soci::use(account, "account"));
            user = LoadUser(stmt);
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetByAccount failed: " << e.what();
        }
        return user;
    }

    models::User UserDao::GetByAccountId(uint64_t accountId) {
        SLOG_DEBUG << "GetByAccountId: " << accountId;
        models::User user;
        try {
            soci::session session = GetSession();
            soci::statement stmt = (session.prepare << UserSelectSQL << " WHERE account_id = :account_id",
                                    soci::use(accountId, "account_id"));
            user = LoadUser(stmt);
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetByAccountId failed: " << e.what();
        }
        return user;
    }

    models::User UserDao::GetByAccountIdAndAccessToken(uint64_t accountId, const std::string &accessToken) {
        SLOG_DEBUG << "GetByAccountIdAndAccessToken: " << accountId;
        models::User user;
        try {
            soci::session session = GetSession();
            soci::statement stmt =
                (session.prepare << UserSelectSQL << " WHERE account_id = :account_id AND access_token = :access_token",
                 soci::use(accountId, "account_id"), soci::use(accessToken, "access_token"));
            user = LoadUser(stmt);
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetByAccountIdAndAccessToken failed: " << e.what();
        }
        return user;
    }

    models::User UserDao::GetByAccountIdAndRefreshToken(uint64_t accountId, const std::string &refreshToken) {
        SLOG_DEBUG << "GetByAccountIdAndRefreshToken: " << accountId;
        models::User user;
        try {
            soci::session session = GetSession();
            soci::statement stmt =
                (session.prepare << UserSelectSQL
                                 << " WHERE account_id = :account_id AND refresh_token = :refresh_token",
                 soci::use(accountId, "account_id"), soci::use(refreshToken, "refresh_token"));
            user = LoadUser(stmt);
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetByAccountIdAndRefreshToken failed: " << e.what();
        }
        return user;
    }

    models::User UserDao::GetByFingerprintId(uint64_t fingerprintId) {
        SLOG_DEBUG << "GetByFingerprintId: " << fingerprintId;
        models::User user;
        if (fingerprintId == 0) {
            return user;
        }
        try {
            soci::session session = GetSession();
            soci::statement stmt = (session.prepare << UserSelectSQL << " WHERE fingerprint_id = :fingerprint_id",
                                    soci::use(fingerprintId, "fingerprint_id"));
            user = LoadUser(stmt);
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetByFingerprintId failed: " << e.what();
        }
        return user;
    }

    int64_t UserDao::GetFingerprintUserCount() {
        SLOG_DEBUG << "GetFingerprintUserCount";
        int64_t count = 0;
        try {
            soci::session session = GetSession();
            soci::statement stmt =
                (session.prepare << "SELECT COUNT(*) FROM users WHERE fingerprint_id != 0", soci::into(count));
            stmt.execute(true);
            if (!stmt.got_data()) {
                return 0;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetFingerprintUserCount failed: " << e.what();
            return 0;
        }
        return count;
    }

    bool UserDao::Insert(const models::User &user) {
        SLOG_DEBUG << "Insert: " << user.mAccount;
        try {
            soci::session session = GetSession();
            session
                << "INSERT INTO users (account_id, account, password, user_name, "
                   "email, phone, fingerprint_id, group_id, comment, create_time, access_token, refresh_token) "
                   "VALUES (:account_id, :account, :password, :user_name, "
                   ":email, :phone, :fingerprint_id, :group_id, :comment, :create_time, :access_token, :refresh_token)",
                soci::use(user.mAccountId, "account_id"), soci::use(user.mAccount, "account"),
                soci::use(user.mPassword, "password"), soci::use(user.mUserName, "user_name"),
                soci::use(user.mEmail, "email"), soci::use(user.mPhone, "phone"),
                soci::use(user.mFingerprintId, "fingerprint_id"), soci::use(user.mGroupId, "group_id"),
                soci::use(user.mComment, "comment"), soci::use(user.mCreateTime, "create_time"),
                soci::use(user.mAccessToken, "access_token"), soci::use(user.mRefreshToken, "refresh_token");

            SLOG_INFO << "User inserted: " << user.mAccount;
        } catch (const std::exception &e) {
            SLOG_ERROR << "Insert failed: " << e.what();
            return false;
        }
        return true;
    }

    bool UserDao::Update(const models::User &user) {
        SLOG_DEBUG << "Update: " << user.mAccountId;
        try {
            soci::session session = GetSession();
            soci::statement stmt = (session.prepare << "UPDATE users SET user_name = :user_name, phone = :phone, "
                                                       "email = :email, group_id = :group_id, comment = :comment "
                                                       "WHERE account_id = :account_id",
                                    soci::use(user.mUserName, "user_name"), soci::use(user.mPhone, "phone"),
                                    soci::use(user.mEmail, "email"), soci::use(user.mGroupId, "group_id"),
                                    soci::use(user.mComment, "comment"), soci::use(user.mAccountId, "account_id"));
            stmt.execute(true);
            if (static_cast<int>(stmt.get_affected_rows()) > 0) {
                SLOG_INFO << "User updated: " << user.mAccountId;
                return true;
            }
            SLOG_ERROR << "User not found for update: " << user.mAccountId;
            return false;
        } catch (const std::exception &e) {
            SLOG_ERROR << "Update failed: " << e.what();
            return false;
        }
    }

    bool UserDao::UpdatePassword(uint64_t accountId, const std::string &newPassword) {
        SLOG_DEBUG << "UpdatePassword: " << accountId;
        try {
            soci::session session = GetSession();
            soci::statement stmt =
                (session.prepare << "UPDATE users SET password = :password WHERE account_id = :account_id",
                 soci::use(newPassword, "password"), soci::use(accountId, "account_id"));
            stmt.execute(true);
            if (static_cast<int>(stmt.get_affected_rows()) > 0) {
                SLOG_INFO << "Password updated for: " << accountId;
                return true;
            }
            SLOG_ERROR << "User not found for password update: " << accountId;
            return false;
        } catch (const std::exception &e) {
            SLOG_ERROR << "UpdatePassword failed: " << e.what();
            return false;
        }
    }

    bool UserDao::ClearTokens(uint64_t accountId) {
        SLOG_DEBUG << "ClearTokens: " << accountId;
        try {
            soci::session session = GetSession();
            soci::statement stmt = (session.prepare << "UPDATE users SET access_token = '', refresh_token = '' "
                                                       "WHERE account_id = :account_id",
                                    soci::use(accountId, "account_id"));
            stmt.execute(true);
            return static_cast<int>(stmt.get_affected_rows()) > 0;
        } catch (const std::exception &e) {
            SLOG_ERROR << "ClearTokens failed: " << e.what();
            return false;
        }
    }

    bool UserDao::UpdateTokens(uint64_t accountId, const std::string &accessToken, const std::string &refreshToken) {
        SLOG_DEBUG << "UpdateTokens: " << accountId;
        try {
            soci::session session = GetSession();
            soci::statement stmt = (session.prepare << "UPDATE users SET access_token = :access_token, "
                                                       "refresh_token = :refresh_token WHERE account_id = :account_id",
                                    soci::use(accessToken, "access_token"), soci::use(refreshToken, "refresh_token"),
                                    soci::use(accountId, "account_id"));
            stmt.execute(true);
            if (static_cast<int>(stmt.get_affected_rows()) > 0) {
                SLOG_INFO << "Tokens updated for: " << accountId;
                return true;
            }
            SLOG_ERROR << "User not found for token update: " << accountId;
            return false;
        } catch (const std::exception &e) {
            SLOG_ERROR << "UpdateTokens failed: " << e.what();
            return false;
        }
    }

    bool UserDao::UpdateFingerprintId(uint64_t accountId, uint64_t fingerprintId) {
        SLOG_DEBUG << "UpdateFingerprintId: " << accountId << ", fingerprintId: " << fingerprintId;
        try {
            soci::session session = GetSession();
            soci::statement stmt =
                (session.prepare << "UPDATE users SET fingerprint_id = :fingerprint_id WHERE account_id = :account_id",
                 soci::use(fingerprintId, "fingerprint_id"), soci::use(accountId, "account_id"));
            stmt.execute(true);
            if (static_cast<int>(stmt.get_affected_rows()) > 0) {
                SLOG_INFO << "FingerprintId updated for: " << accountId;
                return true;
            }
            SLOG_ERROR << "User not found for fingerprint update: " << accountId;
            return false;
        } catch (const std::exception &e) {
            SLOG_ERROR << "UpdateFingerprintId failed: " << e.what();
            return false;
        }
    }

    bool UserDao::DeleteByAccountIds(const std::vector<uint64_t> &accountIds) {
        SLOG_DEBUG << "DeleteByAccountIds, count: " << accountIds.size();
        if (accountIds.empty()) {
            return false;
        }
        try {
            soci::session session = GetSession();
            int totalDeleted = 0;
            for (const auto &accountId : accountIds) {
                soci::statement stmt = (session.prepare << "DELETE FROM users WHERE account_id = :account_id",
                                        soci::use(accountId, "account_id"));
                stmt.execute(true);
                totalDeleted += static_cast<int>(stmt.get_affected_rows());
            }
            SLOG_INFO << "Deleted " << totalDeleted << " users";
            return totalDeleted > 0;
        } catch (const std::exception &e) {
            SLOG_ERROR << "DeleteByAccountIds failed: " << e.what();
            return false;
        }
    }

    uint64_t UserDao::GetFirstAdminAccountId() {
        SLOG_DEBUG << "GetFirstAdminAccountId";
        uint64_t accountId = 0;
        try {
            soci::session session = GetSession();
            // group_id=1 对应 Authority::ADMINISTRATOR (管理员组)
            soci::statement stmt = (session.prepare << "SELECT account_id FROM users "
                                                       "WHERE group_id = 1 ORDER BY create_time ASC LIMIT 1",
                                    soci::into(accountId));
            stmt.execute(true);
            if (!stmt.got_data()) {
                SLOG_WARN << "No admin user found (group_id=1)";
                return 0;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetFirstAdminAccountId failed: " << e.what();
            return 0;
        }
        return accountId;
    }

    uint64_t UserDao::GetFirstGuestAccountId() {
        SLOG_DEBUG << "GetFirstGuestAccountId";
        uint64_t accountId = 0;
        try {
            soci::session session = GetSession();
            // group_id=3 对应 Authority::GUEST (访客组)
            soci::statement stmt = (session.prepare << "SELECT account_id FROM users "
                                                       "WHERE group_id = 3 ORDER BY create_time ASC LIMIT 1",
                                    soci::into(accountId));
            stmt.execute(true);
            if (!stmt.got_data()) {
                SLOG_WARN << "No guest user found (group_id=3)";
                return 0;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetFirstGuestAccountId failed: " << e.what();
            return 0;
        }
        return accountId;
    }

    UserSearchResult UserDao::Search(const UserSearchFilter &filter) {
        SLOG_DEBUG << "Search, keyword: " << filter.mKeyword << ", current: " << filter.mCurrent
                   << ", page_size: " << filter.mPageSize;
        UserSearchResult result;
        try {
            soci::session session = GetSession();
            std::string likeKeyword = "%" + filter.mKeyword + "%";
            // 排除访客用户(group_id=3, GUEST)，访客不展示在用户列表中
            session << "SELECT COUNT(*) FROM users WHERE (account LIKE :kw OR user_name LIKE :kw "
                       "OR phone LIKE :kw OR email LIKE :kw OR comment LIKE :kw) AND group_id != 3",
                soci::use(likeKeyword, "kw"), soci::into(result.mTotal);

            int offset = (filter.mCurrent - 1) * filter.mPageSize;
            soci::rowset<soci::row> rs =
                (session.prepare << "SELECT " << UserColumns
                                 << " FROM users WHERE (account LIKE :kw OR user_name LIKE :kw "
                                    "OR phone LIKE :kw OR email LIKE :kw OR comment LIKE :kw) "
                                    "AND group_id != 3 "
                                    "ORDER BY create_time DESC LIMIT :limit OFFSET :offset",
                 soci::use(likeKeyword, "kw"), soci::use(filter.mPageSize, "limit"), soci::use(offset, "offset"));

            for (const soci::row &row : rs) {
                models::User user;
                MapRowToUser(row, user);
                result.mRecords.push_back(std::move(user));
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "Search failed: " << e.what();
        }
        return result;
    }

    int32_t UserDao::CountFingerprintEnrolled() {
        SLOG_DEBUG << "CountFingerprintEnrolled";
        int32_t count = 0;
        try {
            soci::session session = GetSession();
            session << "SELECT COUNT(*) FROM users WHERE fingerprint_id != 0", soci::into(count);
        } catch (const std::exception &e) {
            SLOG_ERROR << "CountFingerprintEnrolled failed: " << e.what();
            return -1;
        }
        return count;
    }

    // ==================== user_group ====================

    models::UserGroup UserDao::GetGroupById(uint64_t groupId) {
        SLOG_DEBUG << "GetGroupById: " << groupId;
        models::UserGroup group;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << "SELECT id, name, description, is_system, created_at, updated_at "
                                    "FROM user_group WHERE id = :id",
                 soci::use(groupId, "id"));
            for (const soci::row &row : rs) {
                MapRowToUserGroup(row, group);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetGroupById failed: " << e.what();
        }
        return group;
    }

    models::UserGroup UserDao::GetGroupByName(const std::string &name) {
        SLOG_DEBUG << "GetGroupByName: " << name;
        models::UserGroup group;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << "SELECT id, name, description, is_system, created_at, updated_at "
                                    "FROM user_group WHERE name = :name",
                 soci::use(name, "name"));
            for (const soci::row &row : rs) {
                MapRowToUserGroup(row, group);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetGroupByName failed: " << e.what();
        }
        return group;
    }

    bool UserDao::InsertGroup(const models::UserGroup &group) {
        SLOG_DEBUG << "InsertGroup: " << group.mName;
        try {
            int sysVal = group.mIsSystem ? 1 : 0;
            soci::session session = GetSession();
            session << "INSERT INTO user_group (name, description, is_system) "
                       "VALUES (:name, :description, :is_system)",
                soci::use(group.mName, "name"), soci::use(group.mDescription, "description"),
                soci::use(sysVal, "is_system");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "InsertGroup failed: " << e.what();
            return false;
        }
    }

    bool UserDao::UpdateGroup(const models::UserGroup &group) {
        SLOG_DEBUG << "UpdateGroup: " << group.mId;
        try {
            soci::session session = GetSession();
            soci::statement stmt =
                (session.prepare << "UPDATE user_group SET name = :name, description = :description WHERE id = :id",
                 soci::use(group.mName, "name"), soci::use(group.mDescription, "description"),
                 soci::use(group.mId, "id"));
            stmt.execute(true);
            return stmt.get_affected_rows() > 0;
        } catch (const std::exception &e) {
            SLOG_ERROR << "UpdateGroup failed: " << e.what();
            return false;
        }
    }

    bool UserDao::DeleteGroup(uint64_t groupId) {
        SLOG_DEBUG << "DeleteGroup: " << groupId;
        try {
            soci::session session = GetSession();
            soci::statement stmt = (session.prepare << "DELETE FROM user_group WHERE id = :id AND is_system = 0",
                                    soci::use(groupId, "id"));
            stmt.execute(true);
            return stmt.get_affected_rows() > 0;
        } catch (const std::exception &e) {
            SLOG_ERROR << "DeleteGroup failed: " << e.what();
            return false;
        }
    }

    std::vector<models::UserGroup> UserDao::GetAllGroups() {
        SLOG_DEBUG << "GetAllGroups";
        std::vector<models::UserGroup> result;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << "SELECT id, name, description, is_system, created_at, updated_at "
                                    "FROM user_group ORDER BY id");
            for (const soci::row &row : rs) {
                models::UserGroup group;
                MapRowToUserGroup(row, group);
                result.push_back(std::move(group));
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetAllGroups failed: " << e.what();
        }
        return result;
    }

}  // namespace qifeng_ca
