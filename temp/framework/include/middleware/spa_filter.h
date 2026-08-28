/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MIDDLEWARE_SPA_FILTER_H
#define QIFENG_FRAMEWORK_INCLUDE_MIDDLEWARE_SPA_FILTER_H

#include "drogon/HttpFilter.h"

class SPAFilter : public drogon::HttpFilter<SPAFilter, false> {
public:
    void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& fcb,
                  drogon::FilterChainCallback&& fccb) override {
        (void)fcb;
        // 如果是API请求，直接通过
        if (req->path().find("/api/") == 0) {
            return fccb();
        }

        // 检查是否是静态文件请求
        auto pos = req->path().find_last_of('.');
        if (pos != std::string::npos) {
            std::string ext = req->path().substr(pos);
            if (ext.length() < 5) {
                return fccb();
            }
        }

        // 其他请求重定向到index.html
        req->setPath("/index.html");
        return fccb();
    }
};

#endif  // QIFENG_FRAMEWORK_INCLUDE_MIDDLEWARE_SPA_FILTER_H