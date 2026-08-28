/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>

#include "core/upgrade/upgrade_detector_base.h"

namespace qifeng_ca {

    // 网络升级包检测类: 从云端升级软件包管理服务发现并下载升级包。
    // 云端接口契约(v2, 带设备 SN 鉴权):
    //   GET {cloudBaseUrl}/getNewVersion?sn=<sophon_sn>
    //   返回 JSON(单个版本对象, 非数组):
    //     {
    //       "version":"1.1.1",
    //       "soft_url":"<filename>",      // 文件名(非完整URL), 需拼接 download 接口
    //       "sha256_url":"<filename>",    // 文件名(非完整URL), 需拼接 download 接口
    //       "size":<n>,
    //       "password":"<hex>"            // AES-256-CBC 对称加密密钥(OtaCipher 混淆密文)
    //     }
    //   下载接口: GET {cloudBaseUrl}/download?sn=<sophon_sn>&filename=<filename>
    //   软件包下载后统一用 password 通过 openssl enc -d -aes-256-cbc -pbkdf2 解密为明文 tar.gz
    //     (不强制 .aes 后缀, 解密失败即视为非法包), 再做 SHA256 校验
    // FindUpgradePackage 阶段仅拉取版本元数据(不下载),
    // PrepareUpgradePackage 阶段下载 soft_url 与 sha256_url 到 savePath, 解密软件包并校验。
    class PkgNetDetector : public UpgradeDetectorBase {
    public:
        // sophonSn: bm1684x 核心板 SN, 用于云端鉴权(getNewVersion 与 download 接口的 sn 参数)
        PkgNetDetector(std::string currentVersion, std::string savePath, std::string cloudBaseUrl,
                       std::string sophonSn);

        // 发现可用升级包: GET {cloudBaseUrl}/getNewVersion?sn=<sophonSn>, 解析返回候选列表(0/1 个, 不下载)
        std::vector<PackageInfo> FindUpgradePackage() override;

        // 准备升级包: 下载 soft_url + sha256_url 到 savePath, 统一解密为明文 tar.gz, 填充 out 本地路径
        Status PrepareUpgradePackage(const PackageInfo &candidate, PackageInfo &out) override;

    private:
        std::string mCloudBaseUrl;  // 云端升级服务基地址(如 http://192.168.112.164:28283)
        std::string mSophonSn;      // bm1684x 核心板 SN(云端鉴权用)
    };

}  // namespace qifeng_ca
