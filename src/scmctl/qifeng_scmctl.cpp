/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmctl/cli_paser.h"
#include "scmctl/scmctl_client.h"

#include "common/scmd_def.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <yaml-cpp/yaml.h>

namespace {
    /**
     * @brief 获取scmd的UDS socket路径
     * @details 优先读取scmd.yaml中的uds.socket_path，其次环境变量SCMD_SOCK，最后使用默认路径
     * @return socket路径
     */
    // NOLINTNEXTLINE(readability-function-size)
    std::string GetSocketPath() {
        // 1. 优先从scmd.yaml读取uds.socket_path，与scmd保持一致
        try {
            if (std::filesystem::exists(qifeng::scm::DefaultConfigPath)) {
                YAML::Node config = YAML::LoadFile(qifeng::scm::DefaultConfigPath);
                if (config["scmd"] && config["scmd"]["uds"] && config["scmd"]["uds"]["socket_path"]) {
                    std::string socketPath = config["scmd"]["uds"]["socket_path"].as<std::string>();
                    if (!socketPath.empty()) {
                        return socketPath;
                    }
                }
            }
        } catch (...) {
            // yaml解析失败，继续回退到环境变量和默认路径
        }

        // 2. 环境变量SCMD_SOCK
        const char* envSock = std::getenv("SCMD_SOCK");
        if (envSock != nullptr && envSock[0] != '\0') {
            return std::string(envSock);
        }

        // 3. 默认路径
        return "/run/qifeng-scm/scmd.sock";
    }
}  // namespace

int main(int argc, char* argv[]) {
    // 1. 解析命令行参数
    qifeng::scm::CliPaser parser;
    qifeng::scm::ResultMsg result = parser.Parse(argc, argv);
    if (!result.IsDefaultSuccess()) {
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

    // 4. 输出响应
    // SLOG 命令的日志原文直接打印，不做多余格式化（不加 [OK] 前缀、不格式化 data）
    if (parser.GetRequest().command == qifeng::scm::ScmCommand::SLOG) {
        if (response.code == 0) {
            std::cout << response.message;
            return 0;
        }
        std::cerr << response.message << "\n";
        return response.code == 0 ? 0 : 1;
    }

    // 其它命令走统一格式化输出
    qifeng::scm::ResultMsg fmtResult = client.FormatOutput(response);
    if (fmtResult.code == -1) {
        std::cerr << fmtResult.msg;
    } else {
        std::cout << fmtResult.msg;
    }
    return fmtResult.code;
}
