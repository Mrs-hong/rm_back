//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_GRACEFUL_SHUTDOWN_H
#define QIFENG_CA_INCLUDE_COMMON_GRACEFUL_SHUTDOWN_H

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace qifeng_ca {

    class GracefulShutdown {
    public:
        static GracefulShutdown &GetInstance() {
            static GracefulShutdown Instance;
            return Instance;
        }

        // 注册信号处理器(SIGTERM, SIGINT)
        void RegisterSignals();

        // 检查是否收到退出信号
        bool IsShutdownRequested() const { return mShutdownRequested.load(std::memory_order_acquire); }

        // 等待退出信号(阻塞)
        void WaitForShutdown();

        // 主动请求退出
        void RequestShutdown() {
            mShutdownRequested.store(true, std::memory_order_release);
            mCv.notify_all();
        }

    private:
        GracefulShutdown() = default;

        static void SignalHandler(int signum);

        std::atomic<bool> mShutdownRequested {false};
        std::condition_variable mCv;
        std::mutex mCvMutex;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_GRACEFUL_SHUTDOWN_H
