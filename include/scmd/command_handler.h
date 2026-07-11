/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/types.h"
#include "ipc/data_def.h"
#include "service_manger/key_recoder.h"

#include <functional>
#include <memory>
#include <string>

namespace qifeng::scm {

    class ServiceControl;
    class KeyOperationRecorder;

    /**
     * @brief 运行期依赖上下文（供需要构造参数的 handler 使用）
     * @details 由 ScmServer 在注册阶段构造，传递给各 handler 工厂函数。
     *          将 handler 构造所需的运行期依赖集中管理，避免 handler 反向依赖 ScmServer。
     */
    struct HandlerContext {
        std::string selfTestConfigPath;         // CheckHandler 使用：自检配置文件路径
        std::function<void()> shutdownCallback;  // KillHandler 使用：触发 scmd 优雅退出的回调
    };

    /**
     * @brief handler 工厂函数类型：接收 HandlerContext，返回 handler 实例
     */
    using HandlerFactory = std::function<std::unique_ptr<class ICommandHandler>(const HandlerContext&)>;

    /**
     * @brief 命令处理器抽象接口
     * @details 每个 ScmCommand 对应一个实现类，负责参数校验、业务调用和响应组装。
     *          通过此接口将 scmd_server.cpp 中的巨型 switch-case 拆分为独立的处理器。
     */
    class ICommandHandler {
    public:
        virtual ~ICommandHandler() = default;

        /**
         * @brief 获取该处理器负责的命令类型
         * @return 对应的 ScmCommand 枚举值
         */
        virtual ScmCommand GetCommand() const = 0;

        /**
         * @brief 处理命令请求
         * @param request 已解析的请求对象
         * @param serviceControl 服务控制门面，执行业务操作
         * @param recorder 关键操作记录器，需要记录关键操作的处理器自行使用
         * @return 命令执行结果响应
         */
        virtual ScmResponse Handle(const ScmRequest& request,
                                    ServiceControl& serviceControl,
                                    KeyOperationRecorder& recorder) = 0;

        /**
         * @brief 恢复上次未完成的关键操作
         * @param record 上次关键操作记录（result=2 表示进行中被异常终止）
         * @param serviceControl 服务控制门面，执行恢复操作
         * @return 恢复结果；默认实现返回不支持自动恢复的警告
         */
        virtual ResultMsg Recover(const KeyOperationRecord& record, ServiceControl& serviceControl) {
            (void)serviceControl;
            return MakeWarning("Operation does not support auto-recovery: " + record.optName);
        }
    };

}  // namespace qifeng::scm
