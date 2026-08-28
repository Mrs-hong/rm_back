/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "qifeng_framework/common/logger.h"

#include "common/device/disk_check.h"
#include "common/http/http.h"
#include "common/utils/symlink_manager.h"
#include "common/ws/system_message_notifier.h"
#include "controller/filter/disk_filter.h"
#include "core/system/tasks/cleanup_task.h"

CADiskFilter::CADiskFilter() {
    FLOG_INFO("CADiskFilter Init");
}

void CADiskFilter::doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&filterCallback,
                            drogon::FilterChainCallback &&filterChainCallback) {
    auto accountId = req->attributes()->get<uint64_t>("accountId");
    // 校验data2是否可用
    auto &symMgr = qifeng_ca::SymlinkManager::GetInstance();
    if (!symMgr.IsData2Available()) {
        SLOG_ERROR << "UploadOfflineRecording: /data2 symlink not available, cannot upload";
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k503ServiceUnavailable);
        resp->setBody("{\"code\":503,\"message\":\"存储磁盘异常\"}");
        filterCallback(resp);
        return;
    }
    auto &diskCheck = qifeng_ca::DiskCheck::GetInstance();
    if (!diskCheck.IsDiskLow(accountId, 0, true).IsSuccess()) {
        SLOG_WARN << "CADiskFilter: request blocked due to low disk, path=" << req->path();
        qifeng_ca::SystemMessageNotifier::GetInstance().SendDiskError(accountId);

        // 触发异步清理临时文件, 等待清理完成后重检磁盘空间
        qifeng_ca::CleanupTask::GetInstance().TriggerCleanup();
        qifeng_ca::CleanupTask::GetInstance().WaitForCleanupFinish(1);
        auto diskStatus = diskCheck.IsDiskLow(accountId, 1, false);
        if (!diskStatus.IsSuccess()) {
            SLOG_ERROR << "CADiskFilter: disk still low after cleanup, path=" << req->path();
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody(R"({"code":503,"message":")" + diskStatus.GetMsg() + "\"}");
            filterCallback(resp);
            return;
        }
        SLOG_INFO << "CADiskFilter: disk recovered after cleanup, path=" << req->path();
    }

    filterChainCallback();
}

QIFENG_CA_HTTP_FILTER_REGISTRY(CADiskFilter);
