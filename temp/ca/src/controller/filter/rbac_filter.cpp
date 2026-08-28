/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "qifeng_framework/common/logger.h"

#include "common/http/http.h"
#include "controller/filter/rbac_filter.h"
#include "dao_managers/casbin_dao_manager.h"
#include "dao_managers/user_dao_manager.h"

CARbacFilter::CARbacFilter() {
    FLOG_INFO("CARbacFilter Init");
}

void CARbacFilter::doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&filterCallback,
                            drogon::FilterChainCallback &&filterChainCallback) {
    auto attrs = req->attributes();
    std::string account;

    try {
        account = attrs->get<std::string>("account");
    } catch (...) {
        SLOG_WARN << "[CARbacFilter] 'account' attribute not found, JWT filter may not have run";
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k401Unauthorized);
        resp->setBody("{\"code\":401,\"message\":\"Unauthorized: missing account\"}");
        filterCallback(resp);
        return;
    }

    auto user = qifeng_ca::UserDaoManager::GetInstance().GetByAccount(account);
    if (user.mAccountId == 0) {
        SLOG_WARN << "[CARbacFilter] user not found: " << account;
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k403Forbidden);
        resp->setBody("{\"code\":403,\"message\":\"Forbidden: user not found\"}");
        filterCallback(resp);
        return;
    }

    const std::string &path = req->path();
    std::string subject = "group:" + std::to_string(user.mGroupId);

    bool allowed = qifeng_ca::CasbinDaoManager::GetInstance().Enforce(subject, path, "write");
    if (!allowed) {
        SLOG_WARN << "[CARbacFilter] DENY: " << subject << " -> " << path;
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k403Forbidden);
        resp->setBody("{\"code\":403,\"message\":\"Forbidden: permission denied\"}");
        filterCallback(resp);
        return;
    }

    SLOG_DEBUG << "[CARbacFilter] ALLOW: " << subject << " -> " << path;
    filterChainCallback();
}
QIFENG_CA_HTTP_FILTER_REGISTRY(CARbacFilter);
