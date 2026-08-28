//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_USER_DAO_H
#define QIFENG_CA_INCLUDE_DAO_USER_DAO_H

#include <cstdint>
#include <string>
#include <vector>

#include "dao/bms_base_dao.h"
#include "dao/models/bms_user.h"

namespace qifeng_ca {

    struct UserSearchFilter {
        std::string mKeyword;
        int32_t mCurrent = 1;
        int32_t mPageSize = 20;
    };

    struct UserSearchResult {
        int32_t mTotal = 0;
        std::vector<models::User> mRecords;
    };

    class UserDao : public BmsBaseDao {
    public:
        UserDao() = default;
        ~UserDao() override = default;

        UserDao(const UserDao &) = delete;
        UserDao &operator=(const UserDao &) = delete;
        UserDao(UserDao &&) = delete;
        UserDao &operator=(UserDao &&) = delete;

        // User表
        models::User GetByAccount(const std::string &account);

        models::User GetByAccountId(uint64_t accountId);

        // models::User GetByAccountAndPassword(const std::string &account, const std::string &password);
        // models::User GetByAccountIdAndPassword(uint64_t accountId, const std::string &password);

        models::User GetByAccountIdAndAccessToken(uint64_t accountId, const std::string &accessToken);

        models::User GetByAccountIdAndRefreshToken(uint64_t accountId, const std::string &refreshToken);

        models::User GetByFingerprintId(uint64_t fingerprintId);

        // 查询已录入指纹的用户数量 (fingerprint_id != 0)
        int64_t GetFingerprintUserCount();

        // 获取第一个被创建的管理员(group_id=1)账户ID
        uint64_t GetFirstAdminAccountId();

        // 获取第一个被创建的访客(group_id=3)账户ID
        uint64_t GetFirstGuestAccountId();

        bool Insert(const models::User &user);

        bool Update(const models::User &user);

        bool UpdatePassword(uint64_t accountId, const std::string &newPassword);

        bool ClearTokens(uint64_t accountId);

        bool UpdateTokens(uint64_t accountId, const std::string &accessToken, const std::string &refreshToken);

        bool UpdateFingerprintId(uint64_t accountId, uint64_t fingerprintId);

        bool DeleteByAccountIds(const std::vector<uint64_t> &accountIds);

        UserSearchResult Search(const UserSearchFilter &filter);

        int32_t CountFingerprintEnrolled();

    private:
        // Group 表
        models::UserGroup GetGroupById(uint64_t groupId);

        models::UserGroup GetGroupByName(const std::string &name);

        bool InsertGroup(const models::UserGroup &group);

        bool UpdateGroup(const models::UserGroup &group);

        bool DeleteGroup(uint64_t groupId);

        std::vector<models::UserGroup> GetAllGroups();
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_USER_DAO_H
