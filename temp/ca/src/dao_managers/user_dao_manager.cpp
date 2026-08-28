//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//
#include <atomic>
#include <iostream>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/security/jwt_utils.h"

#include "common/common.h"
#include "common/config/auth_config.h"
#include "dao_managers/user_dao_manager.h"

namespace qifeng_ca {

    UserDaoManager &UserDaoManager::GetInstance() {
        static UserDaoManager Instance;
        return Instance;
    }

    UserDaoManager::UserDaoManager()
        : mDao(std::make_shared<UserDao>()), mCache(std::make_shared<UserCacheType>(1024)),
          mDynamicCodeDao(std::make_shared<AdminDynamicCodeDao>()) {
        SLOG_INFO << "UserDaoManager initialized with cache size: 1024";
    }

    models::User UserDaoManager::GetByAccountId(uint64_t accountId) {
        models::User user;
        if (mCache->Get(accountId, user)) {
            SLOG_DEBUG << "Cache hit for accountId: " << accountId;
            return user;
        }
        user = mDao->GetByAccountId(accountId);
        if (user.mAccountId != 0) {
            mCache->Put(accountId, user);
        }
        return user;
    }

    bool UserDaoManager::Insert(const models::User &user) {
        bool ok = mDao->Insert(user);
        if (ok) {
            mCache->Put(user.mAccountId, user);
        }
        return ok;
    }

    bool UserDaoManager::Update(const models::User &user) {
        InvalidateUser(user.mAccountId);
        return mDao->Update(user);
    }

    bool UserDaoManager::UpdatePassword(uint64_t accountId, const std::string &newPassword) {
        InvalidateUser(accountId);
        return mDao->UpdatePassword(accountId, newPassword);
    }

    bool UserDaoManager::ClearTokens(uint64_t accountId) {
        InvalidateUser(accountId);
        return mDao->ClearTokens(accountId);
    }

    bool UserDaoManager::UpdateTokens(uint64_t accountId, const std::string &accessToken,
                                      const std::string &refreshToken) {
        InvalidateUser(accountId);
        return mDao->UpdateTokens(accountId, accessToken, refreshToken);
    }

    bool UserDaoManager::UpdateFingerprintId(uint64_t accountId, uint64_t fingerprintId) {
        InvalidateUser(accountId);
        return mDao->UpdateFingerprintId(accountId, fingerprintId);
    }

    bool UserDaoManager::DeleteByAccountIds(const std::vector<uint64_t> &accountIds) {
        for (auto id : accountIds) {
            InvalidateUser(id);
        }
        return mDao->DeleteByAccountIds(accountIds);
    }

    // 安全敏感查询不走缓存

    models::User UserDaoManager::GetByAccount(const std::string &account) {
        return mDao->GetByAccount(account);
    }

    models::User UserDaoManager::GetByAccountIdAndAccessToken(uint64_t accountId, const std::string &accessToken) {
        return mDao->GetByAccountIdAndAccessToken(accountId, accessToken);
    }
    models::User UserDaoManager::GetByAccountIdAndRefreshToken(uint64_t accountId, const std::string &refreshToken) {
        return mDao->GetByAccountIdAndRefreshToken(accountId, refreshToken);
    }
    models::User UserDaoManager::GetByFingerprintId(uint64_t fingerprintId) {
        return mDao->GetByFingerprintId(fingerprintId);
    }

    int64_t UserDaoManager::GetFingerprintUserCount() {
        return mDao->GetFingerprintUserCount();
    }

