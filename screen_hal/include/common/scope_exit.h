/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_COMMON_SCOPE_EXIT_H
#define QIFENG_FRAMEWORK_COMMON_SCOPE_EXIT_H

#include <utility>

template <typename F>
class ScopeExit {
public:
    explicit ScopeExit(F&& f) noexcept : mFunc(std::forward<F>(f)), mActive(true) {
    }

    ScopeExit(ScopeExit&& other) noexcept : mFunc(std::move(other.mFunc)), mActive(other.mActive) {
        other.mActive = false;
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit& operator=(ScopeExit&&) = delete;

    ~ScopeExit() noexcept {
        if (mActive) {
            mFunc();  // 要求 func 不抛异常
        }
    }

    void Release() noexcept {
        mActive = false;
    }

private:
    F mFunc;
    bool mActive;
};

#endif  // QIFENG_FRAMEWORK_COMMON_SCOPE_EXIT_H
