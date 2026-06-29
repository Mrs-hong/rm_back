// =============================================================================
// register_all.cpp — 集中注册所有内置检查器
//
// 扩展：新增检查器实现 IChecker 后，在此处添加一行 reg.add<YourChecker>();
// =============================================================================
#include "checker/core/registry.h"

#include "checker/checkers/disk_checker.h"
#include "checker/checkers/display_checker.h"
#include "checker/checkers/fingerprint_checker.h"
#include "checker/checkers/light_checker.h"
#include "checker/checkers/memory_checker.h"
#include "checker/checkers/microphone_checker.h"
#include "checker/checkers/model_inference_checker.h"
#include "checker/checkers/network_checker.h"
#include "checker/checkers/tpu_checker.h"

namespace checker {

void register_all(CheckerRegistry& reg) {
  reg.add<DiskChecker>();
  reg.add<MemoryChecker>();
  reg.add<NetworkChecker>();
  reg.add<TpuChecker>();
  reg.add<ModelInferenceChecker>();
  reg.add<DisplayChecker>();
  reg.add<FingerprintChecker>();
  reg.add<MicrophoneChecker>();
  reg.add<LightChecker>();
}

}  // namespace checker
