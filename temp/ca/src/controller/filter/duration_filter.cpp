/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "qifeng_framework/common/logger.h"

#include "common/audio/audio_hal_utils.h"
#include "common/http/http.h"
#include "controller/filter/duration_filter.h"
#include "core/dashboard/dashboard_service.h"
#include "internal/display_manager.h"
#include "internal/hal/hal_bridge.h"

CADurationFilter::CADurationFilter() {
    FLOG_INFO("CADurationFilter Init");
}

void CADurationFilter::doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&filterCallback,
                                drogon::FilterChainCallback &&filterChainCallback) {
    auto info = qifeng_ca::DashboardService::CollectDiskTimeInfo();
    if (info.availableHours < 3) {
        if (qifeng_ca::HalBridge::GetInstance().IsRecordingActive()) {
            qifeng_ca::SetupFingerprintLed();
        } else {
            qifeng_ca::StopFingerprintLed();
        }
        qifeng_ca::DisplayManager::GetInstance().ShowDiskError();
        SLOG_WARN << "CADurationFilter: request blocked due to low recording time, path=" << req->path()
                  << ", availableHours=" << info.availableHours;
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody("{\"code\":-1,\"message\":\"可用录音时长不足3小时\"}");
        filterCallback(resp);
        return;
    }

    filterChainCallback();
}

QIFENG_CA_HTTP_FILTER_REGISTRY(CADurationFilter);
