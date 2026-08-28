//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_UPGRADE_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_UPGRADE_CONFIG_H

#include <string>
#include <utility>
#include <vector>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    // 服务升级相关配置单例
    // 读取 config.yaml 中 upgrade 段配置
    class UpgradeConfig {
    public:
        // ===================== data/.version 文件字段名 =====================
        // 跨模块契约: 升级流程写入, 设备信息采集读取展示
        static constexpr const char* kFieldMainVersion = "VERSION";     // 主版本号(升级包 metadata.version)
        static constexpr const char* kFieldSubVersion = "U_VERSION";    // 子版本号(基于主版本升级次数, 4位数字)
        static constexpr const char* kFieldScreenVersion = "V_SCREEN";  // 屏幕固件组件版本
        static constexpr const char* kFieldCaVersion = "V_CA";          // CA 软件包组件版本
        static constexpr const char* kFieldScmVersion = "V_SCM";        // SCM deb 包组件版本
        static constexpr const char* kFieldModelVersion = "V_MODEL";    // 模型文件组件版本
        static constexpr const char* kFieldNginxVersion = "V_NGINX";    // nginx 配置组件版本

        static UpgradeConfig &GetInstance() {
            static UpgradeConfig Instance;
            return Instance;
        }

        // 升级软件包存放目录(组件部署目录, CleanUpgradeDir 清理范围)
        std::string GetUpgradeSoftDir() const {
            return CONFIG_MANAGER.GetString("upgrade", "soft_dir", "data/src/upgrade/soft");
        }

        // 升级专用临时目录(存放上传的软件包等临时文件, 位于升级目录下)
        std::string GetUpgradeTmpDir() const { return GetUpgradeSoftDir() + "/tmp"; }

        // 升级结果文件路径(由 qf_scmc 写入, qifeng_ca 读取)
        std::string GetUpgradeResultPath() const {
            return CONFIG_MANAGER.GetString("upgrade", "result_path", "data/src/upgrade/upgrade_result.json");
        }

        // ===================== OTA 升级配置 =====================
        // OTA 总开关
        bool IsOtaEnable() const { return IsBoolConfigTrue(CONFIG_MANAGER.GetString("upgrade.ota", "enable", "true")); }

        // 网络升级开关
        bool IsNetEnable() const {
            return IsBoolConfigTrue(CONFIG_MANAGER.GetString("upgrade.ota", "net_enable", "true"));
        }

        // USB 升级开关
        bool IsUsbEnable() const {
            return IsBoolConfigTrue(CONFIG_MANAGER.GetString("upgrade.ota", "usb_enable", "true"));
        }

        // 探测间隔(秒)
        int32_t GetDetectIntervalSec() const {
            return static_cast<int32_t>(CONFIG_MANAGER.GetInt("upgrade.ota", "detect_interval_sec", 3600));
        }

        // 用户确认超时(秒)
        int32_t GetConfirmTimeoutSec() const {
            return static_cast<int32_t>(CONFIG_MANAGER.GetInt("upgrade.ota", "confirm_timeout_sec", 600));
        }

        // 云端升级软件包管理服务地址(HTTP, GET /getNewVersion 直接返回单个版本对象)
        std::string GetCloudBaseUrl() const {
            return CONFIG_MANAGER.GetString("upgrade.ota", "cloud_base_url", "http://192.168.112.164:28283");
        }

        // OTA 下载临时目录(下载/校验, 用完即清)
        std::string GetOtaTmpDir() const {
            return CONFIG_MANAGER.GetString("upgrade.ota", "ota_tmp_dir", "data/src/upgrade/ota_tmp");
        }

        // 设备版本文件路径(data/.version, 含 VERSION= 字段, 与 device_config 共用)
        std::string GetVersionFilePath() const {
            return CONFIG_MANAGER.GetString("device", "version_config_path", "data/.version");
        }

        // 读取当前已安装版本号(从 data/.version 的 VERSION= 字段解析, 失败返回 "0.0.0")
        std::string GetCurrentVersion() const;

        // 读取 .version 中指定字段的值(精确匹配行首 "key=")
        // 文件不存在/字段不存在/值为空时返回 defaultValue
        std::string GetVersionField(const std::string &key, const std::string &defaultValue) const;

        // 批量写入 .version 字段(单次原子写入, 未涉及的字段保持不变, 缺失字段追加到末尾)
        // 用于升级成功后一次性写回主版本/子版本/各组件版本, 保证写入原子性
        bool SetVersionFields(const std::vector<std::pair<std::string, std::string>> &fields) const;

        // 读取子版本号(U_VERSION 字段, 默认 "0000")
        // 子版本号记录基于当前主版本完成升级的次数, 与主版本组合展示(如 2.11.1.u0001)
        std::string GetSubVersion() const;

        // 读取组件版本号(V_SCREEN/V_CA/V_SCM/V_MODEL/V_NGINX, 默认 "0.0.0")
        // 用于升级时组件版本门槛校验
        std::string GetComponentVersion(const std::string &fieldKey) const;

        // 待提交版本文件路径(data/src/upgrade/pending_version)
        // CA/SCM 升级会杀进程, 升级前将目标版本写入此文件; 下次启动 CheckUpgradeResultAndSetComplete
        // 检测到升级成功后, 从此文件读取版本号更新 .version, 并清理此文件
        std::string GetPendingVersionPath() const {
            return CONFIG_MANAGER.GetString("upgrade", "pending_version_path", "data/src/upgrade/pending_version");
        }

    private:
        UpgradeConfig() = default;
        ~UpgradeConfig() = default;
        UpgradeConfig(const UpgradeConfig &) = delete;
        UpgradeConfig &operator=(const UpgradeConfig &) = delete;
        UpgradeConfig(UpgradeConfig &&) = delete;
        UpgradeConfig &operator=(UpgradeConfig &&) = delete;

        // 解析 bool 配置字符串: "true"/"1"/"yes" 视为真, 其余为假
        static bool IsBoolConfigTrue(const std::string &val) { return val == "true" || val == "1" || val == "yes"; }
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_UPGRADE_CONFIG_H
