//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INTERNAL_HAL_FINGERPRINT_MEETING_CONTROLLER_H
#define QIFENG_CA_INTERNAL_HAL_FINGERPRINT_MEETING_CONTROLLER_H

#include <cstdint>
#include <mutex>

namespace qifeng_ca {
    namespace hal {

        // 指纹会议控制器: 管理指纹触发的会议开始/停止业务逻辑(权限等错误控制)
        class FingerprintMeetingController {
        public:
            static FingerprintMeetingController &GetInstance();

            // 处理指纹识别事件: 根据指纹对应的用户身份开始/停止会议
            void HandleFingerprintMatched(uint16_t fingerId, bool matched);

        private:
            FingerprintMeetingController() = default;

            uint64_t ResolveAccountId(uint16_t fingerId, bool matched) const;
            void HandleStopMeeting(uint64_t accountId);
            void HandleStartMeeting(uint64_t accountId, uint16_t fingerId) const;
            void ShowStopConfirmPopup();
            void ShowPermissionDeniedTip();
            void ResetPermissionDeniedCount();
            void DoStopMeeting(uint64_t accountId);

        private:
            mutable std::mutex mMutex;
            uint32_t mPermissionDeniedCount {0};
            uint64_t mLastPermissionDeniedTime {0};
        };
    }  // namespace hal
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INTERNAL_HAL_FINGERPRINT_MEETING_CONTROLLER_H
