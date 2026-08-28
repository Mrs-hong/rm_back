/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_MUTEX_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_MUTEX_H

#include <mutex>
#include <stdexcept>

namespace common {

    namespace utils {

        // 支持 std::lock_guard
        class TimedMutex {
            std::timed_mutex mMtx;
            std::chrono::milliseconds mTimeout;

        public:
            explicit TimedMutex(std::chrono::milliseconds timeout = std::chrono::milliseconds(100))
                : mTimeout(timeout) {
            }

            void lock() {  // NOLINT (clang-tidy readability-identifier-naming)
                if (!mMtx.try_lock_for(mTimeout)) {
                    throw std::runtime_error("TimedMutexAdapter: timeout waiting for lock");
                }
            }

            void unlock() {  // NOLINT (clang-tidy readability-identifier-naming)
                mMtx.unlock();
            }
        };
    }  // namespace utils
}  // namespace common
#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_MUTEX_H
