//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_USER_DAO_MANAGER_H
#define QIFENG_CA_INCLUDE_DAO_USER_DAO_MANAGER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "qifeng_framework/common/cache/lru_cache.h"

#include "common/status.h"
#include "dao/admin_dynamic_code_dao.h"
#include "dao/models/bms_admin_dynamic_code.h"
#include "dao/models/bms_user.h"
#include "dao/user_dao.h"

namespace qifeng_ca {

    class UserDaoManager {
    public:
        using UserCacheType = common::cache::LRUCache<uint64_t, models::User>;

        struct DynamicCodeCache {
            std::string mSignKey;
            std::string mDynamic;
            std::string mEncryptionCode;
            int64_t mExpireTime {0};
            bool mIsUsed {false};
            std::mutex mMutex;
        };

        static UserDaoManager &GetInstance();

        ~UserDaoManager() = default;

        UserDaoManager(const UserDaoManager &) = delete;
        UserDaoManager &operator=(const UserDaoManager &) = delete;
        UserDaoManager(UserDaoManager &&) = delete;
        UserDaoManager &operator=(UserDaoManager &&) = delete;

        // ---User
        models::User GetByAccount(const std::string &account);

        models::User GetByAccountId(uint64_t accountId);

        models::User GetByAccountIdAndAccessToken(uint64_t accountId, const std::string &accessToken);

        models::User GetByAccountIdAndRefreshToken(uint64_t accountId, const std::string &refreshToken);

        models::User GetByFingerprintId(uint64_t fingerprintId);

        // 查询已录入指纹的用户数量 (fingerprint_id != 0), 不走缓存
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

        // ---AdminDynamicCode
        std::string GenDynamicCode();

        // 返回Status是个例外，后续可考虑用内部错误与外部错误码
        Status VerifyAndUseDynamicCode(const std::string &encryptionCode, const std::string &dynamic);

        int32_t CleanExpiredDynamicCode(int64_t beforeTimeMs);

    private:
        UserDaoManager();

        void InvalidateUser(uint64_t accountId);

        // ---AdminDynamicCode
        models::AdminDynamicCode BuildDynamicCodeRecord();

        bool InsertDynamicCode(const models::AdminDynamicCode &record);

        models::AdminDynamicCode GetDynamicCodeByEncryptionCode(const std::string &encryptionCode);

        bool MarkDynamicCodeAsUsed(const std::string &encryptionCode);

    private:
        std::shared_ptr<UserDao> mDao;
        std::shared_ptr<UserCacheType> mCache;

        std::shared_ptr<AdminDynamicCodeDao> mDynamicCodeDao;
        DynamicCodeCache mDynamicCodeCache;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_USER_DAO_MANAGER_H
