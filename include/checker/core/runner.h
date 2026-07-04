/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <memory>
#include <vector>

#include "checker/core/checker.h"
#include "checker/core/result.h"

namespace qifeng::scm {

    /**
     * @brief 并发调度器（带超时）
     * @details 对每个 checker 起一个异步任务，主线程 future::wait_for(timeout) 控制超时：
     *            - 超时则返回 Status::FAIL("timeout")，不阻塞整体流程；
     *            - C++17 无原生线程取消，被超时的任务自然结束（IO 类依赖其自身短超时）；
     *            - 支持顺序模式(parallel=false)便于调试。
     *          Run() 无参、无 Context，配置已通过 SetConfig 注入。
     */
    class Runner {
    public:
        /**
         * @brief 执行所有 checker，按输入顺序返回结果
         * @param checkers 检查器列表
         * @param perItemTimeoutSec 单项超时秒数（<=0 时按 5 处理）
         * @param parallel true=并发；false=顺序（便于调试）
         * @return 检查结果列表
         */
        std::vector<CheckResult> RunAll(const std::vector<std::unique_ptr<ICheckerBase>> &checkers,
                                        int perItemTimeoutSec, bool parallel);
    };

}  // namespace qifeng::scm
