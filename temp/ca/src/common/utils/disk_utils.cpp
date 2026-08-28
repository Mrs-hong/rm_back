#include <chrono>
#include <fstream>
#include <sstream>
#include <sys/statvfs.h>
#include <unordered_map>

#include "qifeng_framework/common/logger.h"

#include "common/utils/disk_utils.h"

namespace qifeng_ca {

    DiskSpaceInfo DiskUtils::GetDiskSpace(const std::string &path) {
        DiskSpaceInfo info;
        struct statvfs vfs {};
        if (statvfs(path.c_str(), &vfs) != 0) {
            SLOG_ERROR << "DiskMonitor: statvfs failed, path=" << path;
            return info;
        }

        info.mTotalBytes = static_cast<uint64_t>(vfs.f_blocks) * static_cast<uint64_t>(vfs.f_frsize);
        info.mAvailBytes = static_cast<uint64_t>(vfs.f_bavail) * static_cast<uint64_t>(vfs.f_frsize);
        info.mUsedBytes = info.mTotalBytes - info.mAvailBytes;
        // 已用空间 = 总块数 - 总空闲块数(f_bfree), 与 df 命令接近一致(不含保留块偏移)
        // info.mUsedBytes = info.mTotalBytes - static_cast<uint64_t>(vfs.f_bfree) *
        // static_cast<uint64_t>(vfs.f_frsize);

        if (info.mTotalBytes > 0) {
            info.mUsagePercent = static_cast<int>((info.mUsedBytes * 100) / info.mTotalBytes);
        }
        SLOG_INFO << "TotalBytes=" << info.mTotalBytes << ", AvailBytes=" << info.mAvailBytes
                  << ", UsedBytes=" << info.mUsedBytes << ", mUsagePercent=" << info.mUsagePercent;
        info.mValid = true;
        return info;
    }

    namespace {
        // /proc/diskstats 字段索引
        constexpr int IdxReadsCompleted = 3;
        constexpr int IdxReadsMerged = 4;
        constexpr int IdxSectorsRead = 5;
        constexpr int IdxMsReading = 6;
        constexpr int IdxWritesCompleted = 7;
        constexpr int IdxWritesMerged = 8;
        constexpr int IdxSectorsWritten = 9;
        constexpr int IdxMsWriting = 10;
        constexpr int IdxIosInProgress = 11;
        constexpr int IdxMsIo = 12;
        constexpr int IdxMsIoWeighted = 13;

        struct DiskIOCounters {
            uint64_t readsCompleted {0};
            uint64_t writesCompleted {0};
            uint64_t msReading {0};
            uint64_t msWriting {0};
            uint64_t iosInProgress {0};
            std::chrono::steady_clock::time_point sampleTime;
        };

        std::unordered_map<std::string, DiskIOCounters> gPrevCounters;  // NOLINT
        std::mutex gIOMutex;                                            // NOLINT

        // 从 /proc/mounts 根据挂载路径获取设备名(如 sda, mmcblk0)
        std::string GetDeviceName(const std::string &mountPath) {
            std::ifstream mounts("/proc/mounts");
            if (!mounts.is_open()) {
                return {};
            }
            std::string line;
            while (std::getline(mounts, line)) {
                std::istringstream iss(line);
                std::string device;
                std::string mount;
                std::string fsType;
                iss >> device >> mount >> fsType;
                if (mount == mountPath) {
                    // 从 /dev/sda1 提取 sda, 从 /dev/mmcblk0p7 提取 mmcblk0
                    auto pos = device.find_last_of('/');
                    std::string name = (pos != std::string::npos) ? device.substr(pos + 1) : device;
                    // 去掉末尾数字分区号(sda1->sda)
                    while (!name.empty() && std::isdigit(name.back())) {
                        name.pop_back();
                    }
                    // 去掉 eMMC/NVMe 分区分隔符 p (mmcblk0p->mmcblk0, nvme0n1p->nvme0n1)
                    if (!name.empty() && name.back() == 'p') {
                        name.pop_back();
                    }
                    return name;
                }
            }
            return {};
        }

        // 解析 /proc/diskstats 中指定设备的IO计数器
        bool ParseDiskstats(const std::string &deviceName, DiskIOCounters &counters) {
            std::ifstream diskstats("/proc/diskstats");
            if (!diskstats.is_open()) {
                return false;
            }
            std::string line;
            while (std::getline(diskstats, line)) {
                std::istringstream iss(line);
                std::vector<std::string> fields;
                std::string field;
                while (iss >> field) {
                    fields.push_back(field);
                }
                if (fields.size() <= static_cast<size_t>(IdxMsIoWeighted)) {
                    continue;
                }
                if (fields[2] == deviceName) {
                    counters.readsCompleted = std::stoull(fields[IdxReadsCompleted]);
                    counters.writesCompleted = std::stoull(fields[IdxWritesCompleted]);
                    counters.msReading = std::stoull(fields[IdxMsReading]);
                    counters.msWriting = std::stoull(fields[IdxMsWriting]);
                    counters.iosInProgress = std::stoull(fields[IdxIosInProgress]);
                    counters.sampleTime = std::chrono::steady_clock::now();
                    // SLOG_DEBUG << "DiskUtils: readsCompleted=" << counters.readsCompleted
                    //            << ", writesCompleted=" << counters.writesCompleted
                    //            << ", msReading=" << counters.msReading << ", msWriting=" << counters.msWriting
                    //            << ", iosInProgress=" << counters.iosInProgress;
                    return true;
                }
            }
            return false;
        }
    }  // namespace

    DiskIOStat DiskUtils::GetDiskIOStat(const std::string &path) {
        DiskIOStat stat;

        std::string deviceName = GetDeviceName(path);
        if (deviceName.empty()) {
            return stat;
        }

        DiskIOCounters current;
        if (!ParseDiskstats(deviceName, current)) {
            return stat;
        }

        // 队列深度: 瞬时值
        stat.mQueue = static_cast<float>(current.iosInProgress);

        // 平均IO延迟: 累计读写耗时 / 累计读写次数
        uint64_t totalIOs = current.readsCompleted + current.writesCompleted;
        uint64_t totalMs = current.msReading + current.msWriting;
        if (totalIOs > 0) {
            stat.mIOLatency = static_cast<float>(totalMs) / static_cast<float>(totalIOs);
        }

        // IOPS: 通过两次采样的差值计算
        {
            std::lock_guard<std::mutex> lock(gIOMutex);
            auto it = gPrevCounters.find(deviceName);
            if (it != gPrevCounters.end()) {
                auto elapsedMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(current.sampleTime - it->second.sampleTime)
                        .count();
                uint64_t deltaIOs = 0;
                if (elapsedMs > 0) {
                    deltaIOs = (current.readsCompleted + current.writesCompleted) -
                               (it->second.readsCompleted + it->second.writesCompleted);
                    stat.mIOPS = static_cast<float>(deltaIOs) / (static_cast<float>(elapsedMs) / 1000.0F);
                }
                // SLOG_INFO << "DiskUtils: elapsedMs=" << elapsedMs << ", deltaIOs=" << deltaIOs
                //           << ", stat.mIOPS=" << stat.mIOPS;
            }
            gPrevCounters[deviceName] = current;
        }

        stat.mValid = true;
        // SLOG_DEBUG << "DiskUtils: device=" << deviceName << ", queue=" << stat.mQueue << ", iops=" << stat.mIOPS
        //            << ", latency=" << stat.mIOLatency;
        return stat;
    }
}  // namespace qifeng_ca
