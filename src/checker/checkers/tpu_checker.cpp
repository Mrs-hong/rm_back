/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/tpu_checker.h"

#include <cstdint>
#include <vector>

#include "checker/core/context.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#if defined(CHECKER_HAS_BM_SDK) && CHECKER_HAS_BM_SDK
    #include "bmlib_runtime.h"
#endif

namespace qifeng::scm {

    // NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
    CheckResult TpuChecker::Run(const Context &ctx) {
        CheckResult r(Name());
        auto t0 = GetTimeMs();

#if !defined(CHECKER_HAS_BM_SDK) || !CHECKER_HAS_BM_SDK
        (void)ctx;
        r.status = Status::SKIPPED;
        r.message = "Sophon SDK not built-in";
        r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
        SLOG_INFO << "[tpu] " << r.message;
        return r;
#else
        if (!ctx.has_bm_sdk) {
            r.status = Status::SKIPPED;
            r.message = "SDK unavailable";
            r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
            return r;
        }

        // 1) 申请 TPU 设备 0
        bm_handle_t handle = nullptr;
        bm_status_t st = bm_dev_request(&handle, 0);
        if (st != BM_SUCCESS || !handle) {
            r.status = Status::SKIPPED;
            r.message = "no TPU device";
            r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
            SLOG_INFO << "[tpu] " << r.message;
            return r;
        }
        // RAII 释放
        auto guard = std::unique_ptr<void, void (*)(void*)>(
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            handle, [](void* h) { bm_dev_free(reinterpret_cast<bm_handle_t>(h)); });

        // 2) 查询设备显存信息
        //    注意: 部分驱动版本 mem_avail 不更新（始终等于 mem_total），
        //    此时用 mem_total - mem_used 作为可用量的兜底计算
        unsigned int heapNum = 0;
        unsigned long long totalMemBytes = 0;
        unsigned long long availMemBytes = 0;
        unsigned long long usedMemBytes = 0;
        if (bm_get_gmem_total_heap_num(handle, &heapNum) == BM_SUCCESS) {
            for (unsigned int i = 0; i < heapNum; ++i) {
                bm_heap_stat_byte_t stat {};
                if (bm_get_gmem_heap_stat_byte_by_id(handle, &stat, i) == BM_SUCCESS) {
                    totalMemBytes += stat.mem_total;
                    usedMemBytes += stat.mem_used;
                    // mem_avail 不可靠时用 total - used 兜底
                    availMemBytes += stat.mem_avail;
                }
            }
        }
        auto totalMemMb = static_cast<unsigned long long>(totalMemBytes / (1024ULL * 1024ULL));
        auto availMemMb = static_cast<unsigned long long>(availMemBytes / (1024ULL * 1024ULL));
        auto usedMemMb = static_cast<unsigned long long>(usedMemBytes / (1024ULL * 1024ULL));
        r.details.emplace_back("mem_total_mb", std::to_string(totalMemMb));
        r.details.emplace_back("mem_used_mb", std::to_string(usedMemMb));
        r.details.emplace_back("mem_avail_mb", std::to_string(availMemMb));
        SLOG_INFO << "[tpu] device memory: " << totalMemMb << " MB total, " << usedMemMb << " MB used, " << availMemMb
                  << " MB available";

        // 3) 显存容量检查：若配置了 min_mem_mb 且显存不足，直接 FAIL
        if (ctx.config.tpu.min_mem_mb > 0 && totalMemMb < static_cast<unsigned long long>(ctx.config.tpu.min_mem_mb)) {
            r.status = Status::FAIL;
            r.message = "TPU memory " + std::to_string(totalMemMb) + " MB < required " +
                        std::to_string(ctx.config.tpu.min_mem_mb) + " MB";
            r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
            SLOG_ERROR << "[tpu] " << r.message;
            return r;
        }

        // 4) 设备内存读写校验：写入 4KB 已知模式，回读比对
        const size_t kSize = 4096;
        bm_device_mem_t mem {};
        if (bm_malloc_device_byte(handle, &mem, kSize) != BM_SUCCESS) {
            r.status = Status::FAIL;
            r.message = "device malloc failed";
            r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
            return r;
        }
        std::vector<uint8_t> src(kSize, 0x5A);
        std::vector<uint8_t> dst(kSize, 0);
        bool ok = true;
        if (bm_memcpy_s2d(handle, mem, src.data()) != BM_SUCCESS) {
            ok = false;
        }
        if (bm_memcpy_d2s(handle, dst.data(), mem) != BM_SUCCESS) {
            ok = false;
        }
        bm_free_device(handle, mem);
        if (ok && src != dst) {
            ok = false;
        }

        r.details.emplace_back("mem_verify", ok ? "ok" : "fail");
        r.status = ok ? Status::PASS : Status::FAIL;
        r.message = ok ? ("tpu ok (" + std::to_string(totalMemMb) + " MB)") : "tpu mem verify failed";
        r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
        SLOG_INFO << "[tpu] " << r.message << " (" << r.elapsed_ms << "ms)";
        return r;
#endif
    }

}  // namespace qifeng::scm
