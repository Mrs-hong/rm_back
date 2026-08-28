/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/security/jwt_utils.h"

#include "common/common.h"
#include "common/config/auth_config.h"
#include "common/http/http.h"
#include "controller/filter/jwt_filter.h"
#include "dao/models/bms_user.h"
#include "dao_managers/user_dao_manager.h"

CAJwtFilter::CAJwtFilter() {
    FLOG_INFO("CAJwtFilter Init");
}

bool CAJwtFilter::ShouldSkipJwtCheck(const std::string &path) const {
    (void)path;
    return false;
}

bool CAJwtFilter::VerifyJwtToken(const std::string &token, std::map<std::string, std::string> &claims) const {
    std::string secretKey = qifeng_ca::AuthConfig::GetInstance().GetJwtSecret();
    bool ret = common::security::JwtUtils::VerifyToken(token, secretKey, claims);
    if (!ret) {  // 使用refresh token验证
        secretKey = qifeng_ca::AuthConfig::GetInstance().GetJwtRefreshSecret();
        ret = common::security::JwtUtils::VerifyToken(token, secretKey, claims);
    }
    return ret;
}

bool CAJwtFilter::GetKey(const drogon::HttpRequestPtr &req, const std::string &token, std::string &key) {
    (void)req;
    (void)token;
    key = qifeng_ca::AuthConfig::GetInstance().GetAdminKey();
    return true;
}

bool CAJwtFilter::ValidateUser(uint64_t userId, const drogon::HttpRequestPtr &req) const {
    std::string token = ExtractJwtToken(req);
    if (token.empty()) {
        return false;
    }
    auto user = qifeng_ca::UserDaoManager::GetInstance().GetByAccountId(userId);
    if (user.mGroupId != qifeng_ca::Authority::GUEST) {
        if (user.mAccountId == 0 || user.mAccessToken.empty() ||
            (token != user.mAccessToken && token != user.mRefreshToken)) {
            SLOG_WARN << "JWT ValidateUser failed: user " << userId << " not found";
            return false;
        }
    }

    req->attributes()->insert("accountId", userId);
    req->attributes()->insert("account", user.mAccount);

    return true;
}

QIFENG_CA_HTTP_FILTER_REGISTRY(CAJwtFilter);
