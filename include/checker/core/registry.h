/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <json/json.h>

#include "checker/core/checker.h"

namespace qifeng::scm {

    /**
     * @brief 检查器注册表
     * @details 持有工厂列表，根据 json 根对象决定实例化哪些 checker。
     *          启用策略：仅当 root 含 T::ClassName() 的 key 时实例化并 SetConfig，
     *          否则跳过该 checker。即「json 加配置段 = 启用，不加 = 不启用」。
     *
     * 扩展方式：
     *   实现 IChecker<XxxConfig> 子类 T（需默认可构造、提供 static std::string ClassName()），
     *   在 register_all.cpp 增加 `reg.Register<T>();`，无需改动 core 层。
     * 设计：checker 默认构造，运行期配置经 SetConfig 注入，便于单元测试与并发。
     */
    class CheckerRegistry {
    public:
        using Factory = std::function<std::unique_ptr<ICheckerBase>()>;

        /**
         * @brief 注册一个检查器类型
         * @tparam T 需默认可构造且提供 static std::string ClassName()
         */
        template <class T>
        void Register() {
            mFactories.emplace_back(T::ClassName(), []() -> std::unique_ptr<ICheckerBase> {
                return std::make_unique<T>();
            });
        }

        /**
         * @brief 构建所有启用的 checker（root 含对应 key 才实例化）
         * @param root json 根对象
         * @return 已配置的 checker 列表（保持注册顺序）
         */
        std::vector<std::unique_ptr<ICheckerBase>> BuildAll(const Json::Value &root) const;

        /**
         * @brief 调试用：已注册的检查项名（不论是否在 json 中启用）
         */
        std::vector<std::string> RegisteredNames() const;

    private:
        std::vector<std::pair<std::string, Factory>> mFactories;
    };

    /**
     * @brief 在 register_all.cpp 中实现：集中注册全部检查器
     */
    void RegisterAll(CheckerRegistry &reg);

}  // namespace qifeng::scm
