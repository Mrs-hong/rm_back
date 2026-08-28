/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "common/logger.h"
#include "common/resource_monitor.h"

std::string ResourceMonitor::GetProcessName() const {
    std::ifstream file("/proc/self/comm");
    if (!file.is_open()) {
        SLOG_ERROR << "Failed to open /proc/self/comm";
        return "unknown";
    }
    std::string processName;
    if (std::getline(file, processName) && !processName.empty()) {
        return processName;
    }
    return "unknown";
}

MemoryInfo ResourceMonitor::CollectMemoryInfo() {
    std::ifstream file("/proc/self/status");
    if (!file.is_open()) {
        SLOG_ERROR << "Failed to open /proc/self/status";
        return MemoryInfo {};
    }
    std::string line;

    MemoryInfo info;
    while (std::getline(file, line)) {
        if (line.rfind("VmSize:", 0) == 0) {
            std::istringstream(line.substr(7)) >> info.vmSizeKB;
        } else if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream(line.substr(6)) >> info.vmRssKB;
        }
    }

    return info;
}

CpuInfo ResourceMonitor::CollectCpuInfo() {
    std::ifstream file("/proc/self/stat");
    if (!file.is_open()) {
        SLOG_ERROR << "Failed to open /proc/self/stat";
        return CpuInfo {};
    }
    std::string line;
    std::getline(file, line);

    std::istringstream iss(line);
    std::vector<std::string> stats;
    std::string s;
    while (iss >> s) {
        stats.push_back(s);
    }

    CpuInfo info;
    if (stats.size() > ProcStatMinFieldCount) {
        info.uTime = std::stoll(stats[ProcStatUtimeIndex]);
        info.sTime = std::stoll(stats[ProcStatStimeIndex]);
    } else {
        SLOG_ERROR << "Unexpected format in /proc/self/stat, expected at least " << ProcStatMinFieldCount
                   << " fields but got " << stats.size();
    }

    return info;
}

DiskInfo ResourceMonitor::CollectDiskInfo() {
    std::ifstream file("/proc/self/io");
    if (!file.is_open()) {
        SLOG_ERROR << "Failed to open /proc/self/io";
        return DiskInfo {};
    }
    std::string line;
    DiskInfo info;
    while (std::getline(file, line)) {
        if (line.rfind("read_bytes:", 0) == 0) {
            std::istringstream(line.substr(11)) >> info.readBytes;
        } else if (line.rfind("write_bytes:", 0) == 0) {
            std::istringstream(line.substr(12)) >> info.writeBytes;
        }
    }

    return info;
}

void ResourceMonitor::ResourceMonitoringThread() {
    const pid_t processId = getpid();
    const std::string processName = GetProcessName();

    CpuInfo prevCpu = CollectCpuInfo();
    DiskInfo prevDisk = CollectDiskInfo();
    long long clk = GetClockTicks();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        MemoryInfo mem = CollectMemoryInfo();
        CpuInfo cpu = CollectCpuInfo();
        DiskInfo disk = CollectDiskInfo();

        long long cpuDelta = (cpu.uTime + cpu.sTime) - (prevCpu.uTime + prevCpu.sTime);
        double cpuPercent = ((double)cpuDelta / (double)clk) / Interval * 100.0;
        long long readDelta = disk.readBytes - prevDisk.readBytes;
        long long writeDelta = disk.writeBytes - prevDisk.writeBytes;

        double readKB = (double)readDelta / BinaryUnit;
        double writeKB = (double)writeDelta / BinaryUnit;

        SLOG_INFO << "Resource Usage | "
                  << "PName=" << processName << " "
                  << "PID=" << processId << " | "
                  << "CPU: " << cpuPercent << "% | "
                  << "RSS=" << (double)mem.vmRssKB / BinaryUnit << "MB | "
                  << "VSZ=" << (double)mem.vmSizeKB / BinaryUnit << "MB | "
                  << "Disk: R=" << readKB << "KB/s "
                  << "W=" << writeKB << "KB/s";

        prevCpu = cpu;
        prevDisk = disk;
    }
}

void ResourceMonitor::Run() {
    monitor = std::thread(&ResourceMonitor::ResourceMonitoringThread, this);
    monitor.detach();
}
