/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "scmctl/cli_commands.h"
#include "scmctl/cli_parser.h"

#include <CLI/CLI.hpp>
#include <memory>
#include <vector>

namespace qifeng::scm {

    ResultMsg CliParser::Parse(int argc, char** argv) {
        CLI::App app {"qf_scmc - Service Control Manager Client"};
        app.set_version_flag("--version,-v", "0.0.1");
        app.require_subcommand(0, 1);

        // 从注册表加载所有命令（由 REGISTER_CLI_COMMAND 宏在各 Command 实现 cpp 中自注册）
        auto commands = CliCommandRegistry::Instance().BuildAll();

        for (auto &cmd : commands) {
            cmd->Setup(app);
        }

        try {
            app.parse(argc, argv);
        } catch (const CLI::CallForVersion &e) {
            mRequest.data = VersionRequest{};
            return MakeWarning(e.what());
        } catch (const CLI::ParseError &e) {
            if (e.get_exit_code() == 0) {
                return MakeWarning(app.help());
            }
            return MakeError(std::string("Error: ") + e.what() + "\n" + app.help());
        }

        // 遍历命令，命中者组装 Request
        for (auto &cmd : commands) {
            ResultMsg result = cmd->BuildRequest(app, mRequest);
            if (result.code == 0) {
                return MakeSuccess();
            }
            if (result.code == -1) {
                return result;
            }
            // code == 2 表示未命中，继续尝试下一个命令
        }

        // 没有子命令
        return MakeWarning(app.help());
    }

    const ScmRequest &CliParser::GetRequest() const {
        return mRequest;
    }

}  // namespace qifeng::scm
