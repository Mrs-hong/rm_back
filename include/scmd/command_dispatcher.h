/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

#include <memory>
#include <unordered_map>

namespace qifeng::scm {

    struct ServiceContext;
    class KeyOperationRecorder;
    struct KeyOperationRecord;

    /**
     * @brief 命令分发器
     * @details 维护 ScmCommand 到 ICommandHandler 的映射，根据请求类型分发给对应处理器。
     *          新增命令时只需注册新的处理器，无需修改 ScmServer 核心逻辑。
     */
    class CommandDispatcher {
    public:
        CommandDispatcher() = default;
        ~CommandDispatcher() = default;

        CommandDispatcher(const CommandDispatcher&) = delete;
        CommandDispatcher& operator=(const CommandDispatcher&) = delete;
        CommandDispatcher(CommandDispatcher&&) = delete;
        CommandDispatcher& operator=(CommandDispatcher&&) = delete;

        /**
         * @brief 注册命令处理器
         * @param handler 处理器实例，由分发器接管所有权
         * @details 若同一命令被重复注册，将输出错误日志并忽略后一次注册。
         */
        void Register(std::unique_ptr<ICommandHandler> handler);

        /**
         * @brief 从 HandlerRegistry 加载所有已注册的 handler
         * @param ctx 构造 handler 所需的运行期依赖上下文
         * @details 调用 HandlerRegistry::BuildAll 构造所有 handler 并逐个注册。
         *          替代 ScmServer 中手工列举所有 handler 的脚手架代码。
         */
        void LoadFromRegistry(const HandlerContext& ctx);

        /**
         * @brief 分发请求到对应处理器
         * @param request 已解析的请求对象
         * @param ctx 服务上下文，包含所有子服务
         * @param recorder 关键操作记录器
         * @return 命令执行结果响应；若命令未注册则返回 Unknown command 错误。
         */
        ScmResponse Dispatch(const ScmRequest& request,
                             const ServiceContext& ctx,
                             KeyOperationRecorder& recorder) const;

        /**
         * @brief 分发操作恢复到对应处理器
         * @param record 上次未完成的关键操作记录
         * @param ctx 服务上下文，包含所有子服务
         * @return 恢复结果；若操作类型未知或无对应处理器则返回警告
         * @details 根据 record.optName 解析命令类型，查表调用对应 handler 的 Recover()。
         *          替代 ScmServer 中手工 switch-case 的恢复逻辑。
         */
        ResultMsg Recover(const KeyOperationRecord& record, const ServiceContext& ctx) const;

    private:
        std::unordered_map<ScmCommand, std::unique_ptr<ICommandHandler>> mHandlers;
    };

}  // namespace qifeng::scm
