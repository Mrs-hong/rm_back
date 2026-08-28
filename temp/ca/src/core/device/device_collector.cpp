//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/statvfs.h>

#include "qifeng_framework/common/logger.h"

#include "common/common.h"
#include "common/config/device_config.h"
#include "common/utils/disk_utils.h"
#include "core/device/device_collector.h"
#include "internal/hal/hal_bridge.h"
// #include "internal/lms/lms_model_manager.h"
#include "lms_hm/hm_qwen_infer.h"

namespace qifeng_ca {

    // 温度统一保留2位小数
    static float RoundTemp2Decimals(float value) {
        return std::round(value * 100.0F) / 100.0F;
    }

    std::string DeviceCollector::GetArchName() const {
        return DeviceConfig::GetInstance().GetArchName();
    }

    std::string DeviceCollector::ExecCommand(const std::string &cmd, int /*timeoutMs*/) {
        std::array<char, 256> buffer {};
        std::string result;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            return result;
        }
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            result += buffer.data();
        }
        pclose(pipe);
        return result;
    }

    std::string DeviceCollector::ReadFileContent(const std::string &path) {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            return "";
        }
        std::ostringstream oss;
        oss << ifs.rdbuf();
        return oss.str();
    }

    // 从/proc/cmdline中解析SN=内核参数(如 SN=HQ50CT26040002)
    // cmdline为单行空格分隔的参数串, 按token精确匹配SN=前缀, 避免误匹配其他含SN的参数
    static std::string ReadCmdlineSn() {
        std::ifstream ifs("/proc/cmdline");
        std::string line;
        if (!std::getline(ifs, line)) {
            return "";
        }
        static constexpr const char* kPrefix = "SN=";
        std::istringstream iss(line);
        std::string token;
        while (iss >> token) {
            if (token.compare(0, 3, kPrefix) == 0) {
                std::string sn = token.substr(3);
                if (!sn.empty()) {
                    return sn;
                }
            }
        }
        return "";
    }

    // 从/proc/version中提取Linux内核版本(如 5.10.226)
    // 文件格式: "Linux version 5.10.226 (build@...) (gcc ...) #3 SMP ..."
    static std::string ReadLinuxKernelVersion() {
        std::ifstream ifs("/proc/version");
        std::string line;
        if (!std::getline(ifs, line)) {
            return "";
        }
        static constexpr const char* kPrefix = "Linux version ";
        auto pos = line.find(kPrefix);
        if (pos == std::string::npos) {
            return "";
        }
        auto start = pos + std::string(kPrefix).size();
        auto end = line.find(' ', start);
        return (end == std::string::npos) ? line.substr(start) : line.substr(start, end - start);
    }

    // 读取设备序列号: /proc/cmdline 的 SN= 内核参数
    // TODO(合并master时恢复): 原SOPHON平台逻辑为解析/factory/OEMconfig.ini的DEVICE_SN行,
    //   合并时需增加设备识别(IsM50Device)分支, SOPHON走OEMconfig.ini, m50走cmdline
    static std::string ReadSerialNumber() {
        return ReadCmdlineSn();
    }

    // 读取系统固件版本: /proc/version 的 Linux 内核版本
    // TODO(合并master时恢复): 原SOPHON平台逻辑为执行bm_version命令取SophonSDK version行,
    //   合并时需增加设备识别(IsM50Device)分支, SOPHON走bm_version, m50走/proc/version
    static std::string ReadBmVersion() {
        return ReadLinuxKernelVersion();
    }

