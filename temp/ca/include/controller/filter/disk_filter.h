/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_FILTER_DISK_FILTER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_FILTER_DISK_FILTER_H

#include "drogon/HttpFilter.h"

class CADiskFilter : public drogon::HttpFilter<CADiskFilter, false> {
public:
    CADiskFilter();

    void doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&filterCallback,
                  drogon::FilterChainCallback &&filterChainCallback) override;
};

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_FILTER_DISK_FILTER_H
