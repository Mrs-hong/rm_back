/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#ifndef QIFENG_FRAMEWORK_COMMON_RESOURCE_MONITOR_H
#define QIFENG_FRAMEWORK_COMMON_RESOURCE_MONITOR_H

#include <thread>
#include <unistd.h>

struct MemoryInfo {
    long long vmSizeKB {};
    long long vmRssKB {};
};

struct CpuInfo {
    long long uTime {};
    long long sTime {};
};

struct DiskInfo {
    long long readBytes {};
    long long writeBytes {};
};

class ResourceMonitor {
public:
    ResourceMonitor() = default;
    ~ResourceMonitor() = default;
    // 禁止拷贝
    ResourceMonitor(const ResourceMonitor&) = delete;
    ResourceMonitor(ResourceMonitor&&) noexcept = default;
    ResourceMonitor& operator=(const ResourceMonitor&) = delete;
    ResourceMonitor& operator=(ResourceMonitor&&) noexcept = default;
    long long GetClockTicks() {
        static long long Ticks = sysconf(_SC_CLK_TCK);
        return Ticks;
    }
    std::string GetProcessName() const;
    MemoryInfo CollectMemoryInfo();
    CpuInfo CollectCpuInfo();
    DiskInfo CollectDiskInfo();
    void ResourceMonitoringThread();
    void Run();

private:
    std::thread monitor;

    // 对应 Linux stat 第14个字段：用户态 CPU 时间
    static constexpr size_t ProcStatUtimeIndex = 13;
    // 对应 Linux stat 第15个字段：内核态 CPU 时间
    static constexpr size_t ProcStatStimeIndex = 14;
    // 至少需要 15 个字段才能安全访问 utime 和 stime
    static constexpr size_t ProcStatMinFieldCount = 15;
    // 二进制内存单位常量
    static constexpr double BinaryUnit = 1024.0;
    // TODO(检测时间间隔需要从配置文件读取)
    static constexpr double Interval = 1.0;
};

#endif  // QIFENG_FRAMEWORK_COMMON_RESOURCE_MONITOR_H