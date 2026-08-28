//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>

#include "qifeng_framework/common/logger.h"

#include "common/status.h"
#include "core/system/reset_task.h"
#include "internal/hal/fingerprint_bridge.h"

namespace qifeng_ca {

    namespace {

        // 指纹ID范围: 1 ~ MaxFingerId
        constexpr uint16_t MaxFingerId = 100;
        constexpr uint32_t FingerDeleteTimeoutMs = 2000;

    }  // namespace

    // 删除设备上1-100号指纹模板
    class DeleteFingerprintsResetTask final : public ResetTask {
    public:
        std::string_view Name() const override { return "DeleteFingerprints"; }

        Status Execute(uint64_t operatorAccountId) override {
            (void)operatorAccountId;
            auto &bridge = FingerprintBridge::GetInstance();
            int deleted = 0;
            for (uint16_t fid = 1; fid <= MaxFingerId; ++fid) {
                auto ret = bridge.Delete(fid, FingerDeleteTimeoutMs);
                if (ret == FingerprintResult::OK) {
                    ++deleted;
                }
            }
            SLOG_INFO << "Reset: deleted " << deleted << " fingerprints from device";
            return Status {};
        }
    };

    std::unique_ptr<ResetTask> CreateDeleteFingerprintsResetTask() {
        return std::make_unique<DeleteFingerprintsResetTask>();
    }

}  // namespace qifeng_ca
