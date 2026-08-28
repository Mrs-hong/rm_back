//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "internal/hal/display/popup_manager.h"

#include "common/common.h"

namespace qifeng_ca {

    void PopupManager::Show(uint16_t tip, uint64_t durationMs) {
        std::lock_guard<std::mutex> lock(mMutex);
        mTip = tip;
        mExpireTime = static_cast<uint64_t>(GetTimeMs()) + durationMs;
    }

    uint16_t PopupManager::Get() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mTip == 0) {
            return 0;
        }
        if (static_cast<uint64_t>(GetTimeMs()) >= mExpireTime) {
            mTip = 0;
            mExpireTime = 0;
            return 0;
        }
        return mTip;
    }

    void PopupManager::Clear() {
        std::lock_guard<std::mutex> lock(mMutex);
        mTip = 0;
        mExpireTime = 0;
    }

}  // namespace qifeng_ca
