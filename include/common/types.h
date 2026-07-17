/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <string>

namespace qifeng::scm {
    // ResultMsg前向声明
    struct ResultMsg;

    // 辅助函数声明
    ResultMsg MakeResult(int code, const std::string &msg);
    // code: 0 成功
    ResultMsg MakeSuccess();
    // code: -1 失败
    ResultMsg MakeError(const std::string &msg);
    // code: 1 警告
    ResultMsg MakeWarning(const std::string &msg);

    /**
     * @brief 操作结果封装
     */
    struct ResultMsg {
        int code {0};     // 0: success, non-zero: failure or custom code
        std::string msg;  // 错误信息

        ResultMsg() = default;
        ResultMsg(int c, const std::string &m) : code(c), msg(m) {}

        bool IsDefalutSuccess() const { return code == 0; }
    };
}  // namespace qifeng::scm
