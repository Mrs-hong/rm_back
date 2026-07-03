/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/model_inference_checker.h"

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "checker/core/context.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#if defined(CHECKER_HAS_BM_SDK) && CHECKER_HAS_BM_SDK
    #include "bmlib_runtime.h"
    #include "bmruntime_interface.h"
#endif

namespace qifeng::scm {

#if defined(CHECKER_HAS_BM_SDK) && CHECKER_HAS_BM_SDK
    namespace {

        bool FileExists(const std::string &path) {
            std::ifstream f(path, std::ios::binary);
            return f.good();
        }

        size_t ShapeNumElements(const bm_shape_t &shape) {
            size_t n = 1;
            for (int i = 0; i < shape.num_dims; ++i) {
                n *= static_cast<size_t>(shape.dims[i]);
            }
            return n;
        }

        size_t DtypeSizeBytes(bm_data_type_t dtype) {
            switch (dtype) {
                case BM_FLOAT32:  return 4;
                case BM_FLOAT16:  return 2;
                case BM_INT8:     return 1;
                case BM_UINT8:    return 1;
                case BM_INT16:    return 2;
                case BM_UINT16:   return 2;
                case BM_INT32:    return 4;
                case BM_UINT32:   return 4;
                case BM_BFLOAT16: return 2;
                case BM_INT4:     return 1;
                case BM_UINT4:    return 1;
                default:          return 4;
            }
        }

    }  // namespace
#endif

                // NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
    CheckResult ModelInferenceChecker::Run(const Context &ctx) {
        CheckResult r(Name());
        auto t0 = GetTimeMs();

#if !defined(CHECKER_HAS_BM_SDK) || !CHECKER_HAS_BM_SDK
        (void)ctx;
        r.status = Status::SKIPPED;
        r.message = "Sophon SDK not built-in";
        r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
        SLOG_INFO << "[model_inference] " << r.message;
        return r;
#else
        const std::string &modelPath = ctx.config.model.path;

        // 1) 模型文件是否部署
        if (!FileExists(modelPath)) {
            r.status = Status::SKIPPED;
            r.message = "probe model not deployed: " + modelPath;
            r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
            SLOG_INFO << "[model_inference] " << r.message;
            return r;
        }

        // 2) 申请 TPU 设备
        bm_handle_t handle = nullptr;
        if (bm_dev_request(&handle, 0) != BM_SUCCESS || !handle) {
            r.status = Status::SKIPPED;
            r.message = "no TPU device";
            r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
            SLOG_INFO << "[model_inference] " << r.message;
            return r;
        }
        auto hguard = std::unique_ptr<void, void (*)(void*)>(
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            handle, [](void* h) { bm_dev_free(reinterpret_cast<bm_handle_t>(h)); });

        // 3) 创建 runtime 并加载 bmodel
        void* rt = bmrt_create(handle);
        if (!rt) {
            r.status = Status::FAIL;
            r.message = "bmrt_create failed";
            r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
            return r;
        }
        auto rtguard = std::unique_ptr<void, void (*)(void*)>(rt, [](void* p) { bmrt_destroy(p); });

        if (!bmrt_load_bmodel(rt, modelPath.c_str())) {
            r.status = Status::FAIL;
            r.message = "load bmodel failed";
            r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
            SLOG_ERROR << "[model_inference] load bmodel failed: " << modelPath;
            return r;
        }

        // 4) 获取网络名称列表
        //    签名: void bmrt_get_network_names(void* p_bmrt, const char*** network_names)
        //    调用后需调用方 free(network_names)
        const char** networkNames = nullptr;
        bmrt_get_network_names(rt, &networkNames);
        int num = bmrt_get_network_number(rt);
        r.details.emplace_back("networks", std::to_string(num));
        if (num <= 0 || !networkNames) {
            r.status = Status::FAIL;
            r.message = "no network in bmodel";
            r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
            free(networkNames);
            return r;
        }

        std::string firstName = (networkNames[0]) ? networkNames[0] : "unknown";
        r.details.emplace_back("first_network", firstName);

        // 5) 获取网络信息（shape 在 stages[0] 中）
        const bm_net_info_t* netInfo = bmrt_get_network_info(rt, firstName.c_str());
        if (!netInfo) {
            r.status = Status::PASS;
            r.message = "model loaded: " + firstName + " (no net info, skip inference)";
            r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
            SLOG_WARN << "[model_inference] bmrt_get_network_info returned null for: " << firstName;
            free(networkNames);
            return r;
        }

        // 6) 使用 bmrt_launch_data 推理（runtime 自动管理设备内存分配/拷贝）
        //    签名: bool bmrt_launch_data(void* p_bmrt, const char* net_name,
        //              void* const input_datas[], const bm_shape_t input_shapes[], int input_num,
        //              void* output_datas[], bm_shape_t output_shapes[], int output_num,
        //              bool user_mem);
        int inputNum = netInfo->input_num;
        int outputNum = netInfo->output_num;
        int stageIdx = 0;

        // 准备输入缓冲区：零填充
        std::vector<std::vector<uint8_t>> inputBuffers(static_cast<size_t>(inputNum));
        std::vector<void*> inputDataPtrs(static_cast<size_t>(inputNum), nullptr);
        std::vector<bm_shape_t> inputShapes(static_cast<size_t>(inputNum));
        for (int i = 0; i < inputNum; ++i) {
            auto idx = static_cast<size_t>(i);
            inputShapes[idx] = netInfo->stages[stageIdx].input_shapes[idx];
            size_t elemCount = ShapeNumElements(inputShapes[idx]);
            size_t typeSize = DtypeSizeBytes(netInfo->input_dtypes[idx]);
            inputBuffers[idx].resize(elemCount * typeSize, 0);
            inputDataPtrs[idx] = inputBuffers[idx].data();
        }

        // 准备输出缓冲区
        std::vector<std::vector<uint8_t>> outputBuffers(static_cast<size_t>(outputNum));
        std::vector<void*> outputDataPtrs(static_cast<size_t>(outputNum), nullptr);
        std::vector<bm_shape_t> outputShapes(static_cast<size_t>(outputNum));
        for (int i = 0; i < outputNum; ++i) {
            auto idx = static_cast<size_t>(i);
            outputShapes[idx] = netInfo->stages[stageIdx].output_shapes[idx];
            size_t elemCount = ShapeNumElements(outputShapes[idx]);
            size_t typeSize = DtypeSizeBytes(netInfo->output_dtypes[idx]);
            outputBuffers[idx].resize(elemCount * typeSize, 0);
            outputDataPtrs[idx] = outputBuffers[idx].data();
        }

        // 7) 执行推理
        bool launched = bmrt_launch_data(rt, firstName.c_str(),
                                          inputDataPtrs.data(), inputShapes.data(), inputNum,
                                          outputDataPtrs.data(), outputShapes.data(), outputNum,
                                          false);  // user_mem=false
        int syncRet = -1;
        if (launched) {
            syncRet = bm_thread_sync(handle);
        }

        r.details.emplace_back("launch", launched ? "ok" : "fail");
        r.details.emplace_back("sync", std::to_string(syncRet));

        bool ok = launched && (syncRet == 0);
        r.status = ok ? Status::PASS : Status::FAIL;
        r.message = ok ? ("inference ok: " + firstName) : "inference check failed";
        r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
        SLOG_INFO << "[model_inference] " << r.message << " (" << r.elapsed_ms << "ms)";

        free(networkNames);
        return r;
#endif
    }

}  // namespace qifeng::scm
