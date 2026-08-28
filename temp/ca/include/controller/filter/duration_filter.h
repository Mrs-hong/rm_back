/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_FILTER_DURATION_FILTER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_FILTER_DURATION_FILTER_H

#include "drogon/HttpFilter.h"

class CADurationFilter : public drogon::HttpFilter<CADurationFilter, false> {
public:
    CADurationFilter();

    void doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&filterCallback,
                  drogon::FilterChainCallback &&filterChainCallback) override;
};

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_FILTER_DURATION_FILTER_H
