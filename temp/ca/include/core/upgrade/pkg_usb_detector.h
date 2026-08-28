/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>

#include "core/upgrade/upgrade_detector_base.h"

namespace qifeng_ca {

    // USB 升级包检测类: 扫描 USB 大容量存储挂载点, 发现升级包并拷贝到本地保存目录。
    // 升级包文件名约定: qifeng_*_upgrade_*.tar.gz, 同目录须存在 <包名>.sha256 清单文件。
    // FindUpgradePackage 阶段仅扫描枚举(不拷贝),
    // PrepareUpgradePackage 阶段将升级包与 sha256 文件拷贝到 savePath。
    class PkgUsbDetector : public UpgradeDetectorBase {
    public:
        PkgUsbDetector(std::string currentVersion, std::string savePath);

        // 发现可用升级包: 扫描 USB 挂载点, 返回候选列表(不拷贝)
        std::vector<PackageInfo> FindUpgradePackage() override;

        // 准备升级包: 将 USB 上的升级包与 sha256 文件拷贝到 savePath, 填充 out 本地路径
        Status PrepareUpgradePackage(const PackageInfo &candidate, PackageInfo &out) override;
    };

}  // namespace qifeng_ca
