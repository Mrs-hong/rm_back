/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "ipc/data_def.h"

namespace qifeng::scm {

    class ServiceControl;
    class KeyOperationRecorder;

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
    };

}  // namespace qifeng::scm