    uint64_t UserDaoManager::GetFirstAdminAccountId() {
        // 管理员账户为系统账户, 每分钟刷新一次
        static std::atomic<uint64_t> CachedAdminId {0};
        static std::atomic<uint64_t> LastRefreshMs {0};
        constexpr int64_t kRefreshIntervalMs = 60LL * 1000LL;  // 1分钟

        uint64_t now = GetTimeMs();
        uint64_t last = LastRefreshMs.load(std::memory_order_relaxed);
        if (CachedAdminId.load(std::memory_order_relaxed) != 0 && (now - last) < kRefreshIntervalMs) {
            return CachedAdminId.load(std::memory_order_relaxed);
        }

        uint64_t adminId = mDao->GetFirstAdminAccountId();
        if (adminId == 0) {
            SLOG_WARN << "Admin account not found in database";
            // 查询失败时返回旧值, 避免短暂故障导致返回0
            return CachedAdminId.load(std::memory_order_relaxed);
        }
        CachedAdminId.store(adminId, std::memory_order_relaxed);
        LastRefreshMs.store(now, std::memory_order_relaxed);
        return adminId;
    }

    uint64_t UserDaoManager::GetFirstGuestAccountId() {
        // 访客账户为系统账户, 每分钟刷新一次
        static std::atomic<uint64_t> CachedGuestId {0};
        static std::atomic<uint64_t> LastRefreshMs {0};
        constexpr int64_t kRefreshIntervalMs = 60LL * 1000LL;  // 1分钟

        uint64_t now = GetTimeMs();
        uint64_t last = LastRefreshMs.load(std::memory_order_relaxed);
        if (CachedGuestId.load(std::memory_order_relaxed) != 0 && (now - last) < kRefreshIntervalMs) {
            return CachedGuestId.load(std::memory_order_relaxed);
        }

        uint64_t guestId = mDao->GetFirstGuestAccountId();
        if (guestId == 0) {
            SLOG_WARN << "Guest account not found in database";
            // 查询失败时返回旧值, 避免短暂故障导致返回0
            return CachedGuestId.load(std::memory_order_relaxed);
        }
        CachedGuestId.store(guestId, std::memory_order_relaxed);
        LastRefreshMs.store(now, std::memory_order_relaxed);
        return guestId;
    }

    UserSearchResult UserDaoManager::Search(const UserSearchFilter &filter) {
        return mDao->Search(filter);
    }

    int32_t UserDaoManager::CountFingerprintEnrolled() {
        return mDao->CountFingerprintEnrolled();
    }

    void UserDaoManager::InvalidateUser(uint64_t accountId) {
        mCache->Remove(accountId);
    }

    // ---AdminDynamicCode

    models::AdminDynamicCode UserDaoManager::BuildDynamicCodeRecord() {
        auto &config = AuthConfig::GetInstance();
        uint64_t dynamicUint64 = GetUuid();
        std::string signKey = GenUUID();
        int32_t expirySeconds = config.GetVerifyCodeExpirySeconds();
        int64_t expireTime = static_cast<int64_t>(GetTimeMs()) + static_cast<int64_t>(expirySeconds) * 1000;

        std::string dynamic = std::to_string(dynamicUint64);
        std::string encryptionCode = common::security::JwtUtils::GenerateToken(dynamicUint64, signKey, expirySeconds);

        models::AdminDynamicCode record;
        record.mDynamic = dynamic;
        record.mEncryptionCode = encryptionCode;
        record.mExpireTime = expireTime;
        record.mIsUsed = false;
        record.mCreateTime = static_cast<int64_t>(GetTimeMs());

        {
            std::lock_guard<std::mutex> lock(mDynamicCodeCache.mMutex);
            mDynamicCodeCache.mSignKey = signKey;
            mDynamicCodeCache.mDynamic = dynamic;
            mDynamicCodeCache.mEncryptionCode = encryptionCode;
            mDynamicCodeCache.mExpireTime = expireTime;
            mDynamicCodeCache.mIsUsed = false;
        }

        return record;
    }

    bool UserDaoManager::InsertDynamicCode(const models::AdminDynamicCode &record) {
        bool ok = mDynamicCodeDao->Insert(record);
        if (!ok) {
            SLOG_ERROR << "InsertDynamicCode failed for dynamic: " << record.mDynamic;
        }
        return ok;
    }

    models::AdminDynamicCode UserDaoManager::GetDynamicCodeByEncryptionCode(const std::string &encryptionCode) {
        return mDynamicCodeDao->GetByEncryptionCode(encryptionCode);
    }

