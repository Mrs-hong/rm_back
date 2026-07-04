/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/core/registry.h"

#include "checker/checkers/disk_checker.h"
#include "checker/checkers/display_checker.h"
#include "checker/checkers/fan_checker.h"
#include "checker/checkers/fingerprint_checker.h"
#include "checker/checkers/light_checker.h"
#include "checker/checkers/memory_checker.h"
#include "checker/checkers/microphone_checker.h"
#include "checker/checkers/model_inference_checker.h"
#include "checker/checkers/network_checker.h"
#include "checker/checkers/pcba_checker.h"
#include "checker/checkers/tpu_checker.h"

namespace qifeng::scm {

    /**
     * @brief 集中注册全部检查器
     * @details 新增检查项时在此处加一行 `reg.Register<XxxChecker>();`
     *          无需改动 core 层与 json_load。
     *          SDK 相关 checker 的注册受编译宏保护，无 SDK 时不参与注册，
     *          test_check 在沙箱中编译时自然不引入这些 checker 的实现。
     */
    void RegisterAll(CheckerRegistry &reg) {
        reg.Register<DiskChecker>();
        reg.Register<MemoryChecker>();
        reg.Register<NetworkChecker>();
#if defined(CHECKER_HAS_BM_SDK) && CHECKER_HAS_BM_SDK
        reg.Register<TpuChecker>();
        reg.Register<ModelInferenceChecker>();
        reg.Register<FanChecker>();
#endif
        reg.Register<DisplayChecker>();
        reg.Register<FingerprintChecker>();
#if defined(CHECKER_HAS_ALSA) && CHECKER_HAS_ALSA
        reg.Register<MicrophoneChecker>();
#endif
        reg.Register<LightChecker>();
        reg.Register<PcbaChecker>();
    }

}  // namespace qifeng::scm
