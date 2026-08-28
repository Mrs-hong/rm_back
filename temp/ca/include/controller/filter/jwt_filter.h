/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_FILTER_JWT_MIDDLEWARE_H
#define QIFENG_CA_INCLUDE_CONTROLLER_FILTER_JWT_MIDDLEWARE_H

#include "qifeng_framework/middleware/jwt_middleware.h"

class CAJwtFilter : public JwtFilter {
public:
    CAJwtFilter();

    bool ValidateUser(uint64_t userId, const drogon::HttpRequestPtr &req) const override;

    bool GetKey(const drogon::HttpRequestPtr &req, const std::string &token, std::string &key) override;

    bool ShouldSkipJwtCheck(const std::string &path) const override;

    bool VerifyJwtToken(const std::string &token, std::map<std::string, std::string> &claims) const override;
};

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_FILTER_JWT_MIDDLEWARE_H
