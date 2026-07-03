/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "common/types.h"

namespace qifeng::scm {

    /**
     * @brief 命令执行的原始结果
     */
    struct CmdResult {
    int exit_code = -1;          /**< 进程退出码，-1 表示被信号终止 */
    std::string output;          /**< 合并的 stdout/stderr 输出 */
    bool timed_out = false;      /**< 是否因超时被终止 */
};

/**
 * @brief 命令执行选项
 */
struct CmdOptions {
    int timeout_ms = 5000;       /**< 超时时间（毫秒），0 表示不限时，默认 5 秒 */
    size_t max_output = 1 << 20; /**< 最大输出字节数，默认 1MB */
};

/**
 * @brief 自定义结果解析函数类型
 * @details 调用方可注入自定义解析逻辑，将 CmdResult 转换为 ResultMsg
 * @param result 命令执行的原始结果
 * @return ResultMsg 解析后的操作结果
 */
using CmdResultParser = std::function<ResultMsg(const CmdResult &result)>;

/**
 * @brief 执行外部命令，返回原始结果
 * @details 通过 fork/exec/waitpid 实现安全的子进程管理，保证：
 *          - 子进程不会变成僵尸进程（waitpid 回收）
 *          - 子进程超时后整个进程组被 SIGKILL 终止
 *          - 父进程不会因子进程异常而崩溃
 * @param program 程序名称或路径
 * @param args    参数列表（不含程序名本身）
 * @param options 执行选项（超时、输出限制等）
 * @return CmdResult 命令执行的原始结果
 */
CmdResult RunCmd(const std::string &program,
                 const std::vector<std::string> &args,
                 const CmdOptions &options = {});

/**
 * @brief 执行外部命令，返回解析后的结果
 * @details 在 RunCmd 基础上应用结果解析器。若未提供自定义解析器，
 *          使用默认解析：退出码 0 为成功，输出作为 msg；超时或非零退出码为失败。
 * @param program 程序名称或路径
 * @param args    参数列表（不含程序名本身）
 * @param options 执行选项（超时、输出限制等）
 * @param parser  自定义结果解析函数，为空则使用默认解析
 * @return ResultMsg 执行结果，code=0 表示成功
 */
ResultMsg ExecuteCmd(const std::string &program,
                     const std::vector<std::string> &args,
                     const CmdOptions &options = {},
                     const CmdResultParser &parser = nullptr);

}  // namespace qifeng::scm
