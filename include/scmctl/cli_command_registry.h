/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include "scmctl/cli_commands.h"

#include <functional>
#include <memory>
#include <vector>

namespace qifeng::scm {

    /**
     * @brief CLI 命令注册表（单例）
     * @details 类似 HandlerRegistry，通过 REGISTER_CLI_COMMAND 宏在静态初始化期自注册，
     *          消除 cli_paser.cpp 中的手工命令列表，实现开闭原则。
     */
    class CliCommandRegistry {
    public:
        using CommandFactory = std::function<std::unique_ptr<CliCommand>()>;

        static CliCommandRegistry& Instance();

        /**
         * @brief 注册命令工厂
         * @param factory 命令构造函数封装
         */
        void Register(CommandFactory factory);

        /**
         * @brief 构造所有已注册命令的实例
         * @return 命令实例列表
         */
        std::vector<std::unique_ptr<CliCommand>> BuildAll() const;

    private:
        CliCommandRegistry() = default;
        std::vector<CommandFactory> mFactories;
    };

}  // namespace qifeng::scm

/**
 * @brief CLI 命令自注册宏
 * @details 在 cli_commands.cpp 中各命令实现末尾调用，静态初始化期自动注册到 CliCommandRegistry。
 *          新增 CLI 命令时只需在命令实现文件末尾添加此宏，无需修改 cli_paser.cpp。
 * @param CommandClass CliCommand 子类名
 */
#define REGISTER_CLI_COMMAND(CommandClass)                                            \
    namespace {                                                                       \
        struct CommandClass##_CliAutoReg {                                             \
            CommandClass##_CliAutoReg() {                                              \
                ::qifeng::scm::CliCommandRegistry::Instance().Register(               \
                    []() { return std::make_unique<CommandClass>(); });               \
            }                                                                         \
        };                                                                            \
        static CommandClass##_CliAutoReg g_##CommandClass##_cli_reg;                  \
    }
