//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_AAS_BMS_AUDIO_PROVIDER_H
#define QIFENG_CA_INCLUDE_INTERNAL_AAS_BMS_AUDIO_PROVIDER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "aas/aas_callback.h"

namespace qifeng_ca {

    class BmsAudioProvider : public qifeng::aas::GetAudioBase {
    public:
        virtual void SignalEnd() = 0;
        virtual bool IsEnded() const = 0;
        virtual bool IsRealtime() const = 0;
        virtual void PushAudioData(const uint8_t* data, size_t len) = 0;

        // 暂停/继续: 暂停时GetAudio阻塞不处理数据, 音频时长自然暂停
        virtual void Pause() = 0;
        virtual void Resume() = 0;

        // 检查缓冲区是否还有未处理的数据(对外提供)
        virtual bool HasPendingData() const = 0;

        // 离线数据准备回调: 离线模式下由外部提供数据读取逻辑
        // 回调返回true表示还有数据, false表示数据已读完
        using DataPreparer = std::function<bool(std::vector<uint8_t> &)>;
        // 设置离线数据准备回调(基类默认no-op, 子类按需覆写)
        virtual void SetDataPreparer(DataPreparer /*preparer*/) {}

        const std::string &GetAudioId() const { return mAudioId; }
        uint64_t GetAccountId() const { return mAccountId; }

        using TransResultCallback =
            std::function<void(const std::string &audioId, uint64_t accountId, const qifeng::aas::AasResult &result)>;
        void SetTransResultCallback(TransResultCallback cb);

    protected:
        BmsAudioProvider(const std::string &audioId, uint64_t accountId);

        std::string mAudioId;
        uint64_t mAccountId {0};
        TransResultCallback mTransResultCb;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_AAS_BMS_AUDIO_PROVIDER_H
