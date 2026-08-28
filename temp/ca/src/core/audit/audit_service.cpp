//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "common/audit_action_registry.h"
#include "common/common.h"
#include "core/audit/audit_service.h"
#include "dao/models/bms_audit.h"

namespace qifeng_ca {

    AuditDao &AuditService::GetDao() {
        static AuditDao SDao;
        return SDao;
    }

    Status AuditService::SearchAuditLog(const AuditSearchRequest &req, AuditSearchResponse* resp) {
        if (req.page_size() > 100 || req.page_size() <= 0) {
            return Status {-1, "page_size 参数异常，范围1-100"};
        }
        if (req.current() <= 0) {
            return Status {-1, "current 参数异常"};
        }

        AuditSearchFilter filter;
        filter.mCurrent = req.current();
        filter.mPageSize = req.page_size();

        // accounts数组: 空则跳过account搜索
        for (const auto &acc : req.accounts()) {
            filter.mUserAccounts.push_back(acc);
        }
        // actions数组: 校验注册表后过滤, 空则跳过
        auto &registry = ListenActionRegistry::GetInstance();
        for (const auto &act : req.actions()) {
            if (registry.IsValidAction(act)) {
                filter.mActions.push_back(act);
            }
        }
        if (req.has_start_time()) {
            filter.mStartTime = req.start_time();
        }
        if (req.has_end_time()) {
            filter.mEndTime = req.end_time();
        }
        if (req.has_success()) {
            filter.mSuccessFilter = req.success();
        }
        if (filter.mStartTime < 0 || filter.mEndTime < 0 || filter.mEndTime < filter.mStartTime) {
            return Status {-1, "时间范围异常"};
        }

        AuditSearchResult result = GetDao().Search(filter);
        FillSearchResponse(result, resp);
        return Status {};
    }

    void AuditService::FillSearchResponse(const AuditSearchResult &result, AuditSearchResponse* resp) {
        resp->set_total(result.mTotal);
        for (const auto &r : result.mRecords) {
            auto* item = resp->add_records();
            item->set_account_id(r.mUserId);
            item->set_account(r.mUserAccount);
            item->set_account_name(r.mUserName);
            item->set_group_name(GetGroupName(r.mUserGroupId));
            item->set_action(r.mAction);
            item->set_action_desc(r.mActionDesc);
            item->set_success(r.mSuccess ? 1 : 0);
            item->set_error_message(r.mErrorMessage);
            item->set_create_time(r.mCreateTime);
            item->set_request_data(r.mRequestData);
        }
    }

}  // namespace qifeng_ca