#ifdef SOPHON_PLATFORM_UNUSED
    // SOPHON(bm1684x)平台原有读取逻辑(合并master时恢复使用)
    static std::string ReadSerialNumberSophon() {
        std::ifstream file("/factory/OEMconfig.ini");
        if (!file.is_open()) {
            return "";
        }
        std::string line;
        while (std::getline(file, line)) {
            if (line.compare(0, 11, "DEVICE_SN =") == 0) {
                auto pos = line.find('=');
                if (pos != std::string::npos) {
                    std::string sn = line.substr(pos + 1);
                    // 去除首尾空白
                    sn.erase(0, sn.find_first_not_of(" \t"));
                    sn.erase(sn.find_last_not_of(" \t") + 1);
                    return sn;
                }
            }
        }
        return "";
    }

    // SOPHON(bm1684x)平台bm_version读取逻辑(合并master时恢复使用)
    static std::string ReadBmVersionSophon() {
        FILE* pipe = popen("bm_version 2>/dev/null", "r");
        if (pipe == nullptr) {
            return "";
        }
        std::array<char, 256> buf {};
        std::string result;
        while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
            std::string line(buf.data());
            if (line.find("SophonSDK version:") != std::string::npos) {
                auto pos = line.find(":");
                if (pos != std::string::npos) {
                    result = line.substr(pos + 1);
                    // 去除首尾空白
                    result.erase(0, result.find_first_not_of(" \t\r\n"));
                    result.erase(result.find_last_not_of(" \t\r\n") + 1);
                }
                break;
            }
        }
        pclose(pipe);
        return result;
    }
