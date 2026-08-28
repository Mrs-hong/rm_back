//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <csignal>

#include "qifeng_framework/common/logger.h"

#include "common/graceful_shutdown.h"

namespace qifeng_ca {

    void GracefulShutdown::RegisterSignals() {
        std::signal(SIGTERM, SignalHandler);
        std::signal(SIGINT, SignalHandler);
        SLOG_INFO << "GracefulShutdown: signal handlers registered (SIGTERM, SIGINT)";
    }

    void GracefulShutdown::SignalHandler(int signum) {
        auto &inst = GetInstance();
        SLOG_INFO << "GracefulShutdown: received signal " << signum;
        inst.RequestShutdown();
    }

    void GracefulShutdown::WaitForShutdown() {
        std::unique_lock<std::mutex> lock(mCvMutex);
        mCv.wait(lock, [this] { return mShutdownRequested.load(std::memory_order_acquire); });
    }

}  // namespace qifeng_ca
