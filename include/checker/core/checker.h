/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <json/json.h>

#include <memory>
#include <string>

#include "checker/core/result.h"

namespace qifeng::scm {

    /**
     * @brief checker 非模板基类（类型擦除）
     * @details CheckerRegistry 统一存储 ICheckerBase 指针，配置经 SetConfig 注入后 Run() 无参。
     *          设计要点：
     *            - 默认构造、不可拷贝、可移动；
     *            - 配置与执行解耦：先 SetConfig 注入配置，再 Run() 执行；
     */
    class ICheckerBase {
    public:
        ICheckerBase() = default;
        virtual ~ICheckerBase() = default;
        ICheckerBase(const ICheckerBase &) = delete;
        ICheckerBase &operator=(const ICheckerBase &) = delete;

        /**
         * @brief 检查项名称（用于报告标识）
         */
        virtual std::string Name() const = 0;

        /**
         * @brief 严重等级：CRITICAL 失败将导致整体 FAIL，WARNING 仅记录
         */
        virtual enum Severity Severity() const = 0;

        /**
         * @brief 执行检查（配置已通过 SetConfig 注入）。
         *        Runner 负责超时包裹，本函数应尽量快速且 RAII 释放资源。
         */
        virtual CheckResult Run() = 0;

        /**
         * @brief 从 json 子段解析配置到内部 mConfig。
         *        首次 Run() 调用前必须完成有效的初始化。
         * @param configSrc 该 checker 对应的 json 子段（如 root["disk"]）
         */
        virtual void SetConfig(const Json::Value &configSrc) = 0;
    };

    /**
     * @brief checker 模板基类
     * @details 子类继承 IChecker<XxxConfig>，实现 Name/Severity/Run/ParseConfig。
     *          每个 checker 自包含配置结构 + 解析逻辑，新增检查项零改动 core。
     *          SetConfig 默认实现委派给子类的 ParseConfig，把 Json::Value 转 ConfigT。
     * @tparam ConfigT 配置结构体类型，需默认可构造
     */
    template <typename ConfigT>
    class IChecker : public ICheckerBase {
    public:
        IChecker() = default;
        ~IChecker() override = default;

        /**
         * @brief 默认实现：调用子类 ParseConfig 把 json 子段写入 mConfig
         */
        void SetConfig(const Json::Value &configSrc) override {
            ParseConfig(configSrc, mConfig);
        }

    protected:
        ConfigT mConfig;  // 由 SetConfig 更新；Run() 使用

    private:
        /**
         * @brief 子类实现：从 json 子段解析到 cfg
         * @param j 该 checker 对应的 json 子段
         * @param cfg 待填充的配置对象（已为默认值）
         */
        virtual void ParseConfig(const Json::Value &j, ConfigT &cfg) = 0;
    };

    /**
     * @brief checker 指针别名（类型擦除）
     */
    using CheckerPtr = std::unique_ptr<ICheckerBase>;

}  // namespace qifeng::scm
