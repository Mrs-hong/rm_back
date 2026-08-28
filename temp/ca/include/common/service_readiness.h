//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_SERVICE_READINESS_H
#define QIFENG_CA_INCLUDE_COMMON_SERVICE_READINESS_H

#include <atomic>

namespace qifeng_ca {

    // CA服务启动就绪标记: 标记整个CA服务(不含LMS异步启动)是否完全启动完成
    // 在drogon HTTP服务启动完成后置位, 录音等关键操作前需检查此标记
    // 防止HAL已启动但drogon尚未就绪时录音请求导致后台无法记录
    class ServiceReadiness {
    public:
        static ServiceReadiness &GetInstance() {
            static ServiceReadiness Instance;
            return Instance;
        }

        enum ServiceStatus : int {
            NotReady = 0,
            Ready = 1,
            UpgradeInProgress = 2,
            ResetSystem = 3,
        };

        // 标记CA服务已完全启动(在drogon启动回调中调用)
        void MarkReady() { mStatus.store(Ready, std::memory_order_release); }

        // 设置CA服务状态
        void SetStatus(ServiceStatus status) { mStatus.store(status, std::memory_order_release); }

        // 查询CA服务是否已完全启动
        bool IsReady() const { return mStatus.load(std::memory_order_acquire) == Ready; }

        ServiceStatus GetStatus() const { return static_cast<ServiceStatus>(mStatus.load(std::memory_order_acquire)); }

    private:
        ServiceReadiness() = default;
        ~ServiceReadiness() = default;
        ServiceReadiness(const ServiceReadiness &) = delete;
        ServiceReadiness &operator=(const ServiceReadiness &) = delete;
        ServiceReadiness(ServiceReadiness &&) = delete;
        ServiceReadiness &operator=(ServiceReadiness &&) = delete;

        std::atomic<ServiceStatus> mStatus {ServiceStatus::NotReady};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_SERVICE_READINESS_H
