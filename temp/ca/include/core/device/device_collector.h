//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_DEVICE_DEVICE_COLLECTOR_H
#define QIFENG_CA_INCLUDE_CORE_DEVICE_DEVICE_COLLECTOR_H

#include <cstdint>
#include <string>

#include "common/config/device_config.h"
#include "qifeng_ca/device.pb.h"

namespace qifeng_ca {

    class DeviceCollector {
    public:
        DeviceCollector() = default;
        ~DeviceCollector() = default;
        DeviceCollector(const DeviceCollector &) = delete;
        DeviceCollector &operator=(const DeviceCollector &) = delete;
        DeviceCollector(DeviceCollector &&) = default;
        DeviceCollector &operator=(DeviceCollector &&) = default;

        bool CollectSystemInfo(SystemInfoItem &item);

        std::string GetArchName() const;

    private:
        struct CpuStat {
            uint64_t mUser {0};
            uint64_t mNice {0};
            uint64_t mSystem {0};
            uint64_t mIdle {0};
            uint64_t mIowait {0};
            uint64_t mIrq {0};
            uint64_t mSoftirq {0};
            uint64_t mSteal {0};
            bool mValid {false};

            uint64_t Total() const { return mUser + mNice + mSystem + mIdle + mIowait + mIrq + mSoftirq + mSteal; }
            uint64_t Idle() const { return mIdle + mIowait; }
        };

        bool CollectVersionInfo(const std::string &configPath, SystemInfoItem &item);
        bool CollectCpuInfo(SystemInfoItem &item);
        float CollectCpuUtilization();
        bool CollectMemoryInfo(SystemInfoItem &item);
        bool CollectDiskInfo(const std::string &mountPath, SystemInfoItem &item);
        bool CollectAcceleratorInfo(SystemInfoItem &item);
        bool CollectBatteryInfo(SystemInfoItem &item);
        bool CollectConnectInfo(SystemInfoItem &item);
        bool CollectAudioInfo(SystemInfoItem &item);

        bool CollectCpuTemperatureArm(SystemInfoItem &item);
        bool CollectCpuTemperatureX86(SystemInfoItem &item);
        void CollectCpuTempFromSensors(CPUInfoItem &cpu);
        bool CollectAcceleratorInfoArm(const std::string &basicCmd, const std::string &tempCmd, SystemInfoItem &item);
        bool CollectAcceleratorInfoX86(const std::string &gpuCmd, SystemInfoItem &item);
        void ParseNpuSmiOutput(const std::string &output, NPUInfoItem &npu);
        void ParseNpuTemperature(const std::string &output, NPUInfoItem &npu);
        void ParseGpuCsvOutput(const std::string &output, GPUInfoItem &gpu);

        static void TrimQuotes(std::string &s);
        static void TrimWhitespace(std::string &s);
        static std::string ExecCommand(const std::string &cmd, int timeoutMs = 3000);
        static std::string ReadFileContent(const std::string &path);

        CpuStat mPrevCpuStat;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_DEVICE_DEVICE_COLLECTOR_H
