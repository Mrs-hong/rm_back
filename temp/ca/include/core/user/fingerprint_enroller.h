//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_USER_FINGERPRINT_ENROLLER_H
#define QIFENG_CA_INCLUDE_CORE_USER_FINGERPRINT_ENROLLER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "common/status.h"
#include "internal/hal/fingerprint_bridge.h"

namespace qifeng_ca {

    // 指纹录入器: 封装指纹录入的会话管理、后台采集、WS进度推送
    // 指纹录入全局唯一, 同时只允许一个用户进行录入
    class FingerprintEnroller {
    public:
        static FingerprintEnroller &GetInstance();

        // 启动指纹录入
        // oldFingerId: 用户已存在的旧指纹模板ID(0表示新录入, 非0表示覆盖写入: 成功后删除旧模板)
        // 返回 Status: code=0 表示流程已成功启动, 采集结果通过WS推送
        Status StartEnroll(uint64_t accountId, uint16_t oldFingerId, const std::string &accountName);

        // 取消进行中的指纹录入
        Status Cancel(uint64_t accountId);

        // 查询是否有进行中的录入
        bool IsEnrolling(uint64_t accountId) const;

    private:
        FingerprintEnroller() = default;
        ~FingerprintEnroller() = default;
        FingerprintEnroller(const FingerprintEnroller &) = delete;
        FingerprintEnroller &operator=(const FingerprintEnroller &) = delete;
        FingerprintEnroller(FingerprintEnroller &&) = delete;
        FingerprintEnroller &operator=(FingerprintEnroller &&) = delete;

        // 指纹录入会话上下文
        struct EnrollSession {
            uint64_t mAccountId = 0;
            uint16_t mFingerId {0};
            uint16_t mOldFingerId = 0;  // 旧指纹模板ID(成功后删除, 0表示新录入)
            std::string mAccountName;
            uint8_t mSuccessCount = 0;
            uint8_t mFailCount = 0;
            uint8_t mTotalCount = 0;
            std::atomic<bool> mCancelled {false};
        };

        using EnrollSessionPtr = std::shared_ptr<EnrollSession>;

        // 后台采集线程入口
        void EnrollWorker(EnrollSessionPtr session);

        // 处理单次采集结果(返回true表示应结束循环)
        bool HandleCaptureResult(EnrollSessionPtr session, FingerprintResult ret);

        // 完成录入: 保存模板并入库, 删除旧模板(失败不影响录入成功)
        bool FinalizeEnroll(EnrollSessionPtr session);

        // 清理会话(从Map移除 + 恢复LED)
        void CleanupSession(uint64_t accountId);

        // 取消进行中的指纹录入(不加锁)
        Status CancelUnsafe(uint64_t accountId);

    private:
        mutable std::mutex mMutex;
        std::unordered_map<uint64_t, EnrollSessionPtr> mSessions;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_USER_FINGERPRINT_ENROLLER_H
