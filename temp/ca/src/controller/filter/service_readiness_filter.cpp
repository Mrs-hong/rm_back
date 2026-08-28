//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "common/http/http.h"
#include "common/service_readiness.h"
#include "controller/filter/service_readiness_filter.h"

// 将服务状态映射为中文原因说明 (非 Ready 状态)
static std::string_view StatusToReason(qifeng_ca::ServiceReadiness::ServiceStatus status) {
    using S = qifeng_ca::ServiceReadiness::ServiceStatus;
    switch (status) {
        case S::UpgradeInProgress:
            return "系统升级中, 暂不可用";
        case S::ResetSystem:
            return "系统重置中, 暂不可用";
        case S::NotReady:
            return "服务未就绪, 请稍后重试";
        default:
            return "服务不可用";
    }
}

CAServiceReadinessFilter::CAServiceReadinessFilter() {
    FLOG_INFO("CAServiceReadinessFilter Init");
}

void CAServiceReadinessFilter::doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&filterCallback,
                                        drogon::FilterChainCallback &&filterChainCallback) {
    (void)req;
    auto &readiness = qifeng_ca::ServiceReadiness::GetInstance();
    if (readiness.IsReady()) {
        filterChainCallback();
        return;
    }

    auto status = readiness.GetStatus();
    auto reason = StatusToReason(status);
    SLOG_WARN << "CAServiceReadinessFilter: blocked, status=" << static_cast<int>(status)
              << ", reason=" << reason << ", path=" << req->path();

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k503ServiceUnavailable);
    resp->setContentTypeString("application/json");
    std::string body = "{\"code\":-1,\"status\":";
    body += std::to_string(static_cast<int>(status));
    body += ",\"message\":\"";
    body.append(reason.data(), reason.size());
    body += "\"}";
    resp->setBody(body);
    filterCallback(resp);
}

QIFENG_CA_HTTP_FILTER_REGISTRY(CAServiceReadinessFilter);
