//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "internal/aas/bms_audio_provider.h"

namespace qifeng_ca {

    BmsAudioProvider::BmsAudioProvider(const std::string &audioId, uint64_t accountId)
        : mAudioId(audioId), mAccountId(accountId) {
    }

    void BmsAudioProvider::SetTransResultCallback(TransResultCallback cb) {
        mTransResultCb = std::move(cb);
    }

}  // namespace qifeng_ca
