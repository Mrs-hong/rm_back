// =============================================================================
// disk_checker.cpp — 磁盘自检
//
// 检查每个配置挂载点：容量(statvfs) + 4KB 读写校验。
// 区分三种非临界/临界状态：
//   - 只读文件系统(EROFS)：非临界，记 io=ro
//   - 挂载点不存在(ENOENT/ENOTDIR)：非临界，记 not_mounted
//   - 真实 IO 故障：临界，记 io=fail → disk=kFail
// =============================================================================
#include "checker/checkers/disk_checker.h"

#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "checker/log/logger.hpp"
#include "checker/util/time_util.h"

namespace checker {

namespace {

// io_test 三态结果
enum IoResult {
  kIoOk = 0,        // 读写校验通过
  kIoReadOnly = 1,  // 只读文件系统(EROFS)，非临界
  kIoFail = 2       // 真实 IO 故障
};

// 在挂载点做 4KB 读写校验。使用 POSIX API 以可靠捕获 errno。
IoResult io_test(const std::string& mount_point) {
  std::string path = mount_point + "/.bm1684_selftest_io";
  std::string payload(4096, 'A');

  // 写入
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    // EROFS: 只读文件系统；EACCES 也常源于只读挂载
    if (errno == EROFS) return kIoReadOnly;
    return kIoFail;
  }
  ssize_t wn = ::write(fd, payload.data(), payload.size());
  int werr = errno;  // 保存 write 可能设置的 errno
  ::close(fd);
  if (wn != (ssize_t)payload.size()) {
    ::unlink(path.c_str());
    return (werr == EROFS) ? kIoReadOnly : kIoFail;
  }

  // 读回校验
  fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    ::unlink(path.c_str());
    return kIoFail;
  }
  std::string back(4096, '\0');
  ssize_t rn = ::read(fd, &back[0], 4096);
  ::close(fd);
  ::unlink(path.c_str());
  if (rn != (ssize_t)payload.size()) return kIoFail;
  return (back == payload) ? kIoOk : kIoFail;
}

}  // namespace

CheckResult DiskChecker::run(const Context& ctx) {
  CheckResult r(name());
  Timer timer;

  bool ok = true;
  for (const auto& mp : ctx.config.disk.mounts) {
    struct statvfs vfs;
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
    int free_pct = total ? (int)(avail * 100ULL / total) : 0;
    char buf[128];
    snprintf(buf, sizeof(buf), "total=%luMB avail=%luMB free=%d%%", total >> 20, avail >> 20,
             free_pct);
    std::string detail = buf;

    // 读写校验：只读 fs 记 ro(非临界)，真实故障记 fail(临界)
    IoResult io = io_test(mp);
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
    }
    if (free_pct < ctx.config.disk.min_free_pct) {
      detail += " LOW_SPACE";
      ok = false;
    }
    r.details.emplace_back(mp, detail);
  }

  r.status = ok ? Status::kPass : Status::kFail;
  r.message = ok ? "disk ok" : "disk check failed";
  r.elapsed_ms = timer.elapsed_ms();
  LOG_INFO("[disk] %s (%dms)", r.message.c_str(), r.elapsed_ms);
  return r;
}

}  // namespace checker
