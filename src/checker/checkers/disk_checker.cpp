/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/disk_checker.h"

#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include <array>

#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    namespace {

        // io_test 三态结果
        enum IoResult {
            kIoOk = 0,        // 读写校验通过
            kIoReadOnly = 1,  // 只读文件系统(EROFS)，非临界
            kIoFail = 2       // 真实 IO 故障
        };

        /**
         * @brief 在挂载点做 4KB 读写校验。使用 POSIX API 以可靠捕获 errno。
         */
        IoResult IoTest(const std::string &mount_point) {
            std::string path = mount_point + "/.bm1684_selftest_io";
            std::string payload(4096, 'A');

            // 写入
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            if (fd < 0) {
                if (errno == EROFS) {
                    return kIoReadOnly;
                }
                return kIoFail;
            }
            ssize_t wn = ::write(fd, payload.data(), payload.size());
            int werr = errno;
            ::close(fd);
            if (wn != (ssize_t)payload.size()) {
                ::unlink(path.c_str());
                return (werr == EROFS) ? kIoReadOnly : kIoFail;
            }

            // 读回校验
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                ::unlink(path.c_str());
                return kIoFail;
            }
            std::string back(4096, '\0');
            ssize_t rn = ::read(fd, &back[0], 4096);
            ::close(fd);
            ::unlink(path.c_str());
            if (rn != (ssize_t)payload.size()) {
                return kIoFail;
            }
            return (back == payload) ? kIoOk : kIoFail;
        }

    }  // namespace

    void DiskChecker::ParseConfig(const Json::Value &j, DiskConfig &cfg) {
        cfg.min_free_pct = j.isMember("min_free_pct") && j["min_free_pct"].isInt()
                               ? j["min_free_pct"].asInt()
                               : cfg.min_free_pct;
        if (j.isMember("mounts") && j["mounts"].isArray()) {
            cfg.mounts.clear();
            for (Json::ArrayIndex i = 0; i < j["mounts"].size(); ++i) {
                const Json::Value &v = j["mounts"][i];
                if (v.isString()) {
                    cfg.mounts.push_back(v.asString());
                }
            }
        }
    }

    CheckResult DiskChecker::Run() {
        CheckResult r(Name());
        auto t0 = std::chrono::steady_clock::now();

        bool ok = true;
        for (const auto &mp : mConfig.mounts) {
            struct statvfs vfs{};
            if (statvfs(mp.c_str(), &vfs) != 0) {
                // 挂载点不存在：x86 上 BM1684 专用分区缺失，非临界
                if (errno == ENOENT || errno == ENOTDIR) {
                    r.details.emplace_back(mp, "not_mounted");
                } else {
                    r.details.emplace_back(mp, std::string("statvfs failed: ") + ::strerror(errno));
                    ok = false;
                }
                continue;
            }
            unsigned long total = vfs.f_blocks * vfs.f_frsize;
            unsigned long avail = vfs.f_bavail * vfs.f_frsize;
            int freePct = total ? (int)(avail * 100ULL / total) : 0;
            std::array<char, 128> buf{};
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            snprintf(buf.data(), buf.size(), "total=%luMB avail=%luMB free=%d%%", total >> 20, avail >> 20,
                     freePct);
            std::string detail(buf.data());

            // 读写校验：只读 fs 记 ro(非临界)，真实故障记 fail(临界)
            IoResult io = IoTest(mp);
            switch (io) {
                case kIoOk:
                    detail += " io=ok";
                    break;
                case kIoReadOnly:
                    detail += " io=ro";
                    break;
                case kIoFail:
                    detail += " io=fail";
                    ok = false;
                    break;
                default:
                    detail += " io=unknown";
                    ok = false;
                    break;
            }
            if (freePct < mConfig.min_free_pct) {
                detail += " LOW_SPACE";
                ok = false;
            }
            r.details.emplace_back(mp, detail);
        }

        r.status = ok ? Status::PASS : Status::FAIL;
        r.message = ok ? "disk ok" : "disk check failed";
        r.elapsed_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
        SLOG_INFO << "[disk] " << r.message << " (" << r.elapsed_ms << "ms)";
        return r;
    }

}  // namespace qifeng::scm
