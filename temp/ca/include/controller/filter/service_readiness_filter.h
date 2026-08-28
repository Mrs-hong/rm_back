//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_FILTER_SERVICE_READINESS_FILTER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_FILTER_SERVICE_READINESS_FILTER_H

#include "drogon/HttpFilter.h"

// 服务状态校验过滤器
class CAServiceReadinessFilter : public drogon::HttpFilter<CAServiceReadinessFilter, false> {
public:
    CAServiceReadinessFilter();

    void doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&filterCallback,
                  drogon::FilterChainCallback &&filterChainCallback) override;
};

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_FILTER_SERVICE_READINESS_FILTER_H
