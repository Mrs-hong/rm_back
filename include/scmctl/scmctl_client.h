/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/types.h"
#include "ipc/data_def.h"
#include <string>

namespace qifeng::scm {
    /**
     * @brief SCMD 控制客户端
     * @details CliPaser解析命令行参数后，使用UDS功能块与scmd通信，并将结果输出到终端
     */
    class ScmCtlClient {
    public:
        ScmCtlClient() = default;
        ~ScmCtlClient() = default;

        ScmCtlClient(const ScmCtlClient &) = delete;
        ScmCtlClient &operator=(const ScmCtlClient &) = delete;
        ScmCtlClient(ScmCtlClient &&) = delete;
        ScmCtlClient &operator=(ScmCtlClient &&) = delete;

        /**
         * @brief 发送请求并接收响应
         * @param request 请求结构体
         * @param socketPath UDS socket路径
         * @return ScmResponse 响应结构体
         */
        ScmResponse SendRequest(const ScmRequest &request, const std::string &socketPath);

        /**
         * @brief 格式化响应输出到终端
         * @param response 响应结构体
         * @return ResultMsg 格式化结果结构体
         * @details 格式化响应结构体，根据code字段判断是否成功，成功则输出成功信息，失败则输出错误信息
         */
        ResultMsg FormatOutput(const ScmResponse &response);
    };
}  // namespace qifeng::scm
