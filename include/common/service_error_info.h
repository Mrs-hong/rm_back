/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>

namespace qifeng::scm {

    /**
     * @brief 服务错误诊断信息
     * 封装 systemd Service 接口中与进程退出/失败相关的诊断属性
     */
    struct ServiceErrorInfo {
        std::string subState;    // systemd SubState: running/dead/exited/failed/auto-restart
        std::string result;      // systemd Result: success/exit-code/signal/timeout/start-limit-hit/resources
        int exitCode {0};        // 进程退出码（exit_code > 0 表示异常退出）
        int exitStatus {0};      // 进程退出状态（被信号杀死时为信号编号）
        std::string statusText;  // systemd StatusText：人类可读的状态描述

        /**
         * @brief 将错误信息格式化为人类可读的摘要字符串
         * @param info 错误诊断信息
         * @param activeState systemd ActiveState（active/inactive/failed/activating/deactivating）
         * @param recoveryCount 服务重启次数（NRestarts）
         * @return std::string 格式化后的错误摘要，若无错误信息则返回空字符串
         */
        static std::string FormatError(const ServiceErrorInfo &info, const std::string &activeState, int recoveryCount);
    };

}  // namespace qifeng::scm
