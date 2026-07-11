/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/types.h"

#include <string>

namespace qifeng::scm {
    class CommandDispatcher;
    class ServiceControl;
    class KeyOperationRecorder;

    /**
     * @brief 关键操作恢复服务
     * @details 从 ScmServer 抽出的职责：读取上次未完成的关键操作记录，
     *          根据 result 语义（0成功/1失败/2进行中）执行相应恢复逻辑。
     *          result==2 时通过 CommandDispatcher 分发到对应 handler 的 Recover()。
     */
    class OperationRecoveryService {
    public:
        /**
         * @brief 构造函数
         * @param dispatcher 命令分发器（需已通过 LoadFromRegistry 注册所有 handler）
         * @param serviceControl 服务控制门面
         * @param recorder 关键操作记录器（需已设置文件路径）
         */
        OperationRecoveryService(CommandDispatcher& dispatcher,
                                 ServiceControl& serviceControl,
                                 KeyOperationRecorder& recorder);

        /**
         * @brief 执行操作恢复
         * @details 读取上次关键操作记录，根据 result 语义处理：
         *          - 0：上次操作成功完成，清除记录
         *          - 1：上次操作失败，仅告警提示
         *          - 2：上次操作进行中被异常终止，通过分发器查表分发恢复
         */
        void Run();

    private:
        CommandDispatcher& mDispatcher;
        ServiceControl& mServiceControl;
        KeyOperationRecorder& mRecorder;
    };

}  // namespace qifeng::scm
