/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/memory_checker.h"

#include <sys/sysinfo.h>

#include <cstdio>
#include <fstream>
#include <string>

#include <array>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

namespace qifeng::scm {

namespace {

/**
 * @brief 从 /proc/meminfo 读取指定字段(KB)
 */
long long ReadMeminfoKb(const std::string &key) {
    std::ifstream f("/proc/meminfo");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind(key, 0) == 0) {
            long long v = 0;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg, cert-err34-c)
            if (sscanf(line.c_str() + key.size(), ": %lld kB", &v) == 1) {
                return v;
            }
        }
    }
    return -1;
}

}  // namespace

CheckResult MemoryChecker::Run(const Context &ctx) {
    CheckResult r(Name());
    auto t0 = GetTimeMs();

    struct sysinfo si{};
    bool sysOk = sysinfo(&si) == 0;

    long long totalKb = ReadMeminfoKb("MemTotal");
    long long availKb = ReadMeminfoKb("MemAvailable");
    if (totalKb < 0) {
        totalKb = sysOk ? (long long)(si.totalram >> 10) : 0;
    }
    if (availKb < 0) {
        availKb = sysOk ? (long long)(si.freeram >> 10) : 0;
    }

    std::array<char, 128> buf{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    snprintf(buf.data(), buf.size(), "total=%lldMB avail=%lldMB", totalKb >> 10, availKb >> 10);
    r.details.emplace_back("meminfo", buf.data());

    // 检测 ECC 信息（若 /sys/devices/system/edac/mc 存在）
    std::ifstream edac("/sys/devices/system/edac/mc/mc0/ce_count");
    if (edac) {
        int ce = 0;
        edac >> ce;
        r.details.emplace_back("ecc_correctable", std::to_string(ce));
    }

    long long needKb = (long long)ctx.config.memory.min_available_mb << 10;
    bool ok = (availKb >= needKb) && (totalKb > 0);
    r.status = ok ? Status::PASS : Status::FAIL;
    r.message = ok ? "memory ok" : "memory insufficient";
    r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
    SLOG_INFO << "[memory] " << r.message << " (" << r.elapsed_ms << "ms)";
    return r;
}

}  // namespace qifeng::scm