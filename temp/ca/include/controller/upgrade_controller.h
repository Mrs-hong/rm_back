//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_UPGRADE_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_UPGRADE_CONTROLLER_H

#include "drogon/DrObject.h"
#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/upgrade.pb.h"

#include "common/http/http.h"
#include "common/status.h"
#include "core/upgrade/upgrade_service.h"

namespace qifeng_ca {

    // 服务升级控制器
    // 提供四个接口:
    //   1. POST /sys/upgrade/upgrade        - 执行升级(需登录+权限, multipart上传)
    //   2. POST /sys/upgrade/getUpgradeResult - 查询升级结果(无认证, 无状态)
    //   3. POST /sys/upgrade/applyOta       - 确认 OTA 升级(需登录+权限)
    //   4. POST /sys/upgrade/cancelOta      - 取消 OTA 升级(需登录+权限)
    class UpgradeController final : public drogon::DrObject<UpgradeController> {
    public:
        QIFENG_CA_METHOD_LIST_BEGIN(UpgradeController);

        // 升级接口: 需登录+权限, multipart上传 软件包 + sha256清单文件
        QIFENG_CA_METHOD_PREREQ_ADD("服务升级", Upgrade, BmsPreUploadUpgradeReq, "/sys/upgrade/upgrade", drogon::Post,
                                    "CADiskFilter");

        // 查询升级结果: 无认证, 无状态访问
        QIFENG_CA_METHOD_ADD_NO_FILTER(ActionDesc("查询升级结果", false), GetUpgradeResult,
                                       "/sys/upgrade/getUpgradeResult", drogon::Get);

        // 确认 OTA 升级: 需登录+权限, 触发已下载的 OTA 升级包升级
        QIFENG_CA_METHOD_PREREQ_ADD("确认OTA升级", ApplyOta, BmsPreAccountIdReq<UpgradeRequest>, "/sys/upgrade/applyOta",
                                    drogon::Post);

        // 取消 OTA 升级: 需登录+权限, 清理已下载的 OTA 升级包
        QIFENG_CA_METHOD_ADD("取消OTA升级", CancelOta, "/sys/upgrade/cancelOta", drogon::Post);

        QIFENG_CA_METHOD_LIST_END;

        // 执行升级
        Status Upgrade(const UpgradeRequest &req, UpgradeResponse &resp);

        // 查询升级结果
        Status GetUpgradeResult(const Empty &req, GetUpgradeResultResponse &resp);

        // 确认 OTA 升级(使用已下载待确认的 OTA 升级包)
        Status ApplyOta(const UpgradeRequest &req, UpgradeResponse &resp);

        // 取消 OTA 升级(清理已下载的 OTA 升级包)
        Status CancelOta(const Empty &req, UpgradeResponse &resp);

    private:
        UpgradeService mUpgradeService;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_UPGRADE_CONTROLLER_H
