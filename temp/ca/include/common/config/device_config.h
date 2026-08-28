//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_DEVICE_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_DEVICE_CONFIG_H

#include <string>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    enum class DeviceArch { kX86_64, kAarch64, kUnknown };

    class DeviceConfig {
    public:
        static DeviceConfig &GetInstance() {
            static DeviceConfig Instance;
            return Instance;
        }

        std::string GetArchName() const {
            if (mArch == DeviceArch::kX86_64) {
                return "x86_64";
            } else if (mArch == DeviceArch::kAarch64) {
                return "aarch64";
            } else {
                return "unknown";
            }
        }

        std::string GetNpuBasicCmd() const {
            return CONFIG_MANAGER.GetString("device", "npu_basic_cmd", "/opt/sophon/libsophon-current/bin/bm-smi");
        }

        std::string GetNpuTempCmd() const {
            return CONFIG_MANAGER.GetString("device", "npu_temp_cmd", "/sbin/bm_get_temperature");
        }

        std::string GetGpuCmd() const { return CONFIG_MANAGER.GetString("device", "gpu_cmd", "nvidia-smi"); }

        std::string GetVersionConfigPath() const {
            return CONFIG_MANAGER.GetString("device", "version_config_path", "data/.version");
        }

        std::string GetDataDiskMountPath() const {
            return CONFIG_MANAGER.GetString("device", "data_disk_mount_path", "/data2");
        }

        int GetCollectIntervalSec() const {
            int interval = CONFIG_MANAGER.GetInt("device", "collect_interval_sec", 60);
            return (interval < 1) ? 5 : interval;
        }

        int GetDataRetentionDays() const {
            int days = CONFIG_MANAGER.GetInt("device", "data_retention_days", 90);
            return (days < 1) ? 90 : days;
        }

        // 读取配置文件大小上限
        int GetConfigFileMaxSize() const {
            static constexpr int MaxSize = 1024 * 1024;
            int maxSize = CONFIG_MANAGER.GetInt("device", "config_max_size", MaxSize);
            return (maxSize < 1 || maxSize > 2 * MaxSize) ? 2 * MaxSize : maxSize;
        }

        bool IsArm() const { return mArch == DeviceArch::kAarch64; }

        bool IsX86() const { return mArch == DeviceArch::kX86_64; }

    private:
        DeviceConfig() { DetectArchitecture(); }
        ~DeviceConfig() = default;
        DeviceConfig(const DeviceConfig &) = delete;
        DeviceConfig &operator=(const DeviceConfig &) = delete;
        DeviceConfig(DeviceConfig &&) = delete;
        DeviceConfig &operator=(DeviceConfig &&) = delete;

        void DetectArchitecture() {
#if defined(__x86_64__) || defined(_M_X64)
            mArch = DeviceArch::kX86_64;
#elif defined(__aarch64__) || defined(_M_ARM64)
            mArch = DeviceArch::kAarch64;
#else
            mArch = DeviceArch::kUnknown;
#endif
        }

        DeviceArch mArch = DeviceArch::kUnknown;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_DEVICE_CONFIG_H
