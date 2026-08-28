/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MIDDLEWARE_JWT_MIDDLEWARE_H
#define QIFENG_FRAMEWORK_INCLUDE_MIDDLEWARE_JWT_MIDDLEWARE_H

#include "drogon/HttpFilter.h"

/**
 * @brief JWT过滤器，用于验证JWT令牌和记录访问日志
 */
class JwtFilter : public drogon::HttpFilter<JwtFilter, false> {
public:
    /**
     * @brief 构造函数，必须提供以确保过滤器正确注册
     */
    JwtFilter();

    /**
     * @brief  子类实现校验用户逻辑
     */
    virtual bool ValidateUser(uint64_t userId, const drogon::HttpRequestPtr& req) const = 0;

    /**
     * @brief 处理请求，验证JWT令牌并记录访问日志
     */
    void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& filterCallback,
                  drogon::FilterChainCallback&& filterChainCallback) override;

    // 从请求中获取key
    virtual bool GetKey(const drogon::HttpRequestPtr& req, const std::string& token, std::string& key);

    /**
     * @brief 是否跳过JWT校验
     */
    virtual bool ShouldSkipJwtCheck(const std::string& path) const;

    /**
     * @brief 提取JWT Token
     */
    virtual std::string ExtractJwtToken(const drogon::HttpRequestPtr& req) const;

    /**
     * @brief 校验JWT Token
     */
    virtual bool VerifyJwtToken(const std::string& token, std::map<std::string, std::string>& claims) const;

    /**
     * @brief 生成401响应
     */
    virtual drogon::HttpResponsePtr MakeUnauthorizedResponse(const std::string& msg) const;
};

#endif  // QIFENG_FRAMEWORK_INCLUDE_MIDDLEWARE_JWT_MIDDLEWARE_H