    bool UserDaoManager::MarkDynamicCodeAsUsed(const std::string &encryptionCode) {
        bool ok = mDynamicCodeDao->MarkAsUsedByEncryptionCode(encryptionCode);
        if (!ok) {
            SLOG_ERROR << "MarkDynamicCodeAsUsed failed for encryptionCode: " << encryptionCode;
        }
        return ok;
    }

    std::string UserDaoManager::GenDynamicCode() {
        {
            std::lock_guard<std::mutex> lock(mDynamicCodeCache.mMutex);
            uint64_t currentTime = GetTimeMs();
            if (!mDynamicCodeCache.mEncryptionCode.empty() && !mDynamicCodeCache.mIsUsed &&
                mDynamicCodeCache.mExpireTime > static_cast<int64_t>(currentTime)) {
                SLOG_INFO << "Existing dynamic code still valid, returning cached encryptionCode";
                return mDynamicCodeCache.mEncryptionCode;
            }
        }

        models::AdminDynamicCode record = BuildDynamicCodeRecord();

        if (!InsertDynamicCode(record)) {
            std::lock_guard<std::mutex> lock(mDynamicCodeCache.mMutex);
            mDynamicCodeCache.mSignKey.clear();
            mDynamicCodeCache.mDynamic.clear();
            mDynamicCodeCache.mEncryptionCode.clear();
            SLOG_ERROR << "GenDynamicCode: failed to insert dynamic code into DB";
            return {};
        }

        SLOG_INFO << "New dynamic code generated, expires in " << AuthConfig::GetInstance().GetVerifyCodeExpirySeconds()
                  << "s";
        return record.mEncryptionCode;
    }

    static bool VerifyJwtWithSignKey(const std::string &encryptionCode, const std::string &signKey,
                                     const std::string &dynamic) {
        std::map<std::string, std::string> outClaims;
        bool matched = common::security::JwtUtils::VerifyToken(encryptionCode, signKey, outClaims);
        if (!matched) {
            return false;
        }
        auto it = outClaims.find("sub");
        if (it == outClaims.end()) {
            return false;
        }
        if (it->second != dynamic) {
            return false;
        }
        return true;
    }

    Status UserDaoManager::VerifyAndUseDynamicCode(const std::string &encryptionCode, const std::string &dynamic) {
        std::string signKey;
        {
            std::lock_guard<std::mutex> lock(mDynamicCodeCache.mMutex);
            if (mDynamicCodeCache.mSignKey.empty()) {
                return Status {802040, "签名密钥不存在，请刷新重新获取动态码"};
            }
            signKey = mDynamicCodeCache.mSignKey;
        }

        if (!VerifyJwtWithSignKey(encryptionCode, signKey, dynamic)) {
            return Status {802031, "动态密码验证失败"};
        }

        models::AdminDynamicCode record = GetDynamicCodeByEncryptionCode(encryptionCode);
        if (record.mId == 0) {
            return Status {802033, "动态密码不存在"};
        }

        if (record.mIsUsed) {
            return Status {802034, "动态密码已使用"};
        }

        uint64_t currentTime = GetTimeMs();
        if (record.mExpireTime <= static_cast<int64_t>(currentTime)) {
            return Status {802035, "动态密码已过期"};
        }

        {
            std::lock_guard<std::mutex> lock(mDynamicCodeCache.mMutex);
            mDynamicCodeCache.mIsUsed = true;
        }

        if (!MarkDynamicCodeAsUsed(encryptionCode)) {
            std::lock_guard<std::mutex> lock(mDynamicCodeCache.mMutex);
            mDynamicCodeCache.mIsUsed = false;
            SLOG_WARN << "VerifyAndUseDynamicCode: DB update failed, rolling back memory state";
            return Status {802036, "动态码状态更新失败"};
        }

        SLOG_INFO << "Dynamic code verified and marked as used";
        return Status {};
    }

    int32_t UserDaoManager::CleanExpiredDynamicCode(int64_t beforeTimeMs) {
        return mDynamicCodeDao->CleanExpired(beforeTimeMs);
    }

}  // namespace qifeng_ca
