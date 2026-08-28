/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/config_define.h"
#include "common/config_manager.h"
#include "common/logger.h"
#include "common/security/jwt_utils.h"
#include "middleware/jwt_middleware.h"

JwtFilter::JwtFilter() {
    // 初始化UserService对象
    FLOG_TRACE("JwtFilter construct");
}

void JwtFilter::doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& filterCallback,
                         drogon::FilterChainCallback&& filterChainCallback) {
    FLOG_TRACE("JwtFilter doFilter");
    const std::string& path = req->path();
    if (ShouldSkipJwtCheck(path)) {
        filterChainCallback();
        return;
    }

    std::string token = ExtractJwtToken(req);
    if (token.empty()) {
        filterCallback(MakeUnauthorizedResponse("Unauthorized: Missing Authorization header"));
        return;
    }

    std::string adminKey;
    if (!GetKey(req, token, adminKey)) {
        SLOG_INFO << "Admin accessed " << path << " from " << req->peerAddr().toIpPort();
        filterChainCallback();
        return;
    }

    std::map<std::string, std::string> claims;
    if (!VerifyJwtToken(token, claims)) {
        filterCallback(MakeUnauthorizedResponse("Unauthorized: Invalid token"));
        return;
    }

    auto userIdIt = claims.find("sub");
    if (userIdIt == claims.end()) {
        filterCallback(MakeUnauthorizedResponse("Unauthorized: Invalid token claims"));
        return;
    }
    uint64_t userId = std::stoull(userIdIt->second);

    if (!ValidateUser(userId, req)) {
        filterCallback(MakeUnauthorizedResponse("Unauthorized: User not found or inactive"));
        return;
    }
    filterChainCallback();
}

bool JwtFilter::GetKey(const drogon::HttpRequestPtr& req, const std::string& token, std::string& key) {
    std::string adminKey = CONFIG_MANAGER.GetString(std::string(Auth::Section), std::string(Auth::KeyAdminKey), "");
    if (!adminKey.empty() && token == adminKey) {
        req->attributes()->insert("userId", static_cast<uint64_t>(1));
        req->attributes()->insert("username", std::string("admin"));
        return false;
    }
    key = adminKey;
    return true;
}

bool JwtFilter::ShouldSkipJwtCheck(const std::string& path) const {
    return path == "/api/users/login";
}

std::string JwtFilter::ExtractJwtToken(const drogon::HttpRequestPtr& req) const {
    std::string authHeader = req->getHeader("Authorization");
    if (authHeader.empty()) {
        return "";
    }
    if (authHeader.find("Bearer ") == 0) {
        return authHeader.substr(7);
    }
    return authHeader;
}

bool JwtFilter::VerifyJwtToken(const std::string& token, std::map<std::string, std::string>& claims) const {
    std::string secretKey =
        CONFIG_MANAGER.GetString(std::string(Auth::Section), "jwt_secret", "general_agent_jwt_secret_key_default");
    return common::security::JwtUtils::VerifyToken(token, secretKey, claims);
}

drogon::HttpResponsePtr JwtFilter::MakeUnauthorizedResponse(const std::string& msg) const {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k401Unauthorized);
    resp->setBody(std::string("{\"code\": 401, \"message\": \"") + msg + "\"}");
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    return resp;
}
