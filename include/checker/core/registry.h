/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <functional>
#include <utility>
#include <vector>

#include "checker/core/checker.h"

namespace qifeng::scm {

/**
 * @brief 检查器注册表
 * @details 持有工厂列表，统一构建所有 checker。扩展方式：
 *   实现 IChecker 子类 T（需默认可构造、提供 static ClassName()），
 *   在 register_all.cpp 增加 `reg.Add<T>();`，无需改动 core。
 * 设计：checker 默认构造，运行期 Context 经 Runner -> Run(ctx) 注入，
 * 避免构造期依赖，便于单元测试与并发。
 */
class CheckerRegistry {
public:
    using Factory = std::function<CheckerPtr()>;

    /**
     * @brief 注册一个检查器类型
     * @details T 需默认可构造且提供 static std::string ClassName()。
     */
    template <class T>
    void Add() {
        mFactories.emplace_back(T::ClassName(), []() -> CheckerPtr { return std::make_unique<T>(); });
    }

    /**
     * @brief 构建所有 checker 实例（保持注册顺序）
     */
    std::vector<CheckerPtr> BuildAll() const;

    /**
     * @brief 调试用：已注册的检查项名
     */
    std::vector<std::string> RegisteredNames() const;

private:
    std::vector<std::pair<std::string, Factory>> mFactories;
};

/**
 * @brief 在 register_all.cpp 中实现：集中注册全部内置检查器
 */
void RegisterAll(CheckerRegistry &reg);

}  // namespace qifeng::scm