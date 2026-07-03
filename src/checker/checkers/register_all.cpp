/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/core/registry.h"

#include "checker/checkers/by_script/pcba_cheker.h"
#include "checker/checkers/disk_checker.h"
#include "checker/checkers/display_checker.h"
#include "checker/checkers/fan_checker.h"
#include "checker/checkers/fingerprint_checker.h"
#include "checker/checkers/light_checker.h"
#include "checker/checkers/memory_checker.h"
#include "checker/checkers/microphone_checker.h"
#include "checker/checkers/model_inference_checker.h"
#include "checker/checkers/network_checker.h"
#include "checker/checkers/tpu_checker.h"

namespace qifeng::scm {

void RegisterAll(CheckerRegistry &reg) {
    reg.Add<DiskChecker>();
    reg.Add<MemoryChecker>();
    reg.Add<NetworkChecker>();
    reg.Add<TpuChecker>();
    reg.Add<ModelInferenceChecker>();
    reg.Add<FanChecker>();
    reg.Add<DisplayChecker>();
    reg.Add<FingerprintChecker>();
    reg.Add<MicrophoneChecker>();
    reg.Add<LightChecker>();
    reg.Add<PcbaChecker>();
}

}  // namespace qifeng::scm