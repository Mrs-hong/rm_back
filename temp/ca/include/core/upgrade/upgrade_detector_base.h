/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>
#include <vector>

#include "common/status.h"

namespace qifeng_ca {

    // 升级包信息
    // source 标识来源: "net"(云端下载) / "usb"(U盘拷贝)
    // package_path/sha256_path 在 FindUpgradePackage 阶段为来源定位(USB 为原始路径、net 为下载文件名),
    // 在 PrepareUpgradePackage 完成后为本地保存路径
    // password 仅 net 来源有效: 云端返回的 AES-256-CBC 对称加密密钥, 用于解密 .aes 软件包
    struct PackageInfo {
        std::string package_path;  // 升级包路径(Find 阶段为来源URL/原始路径/下载文件名, Prepare 后为本地路径)
        std::string package_name;  // 升级包文件名
        std::string package_version;  // 版本号(如 "1.0.7")
        std::string sha256_path;      // sha256 清单文件路径(Find 阶段为来源, Prepare 后为本地路径)
        std::string source;           // 来源标记: "net" / "usb"
        std::string password;         // AES 解密密钥(仅 net 来源, Find 阶段从云端获取, Prepare 阶段用于解密)
    };

    /**
     * @brief 升级包检测基类
     * @details 定义"发现 + 准备 + 校验"三段式接口:
     *          1. FindUpgradePackage: 廉价, 仅枚举可用升级包元数据(不下载)
     *          2. PrepareUpgradePackage: 昂贵, 下载/拷贝升级包到本地保存目录
     *          3. VerifyUpgradePackage: SHA256 完整性校验
     */
    class UpgradeDetectorBase {
    public:
        UpgradeDetectorBase(std::string currentVersion, std::string savePath)
            : mCurrentVersion(std::move(currentVersion)), mSavePath(std::move(savePath)) {}

        virtual ~UpgradeDetectorBase() = default;

        UpgradeDetectorBase(const UpgradeDetectorBase &) = delete;
        UpgradeDetectorBase &operator=(const UpgradeDetectorBase &) = delete;
        UpgradeDetectorBase(UpgradeDetectorBase &&) noexcept = delete;
        UpgradeDetectorBase &operator=(UpgradeDetectorBase &&) = delete;

        /**
         * @brief 发现可用升级包(仅元数据, 不下载)
         * @return std::vector<PackageInfo> 候选升级包列表(版本号 + 来源定位)
         */
        virtual std::vector<PackageInfo> FindUpgradePackage() = 0;

        /**
         * @brief 准备升级包(下载/拷贝到 mSavePath), 填充 out 的本地路径
         * @param candidate FindUpgradePackage 返回的候选包
         * @param out 准备完成后的升级包信息(含本地 package_path/sha256_path)
         * @return Status 准备结果
         */
        virtual Status PrepareUpgradePackage(const PackageInfo &candidate, PackageInfo &out) = 0;

        /**
         * @brief SHA256 校验升级包完整性(委托 Verify::VerifyUpgradePackage)
         * @param packagePath 升级包本地路径
         * @param sha256Path sha256 清单文件本地路径
         * @return Status 校验结果
         */
        static Status VerifyUpgradePackage(const std::string &packagePath, const std::string &sha256Path);

        const std::string &GetCurrentVersion() const { return mCurrentVersion; }
        const std::string &GetSavePath() const { return mSavePath; }

    protected:
        std::string mCurrentVersion;  // 当前服务版本
        std::string mSavePath;        // 升级包下载保存目录
    };

    /**
     * @brief 语义化版本号比较(点分数字, 如 "1.0.7")
     * @return a<b 返回 -1, 相等返回 0, a>b 返回 1; 非数字分量按字符串比较
     */
    int CompareVersion(const std::string &a, const std::string &b);

    /**
     * @brief 候选版本是否比当前版本新
     */
    bool IsNewerVersion(const std::string &candidate, const std::string &current);

    /**
     * @brief 从升级包文件名解析版本号
     * @details 形如 "qifeng_xxx_upgrade_1.0.7.tar.gz" → "1.0.7"
     *          匹配最后一个 "_upgrade_" 之后、扩展名之前的版本段
     * @return 解析出的版本号, 失败返回空串
     */
    std::string ParseVersionFromFileName(const std::string &fileName);

    /**
     * @brief 从候选列表中选出不低于当前版本的最新升级包
     * @details 相等亦接受: 同主版本重升级场景(云端重推同版本包), 设备端由子版本号 U_VERSION 区分
     * @param candidates 候选列表
     * @param currentVersion 当前版本
     * @return 最新的候选指针, 无不低于当前的候选返回 nullptr
     */
    const PackageInfo* SelectLatestCandidate(const std::vector<PackageInfo> &candidates,
                                             const std::string &currentVersion);

}  // namespace qifeng_ca
