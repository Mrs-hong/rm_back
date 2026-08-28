//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_POPUP_MANAGER_H
#define QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_POPUP_MANAGER_H

#include <cstdint>
#include <mutex>

namespace qifeng_ca {

    // 弹窗状态管理: 管理会议页 operationTip 的显示与自动过期
    // 业务层调用 Show() 设置弹窗, Get() 读取时自动检查过期
    class PopupManager {
    public:
        // 显示弹窗, durationMs 后自动过期
        void Show(uint16_t tip, uint64_t durationMs);

        // 获取当前弹窗 tip, 过期则清零返回 0
        uint16_t Get();

        // 强制清除弹窗
        void Clear();

    private:
        mutable std::mutex mMutex;
        uint16_t mTip {0};
        uint64_t mExpireTime {0};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_POPUP_MANAGER_H
