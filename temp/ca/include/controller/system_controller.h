//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_SYSTEM_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_SYSTEM_CONTROLLER_H

#include "drogon/DrObject.h"
#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/system.pb.h"
#include "qifeng_ca/user.pb.h"

#include "common/http/http.h"
#include "common/status.h"
#include "core/system/system_diagnose_service.h"
#include "core/system/system_reset_service.h"

namespace qifeng_ca {

    class SystemController final : public drogon::DrObject<SystemController> {
    public:
        QIFENG_CA_METHOD_LIST_BEGIN(SystemController);

        QIFENG_CA_METHOD_PREREQ_ADD("重置系统", ResetSystem, BmsPreAccountIdReq<AccountRequst>,
                                    "/sys/system/resetSystem", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("下载诊断日志", DiagnoseLog, BmsPreAccountIdReq<AccountRequst>,
                                    "/sys/system/diagnoseLog", drogon::Post, "CADiskFilter");

        QIFENG_CA_METHOD_LIST_END;

        Status ResetSystem(const AccountRequst &req, Empty &resp);

        Status DiagnoseLog(const AccountRequst &req, DiagnoseLogResponse &resp);

    private:
        SystemResetService &mResetService {SystemResetService::GetInstance()};
        SystemDiagnoseService mDiagnoseService;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_SYSTEM_CONTROLLER_H
