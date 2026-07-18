/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "ipc/data_def.h"
#include "scmd/command_handler.h"

#include <memory>
#include <utility>
#include <vector>

namespace qifeng::scm {

    /**
     * @brief 命令处理器注册表（单例）
     * @details 各 handler.cpp 通过 REGISTER_COMMAND_HANDLER 宏在静态初始化期自注册工厂函数，
     *          运行期由 CommandDispatcher 调用 LoadFromRegistry 统一构造所有 handler 实例。
     *          消除 ScmServer 中手工列举所有 handler 的 RegisterHandlers() 脚手架代码。
     */
    class HandlerRegistry {
    public:
        /**
         * @brief 获取单例实例
         * @return HandlerRegistry 单例引用
         */
        static HandlerRegistry& Instance();

        /**
         * @brief 注册 handler 工厂函数
         * @param cmd 该 handler 负责的命令类型
         * @param factory 工厂函数，接收 HandlerContext 返回 handler 实例
         */
        void Register(ScmCommand cmd, HandlerFactory factory);

        /**
         * @brief 构建所有已注册的 handler（按注册顺序）
         * @param ctx 构造 handler 所需的运行期依赖上下文
         * @return 已构造的 handler 实例列表
         */
        std::vector<std::unique_ptr<ICommandHandler>> BuildAll(const HandlerContext& ctx) const;

    private:
        HandlerRegistry() = default;
        std::vector<std::pair<ScmCommand, HandlerFactory>> mFactories;
    };

}  // namespace qifeng::scm

/**
 * @brief 自注册宏：在 handler.cpp 末尾使用，将 handler 工厂注册到 HandlerRegistry
 * @details 利用静态对象的初始化期副作用完成注册，避免 ScmServer 集中列举所有 handler。
 *          HandlerClass 必须提供接收 HandlerContext 的构造函数。
 * @param CmdEnum 该 handler 对应的 ScmCommand 枚举值
 * @param HandlerClass handler 类名
 */
#define REGISTER_COMMAND_HANDLER(CmdEnum, HandlerClass) \
    namespace { \
        struct HandlerClass##_AutoReg { \
            HandlerClass##_AutoReg() { \
                ::qifeng::scm::HandlerRegistry::Instance().Register(CmdEnum, \
                    [](const ::qifeng::scm::HandlerContext& ctx) -> std::unique_ptr<::qifeng::scm::ICommandHandler> { \
                        return std::make_unique<HandlerClass>(ctx); \
                    }); \
            } \
        }; \
        static HandlerClass##_AutoReg g_##HandlerClass##_auto_reg; \
    }
