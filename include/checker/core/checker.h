/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <memory>
#include <string>

#include "checker/core/context.h"
#include "checker/core/result.h"

namespace qifeng::scm {

/**
 * @brief 自检器统一接口
 * @details 所有具体检查器(DiskChecker 等)实现 IChecker，由 Registry 注册、Runner 调度。
 * 设计要点：
 *   - Run() 接收 const Context &，避免全局状态，便于测试与并发。
 *   - 资源不可用时返回 Status::SKIPPED（与 FAIL 区分）。
 */
class IChecker {
public:
    IChecker() = default;
    virtual ~IChecker() = default;
    IChecker(const IChecker&) = delete;
    IChecker& operator=(const IChecker&) = delete;
    IChecker(IChecker&&) = default;
    IChecker& operator=(IChecker&&) = default;

    /**
     * @brief 检查项名，如 "disk"（用作报告 key，需唯一）
     */
    virtual std::string Name() const = 0;

    /**
     * @brief 严重级别：critical 失败会令 overall=FAIL
     */
    virtual enum Severity Severity() const = 0;

    /**
     * @brief 实际检查逻辑。Runner 负责超时包裹，本函数应尽量快速且 RAII 释放资源。
     */
    virtual CheckResult Run(const Context &ctx) = 0;
};

using CheckerPtr = std::unique_ptr<IChecker>;

}  // namespace qifeng::scm