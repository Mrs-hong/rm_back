/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>

#include "common/status.h"
#include "core/upgrade/package_metadata.h"

namespace qifeng_ca {

    /**
     * @brief 升级包解压部署器
     * @details 职责: 解压 tar 包 → 解析 metadata.json → 按元数据移动组件到升级目录。
     *          从 upgrade_service.cpp 提取, 使 UpgradeService 聚焦于升级编排而非部署细节。
     *
     * 部署流程:
     *   1. 清理升级目录残留(保留 tmp 子目录)
     *   2. 解压 tar 包到临时目录
     *   3. 校验包结构(必须有且仅有一个外层目录)
     *   4. 解析 metadata.json (必须存在, 否则部署失败)
     *   5. 按 metadata 各组件的 dir 字段精确移动组件目录
     *   6. 清理临时目录
     */
    class UpgradePackageDeployer {
    public:
        /**
         * @brief 部署升级包到目标目录
         * @param packagePath 升级包文件路径(.tar.gz/.tgz/.tar)
         * @param targetDir 升级根目录(如 data/upgrade)
         * @param outMetadata 输出解析到的元数据(可选, 无 metadata.json 时字段为空)
         * @return Status 部署结果
         */
        static Status Deploy(const std::string &packagePath, const std::string &targetDir,
                             PackageMetadata *outMetadata = nullptr);

        /**
         * @brief 部署后组件识别结果
         */
        struct DeployedComponents {
            std::string screenDir;  // screen 目录完整路径(空=无 screen 组件)
            bool hasCa = false;     // 是否包含 CA 组件(qifeng_ca/qifeng-ca/model/ngin)
            std::string debFile;    // deb 文件完整路径(空=无 deb 包)
        };

        /**
         * @brief 识别已部署目录中的组件(基于 metadata 精确定位)
         * @param targetDir 升级根目录
         * @param metadata 部署时解析的元数据(必须有效)
         * @return DeployedComponents 组件识别结果
         */
        static DeployedComponents IdentifyComponents(const std::string &targetDir,
                                                     const PackageMetadata &metadata);
    };

}  // namespace qifeng_ca