#endif  // SOPHON_PLATFORM_UNUSED

    bool DeviceCollector::CollectVersionInfo(const std::string &configPath, SystemInfoItem &item) {
        std::ifstream ifs(configPath);
        if (!ifs.is_open()) {
            SLOG_WARN << "Version config file not found: " << configPath;
            return false;
        }

        // 添加产品序列号与基础系统固件版本
        std::string sn = ReadSerialNumber();
        std::string bmVer = ReadBmVersion();
        auto* version = item.add_versions();
        version->set_serial_number(sn);
        version->set_bm_version(bmVer);

        // 主版本(VERSION=)与子版本(U_VERSION=), 用于组合前端显示的控制版本号
        std::string mainVersion;
        std::string subVersion;
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            auto pos = line.find('=');
            if (pos == std::string::npos) {
                continue;
            }
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            TrimQuotes(val);

            if (key == "VERSION") {
                mainVersion = val;
            } else if (key == "U_VERSION") {
                subVersion = val;
            } else if (key == "PRODUCT_VERSION") {
                version->set_product_version(val);
            } else if (key == "SPC") {
                version->set_spc(val);
            } else if (key == "BUILD_TIME") {
                version->set_build_time(val);
            }
        }

        // SMA_VERSION 前端显示替换为控制版本号 "主版本.u子版本"(如 2.11.1.u0001):
        // 主版本来自 VERSION 字段(缺失按默认值 0.0.0), 子版本来自 U_VERSION 字段(缺失按 0000)
        if (mainVersion.empty()) {
            mainVersion = "0.0.0";
        }
        if (subVersion.empty()) {
            subVersion = "0000";
        }
        version->set_sma_version(mainVersion + ".u" + subVersion);
        return true;
    }

    void DeviceCollector::TrimQuotes(std::string &s) {
        if (!s.empty() && (s.front() == '"' || s.front() == '\'')) {
            s = s.substr(1);
        }
        if (!s.empty() && (s.back() == '"' || s.back() == '\'')) {
            s.pop_back();
        }
    }

    bool DeviceCollector::CollectCpuInfo(SystemInfoItem &item) {
        auto &config = DeviceConfig::GetInstance();
        if (config.IsArm()) {
            return CollectCpuTemperatureArm(item);
        }
        return CollectCpuTemperatureX86(item);
    }

    float DeviceCollector::CollectCpuUtilization() {
        std::ifstream ifs("/proc/stat");
        if (!ifs.is_open()) {
            return 0.0F;
        }

        std::string label;
        CpuStat curr;
        ifs >> label >> curr.mUser >> curr.mNice >> curr.mSystem >> curr.mIdle >> curr.mIowait >> curr.mIrq >>
            curr.mSoftirq >> curr.mSteal;
        if (ifs.fail() || label != "cpu") {
            return 0.0F;
        }
        curr.mValid = true;

        float utilization = 0.0F;
        if (mPrevCpuStat.mValid) {
            uint64_t totalDelta = curr.Total() - mPrevCpuStat.Total();
            uint64_t idleDelta = curr.Idle() - mPrevCpuStat.Idle();
            if (totalDelta > 0) {
                utilization = static_cast<float>(totalDelta - idleDelta) / static_cast<float>(totalDelta) * 100.0F;
            }
        }

        mPrevCpuStat = curr;
        return utilization;
    }

    bool DeviceCollector::CollectCpuTemperatureArm(SystemInfoItem &item) {
        auto* cpu = item.mutable_cpu();

        std::string freqStr = ReadFileContent("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
        if (!freqStr.empty()) {
            try {
                cpu->set_speed(std::stof(freqStr) / 1000.0F);
            } catch (...) {
                SLOG_WARN << "Failed to parse CPU frequency on ARM";
            }
        }

        std::string tempStr = ReadFileContent("/sys/class/thermal/thermal_zone0/temp");
        if (!tempStr.empty()) {
            try {
                cpu->set_temperature(RoundTemp2Decimals(std::stof(tempStr) / 1000.0F));
            } catch (...) {
                SLOG_WARN << "Failed to parse CPU temperature on ARM";
            }
        }

        cpu->set_utilization(CollectCpuUtilization());
        return true;
    }

    bool DeviceCollector::CollectCpuTemperatureX86(SystemInfoItem &item) {
        auto* cpu = item.mutable_cpu();

        std::string freqStr = ReadFileContent("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
        if (!freqStr.empty()) {
            try {
                cpu->set_speed(std::stof(freqStr) / 1000.0F);
            } catch (...) {
                SLOG_WARN << "Failed to parse CPU frequency on x86";
            }
        }

        std::string tempStr = ReadFileContent("/sys/class/thermal/thermal_zone0/temp");
        if (!tempStr.empty()) {
            try {
                cpu->set_temperature(RoundTemp2Decimals(std::stof(tempStr) / 1000.0F));
            } catch (...) {
                SLOG_WARN << "Failed to parse CPU temperature from thermal_zone";
            }
        } else {
            CollectCpuTempFromSensors(*cpu);
        }

        cpu->set_utilization(CollectCpuUtilization());
        return true;
    }

    void DeviceCollector::CollectCpuTempFromSensors(CPUInfoItem &cpu) {
        std::string output = ExecCommand("sensors 2>/dev/null | grep -i 'core\\|temp' | head -1");
        auto pos = output.find('+');
        if (pos == std::string::npos) {
            return;
        }
        auto numEnd = output.find("°", pos);
        if (numEnd == std::string::npos) {
            return;
        }
        try {
            cpu.set_temperature(RoundTemp2Decimals(std::stof(output.substr(pos + 1, numEnd - pos - 1))));
        } catch (...) {
            SLOG_WARN << "Failed to parse sensors output";
        }
    }

    bool DeviceCollector::CollectMemoryInfo(SystemInfoItem &item) {
        auto* mem = item.mutable_memory();
        std::ifstream ifs("/proc/meminfo");
        if (!ifs.is_open()) {
            return false;
        }

        long memTotal = 0;
        long memAvailable = 0;
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.find("MemTotal:") == 0) {
                memTotal = std::strtol(line.c_str() + 10, nullptr, 10);
            } else if (line.find("MemAvailable:") == 0) {
                memAvailable = std::strtol(line.c_str() + 14, nullptr, 10);
            }
        }

        if (memTotal > 0) {
            mem->set_total(static_cast<float>(memTotal) / 1024.0F);
            mem->set_usage(static_cast<float>(memTotal - memAvailable) / 1024.0F);
        }
        return true;
    }

    bool DeviceCollector::CollectDiskInfo(const std::string &mountPath, SystemInfoItem &item) {
        auto diskSpace = DiskUtils::GetDiskSpace(mountPath);
        if (!diskSpace.mValid) {
            return false;
        }

        auto* disk = item.mutable_disk();
        disk->set_total(static_cast<float>(diskSpace.mTotalBytes) / (1024.0F * 1024.0F));
        disk->set_usage(static_cast<float>(diskSpace.mUsedBytes) / (1024.0F * 1024.0F));

        // auto ioStat = DiskUtils::GetDiskIOStat(mountPath);
        // if (ioStat.mValid) {
        //     disk->set_queue(ioStat.mQueue);
        //     disk->set_iops(ioStat.mIOPS);
        //     disk->set_io_latency(ioStat.mIOLatency);
        // } else {
        //     disk->set_queue(0.0F);
        //     disk->set_iops(0.0F);
        //     disk->set_io_latency(0.0F);
        // }
        return true;
    }

    bool DeviceCollector::CollectAcceleratorInfo(SystemInfoItem &item) {
        auto &config = DeviceConfig::GetInstance();
        if (config.IsArm()) {
            return CollectAcceleratorInfoArm(config.GetNpuBasicCmd(), config.GetNpuTempCmd(), item);
        }
        return CollectAcceleratorInfoX86(config.GetGpuCmd(), item);
    }

    bool DeviceCollector::CollectAcceleratorInfoArm(const std::string &basicCmd, const std::string &tempCmd,
                                                    SystemInfoItem &item) {
        auto* acc = item.mutable_accelerator();
        auto* npu = acc->mutable_npu();

        // // 使用 bm-smi 获取 NPU 利用率和时钟频率
        // if (!basicCmd.empty()) {
        //     std::string output = ExecCommand(basicCmd + " --noloop");
        //     ParseNpuSmiOutput(output, *npu);
        // }

        // 使用 GetTpuUsage() API 获取 NPU 信息（通过驱动层读取，更准确）
        // M50 可能有多块设备，需遍历所有设备求和后取平均
        int deviceCount = 1;
        try {
            auto dev = tcim::dev_ctrl::HalDeviceFactory::Create("Xh2HalBackend");
            if (dev) {
                deviceCount = dev->GetDeviceCount();
                if (deviceCount <= 0) {
                    deviceCount = 1;
                }
            }
        } catch (...) {
            deviceCount = 1;
        }

        uint64_t memTotalSum = 0;
        uint64_t memUsedSum = 0;
        uint64_t freqMHzSum = 0;
        for (int i = 0; i < deviceCount; ++i) {
            qifeng::lmshm::TpuInfo tpuInfo = qifeng::lmshm::HmQwenInfer::GetTpuUsage(i);
            memTotalSum += tpuInfo.mMemUsage.mTotal;
            memUsedSum += tpuInfo.mMemUsage.mUsed;
            freqMHzSum += tpuInfo.mFreqMHz;
        }

        if (memTotalSum > 0) {
            float memTotalMB = static_cast<float>(memTotalSum) / (1024.0F * 1024.0F);
            float memUsedMB = static_cast<float>(memUsedSum) / (1024.0F * 1024.0F);
            npu->set_memory_total(memTotalMB);
            npu->set_memory_used(memUsedMB);
            npu->set_memory_utilization(memUsedMB / memTotalMB * 100.0F);
        }
        // 频率为所有设备平均值，从驱动层 GetTpuUsage() 读取，换算为 GHz
        float avgFreqMHz = freqMHzSum / static_cast<float>(deviceCount);
        if (avgFreqMHz > 0.0F) {
            npu->set_gpu_currclk(avgFreqMHz / 1000000.0F);
        }

        if (!tempCmd.empty()) {
            std::string tempOutput = ExecCommand(tempCmd);
            ParseNpuTemperature(tempOutput, *npu);
        }

        return true;
    }

    static bool ParseNpuUtilization(const std::string &line, float &utilization) {
        if (line.find("| 0") == std::string::npos) {
            return false;
        }
        size_t lastPct = line.rfind('%');
        if (lastPct == std::string::npos) {
            return false;
        }
        size_t numStart = line.rfind(' ', lastPct);
        if (numStart == std::string::npos) {
            return false;
        }
        try {
            utilization = std::stof(line.substr(numStart + 1, lastPct - numStart - 1));
            return true;
        } catch (...) {
            return false;
        }
    }

    static bool ParseNpuMemoryLine(const std::string &line, float &memUsed, float &memTotal, float &memUtil) {
        if (line.find('/') == std::string::npos || line.find("MB") == std::string::npos) {
            return false;
        }
        size_t slashPos = line.rfind('/');
        if (slashPos == std::string::npos) {
            return false;
        }
        size_t mbAfterSlash = line.find("MB", slashPos);
        if (mbAfterSlash == std::string::npos) {
            return false;
        }
        try {
            std::string totalStr = line.substr(slashPos + 1, mbAfterSlash - slashPos - 1);
            memTotal = std::stof(totalStr);
        } catch (...) {
            return false;
        }
        size_t mbBeforeSlash = line.rfind("MB", slashPos);
        if (mbBeforeSlash == std::string::npos) {
            return false;
        }
        size_t usedStart = line.rfind(' ', mbBeforeSlash);
        if (usedStart == std::string::npos) {
            return false;
        }
        try {
            std::string usedStr = line.substr(usedStart + 1, mbBeforeSlash - usedStart - 1);
            memUsed = std::stof(usedStr);
        } catch (...) {
            return false;
        }
        if (memTotal > 0) {
            memUtil = memUsed / memTotal * 100.0F;
        }
        return true;
    }

    static bool ParseNpuCurrClock(const std::string &line, float &currClk) {
        if (line.find("Active") == std::string::npos) {
            return false;
        }
        size_t activePos = line.find("Active");
        size_t mPos = line.find('M', activePos);
        if (mPos == std::string::npos) {
            return false;
        }
        size_t numStart = line.rfind(' ', mPos);
        if (numStart == std::string::npos) {
            return false;
        }
        try {
            currClk = std::stof(line.substr(numStart + 1, mPos - numStart - 1));
            return true;
        } catch (...) {
            return false;
        }
    }

    void DeviceCollector::ParseNpuSmiOutput(const std::string &output, NPUInfoItem &npu) {
        if (output.empty()) {
            npu.set_npu_utilization(0.0F);
            npu.set_gpu_currclk(0.0F);
            npu.set_memory_utilization(0.0F);
            npu.set_memory_used(0.0F);
            npu.set_memory_total(0.0F);
            return;
        }

        std::istringstream iss(output);
        std::string line;
        float utilization = 0.0F;
        float currClk = 0.0F;
        float memUsed = 0.0F;
        float memTotal = 0.0F;
        float memUtil = 0.0F;
        bool foundUtil = false;
        bool foundClk = false;
        bool foundMem = false;

        while (std::getline(iss, line)) {
            if (!foundUtil && ParseNpuUtilization(line, utilization)) {
                foundUtil = true;
            }
            if (!foundClk && ParseNpuCurrClock(line, currClk)) {
                foundClk = true;
            }
            if (!foundMem && ParseNpuMemoryLine(line, memUsed, memTotal, memUtil)) {
                foundMem = true;
            }
            if (foundUtil && foundClk && foundMem) {
                break;
            }
        }

        npu.set_npu_utilization(utilization);
        npu.set_gpu_currclk(currClk);
        npu.set_memory_utilization(memUtil);
        npu.set_memory_used(memUsed);
        npu.set_memory_total(memTotal);
    }

    void DeviceCollector::ParseNpuTemperature(const std::string &output, NPUInfoItem &npu) {
        std::string tempStr = ReadFileContent("/sys/class/thermal/thermal_zone1/temp");
        if (!tempStr.empty()) {
            try {
                npu.set_temperature(RoundTemp2Decimals(std::stof(tempStr) / 1000.0F));
            } catch (...) {
                SLOG_WARN << "Failed to parse NPU temperature";
            }
        }
        (void)output;
        // auto pos = output.find("chip temperature:");
        // if (pos == std::string::npos) {
        //     return;
        // }
        // auto numStart = output.find_first_of("0123456789", pos);
        // if (numStart == std::string::npos) {
        //     return;
        // }
        // try {
        //     npu.set_temperature(std::stof(output.substr(numStart)));
        // } catch (...) {
        //     SLOG_WARN << "Failed to parse NPU temperature";
        // }
    }

    bool DeviceCollector::CollectAcceleratorInfoX86(const std::string &gpuCmd, SystemInfoItem &item) {
        auto* acc = item.mutable_accelerator();
        if (gpuCmd.empty()) {
            return true;
        }

        std::string queryCmd = gpuCmd + " --query-gpu=utilization.gpu,memory.utilization,memory.used,"
                                        "memory.total,temperature.gpu --format=csv,noheader,nounits 2>/dev/null";
        std::string output = ExecCommand(queryCmd);
        if (output.empty()) {
            return true;
        }

        auto* gpu = acc->add_gpus();
        ParseGpuCsvOutput(output, *gpu);
        return true;
    }

    void DeviceCollector::ParseGpuCsvOutput(const std::string &output, GPUInfoItem &gpu) {
        std::istringstream iss(output);
        std::string token;
        int idx = 0;
        while (std::getline(iss, token, ',')) {
            TrimWhitespace(token);
            if (token.empty()) {
                idx++;
                continue;
            }
            try {
                float val = std::stof(token);
                switch (idx) {
                    case 0:
                        gpu.set_gpu_utilization(val);
                        break;
                    case 1:
                        gpu.set_memory_utilization(val);
                        break;
                    case 2:
                        gpu.set_memory_used(val);
                        break;
                    case 3:
                        gpu.set_memory_total(val);
                        break;
                    case 4:
                        gpu.set_temperature(val);
                        break;
                    default:
                        break;
                }
            } catch (...) {
                SLOG_WARN << "Failed to parse GPU CSV field at index " << idx;
            }
            idx++;
        }
    }

    void DeviceCollector::TrimWhitespace(std::string &s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) {
            s.clear();
            return;
        }
        s = s.substr(start, end - start + 1);
    }

    bool DeviceCollector::CollectBatteryInfo(SystemInfoItem &item) {
        auto* bat = item.mutable_battery_info();
        bat->set_connect("2");
        bat->set_level("80");
        return true;
    }

    bool DeviceCollector::CollectConnectInfo(SystemInfoItem &item) {
        item.mutable_connect()->set_status("1");

        bool connected = HalBridge::GetInstance().HasMicrophone();
        auto* audio = item.mutable_audio();
        if (connected) {
            audio->set_connect("1");
            audio->set_status("normal");
            audio->set_msg("");
        } else {
            audio->set_connect("0");
            audio->set_status("normal");
            audio->set_msg("");
        }
        return true;
    }

    bool DeviceCollector::CollectSystemInfo(SystemInfoItem &item) {
        auto &config = DeviceConfig::GetInstance();
        item.set_timestamp(static_cast<int64_t>(GetTimeMs()) / 1000);
        item.set_device_id("device_1");
        item.set_arch(config.GetArchName());

        CollectConnectInfo(item);
        CollectVersionInfo(config.GetVersionConfigPath(), item);
        CollectCpuInfo(item);
        CollectMemoryInfo(item);
        CollectDiskInfo(config.GetDataDiskMountPath(), item);
        CollectAcceleratorInfo(item);
        CollectBatteryInfo(item);
        return true;
    }

}  // namespace qifeng_ca
