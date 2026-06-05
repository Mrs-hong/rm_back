/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmctl/cli_paser.h"
#include "scmctl/scmctl_client.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
    /**
     * @brief 获取scmd的UDS socket路径
     * @details 优先从环境变量SCMD_SOCK读取，否则使用默认值
     * @return socket路径
     */
    std::string GetSocketPath() {
        const char* envSock = std::getenv("SCMD_SOCK");
        if (envSock != nullptr && envSock[0] != '\0') {
            return std::string(envSock);
        }
        return "./scmd.sock";
    }
}  // namespace

int main(int argc, char* argv[]) {
    // 1. 解析命令行参数
    qifeng::scm::CliPaser parser;
    qifeng::scm::ResultMsg result = parser.Parse(argc, argv);
    if (!result.IsDefalutSuccess()) {
        if (result.code == 1) {
            std::cout << result.msg;
            return 0;
        }
        std::cerr << result.msg;
        return 1;
    }

    // 2. 获取socket路径
    std::string socketPath = GetSocketPath();

    // 3. 发送请求并获取响应
    qifeng::scm::ScmCtlClient client;
    qifeng::scm::ScmResponse response = client.SendRequest(parser.GetRequest(), socketPath);

    // 4. 格式化响应输出
    qifeng::scm::ResultMsg fmtResult = client.FormatOutput(response);
    if (fmtResult.code == -1) {
        std::cerr << fmtResult.msg;
    } else {
        std::cout << fmtResult.msg;
    }
    return 0;
}
