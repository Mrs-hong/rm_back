// =============================================================================
// tpu_checker.cpp — TPU 自检（Sophon SDK bmlib C API）
//
// 申请设备 0，做设备内存 s2d/d2s 读写校验。无 SDK 或无设备时 Skipped。
// =============================================================================
#include "checker/checkers/tpu_checker.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "checker/log/logger.hpp"
#include "checker/util/time_util.h"

#if defined(CHECKER_HAS_BM_SDK) && CHECKER_HAS_BM_SDK
#include "bmlib_runtime.h"
#endif

namespace checker {

CheckResult TpuChecker::run(const Context& ctx) {
  CheckResult r(name());
  Timer timer;

#if !defined(CHECKER_HAS_BM_SDK) || !CHECKER_HAS_BM_SDK
  (void)ctx;
  r.status = Status::kSkipped;
  r.message = "Sophon SDK not built-in";
  r.elapsed_ms = timer.elapsed_ms();
  LOG_INFO("[tpu] %s", r.message.c_str());
  return r;
#else
  if (!ctx.has_bm_sdk) {
    r.status = Status::kSkipped;
    r.message = "SDK unavailable";
    r.elapsed_ms = timer.elapsed_ms();
    return r;
  }

  // 1) 申请 TPU 设备 0
  bm_handle_t handle = nullptr;
  bm_status_t st = bm_dev_request(&handle, 0);
  if (st != BM_SUCCESS || !handle) {
    // x86 无设备：跳过（非故障）
    r.status = Status::kSkipped;
    r.message = "no TPU device";
    r.elapsed_ms = timer.elapsed_ms();
    LOG_INFO("[tpu] %s", r.message.c_str());
    return r;
  }
  // RAII 释放
  auto guard = std::unique_ptr<void, void (*)(void*)>(
      handle, [](void* h) { bm_dev_free(reinterpret_cast<bm_handle_t>(h)); });

  // 2) 设备内存读写校验：写入 4KB 已知模式，回读比对
  const size_t kSize = 4096;
  bm_device_mem_t mem{};
  if (bm_malloc_device_byte(handle, &mem, kSize) != BM_SUCCESS) {
    r.status = Status::kFail;
    r.message = "device malloc failed";
    r.elapsed_ms = timer.elapsed_ms();
    return r;
  }
  std::vector<uint8_t> src(kSize, 0x5A);
  std::vector<uint8_t> dst(kSize, 0);
  bool ok = true;
  if (bm_memcpy_s2d(handle, mem, src.data()) != BM_SUCCESS) ok = false;
  if (bm_memcpy_d2s(handle, dst.data(), mem) != BM_SUCCESS) ok = false;
  bm_free_device(handle, mem);
  if (ok && src != dst) ok = false;

  r.details.emplace_back("mem_verify", ok ? "ok" : "fail");
  r.status = ok ? Status::kPass : Status::kFail;
  r.message = ok ? "tpu ok" : "tpu mem verify failed";
  r.elapsed_ms = timer.elapsed_ms();
  LOG_INFO("[tpu] %s (%dms)", r.message.c_str(), r.elapsed_ms);
  return r;
#endif
}

}  // namespace checker
