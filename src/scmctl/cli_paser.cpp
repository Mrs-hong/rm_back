/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "scmctl/cli_command_registry.h"
#include "scmctl/cli_paser.h"
#include "scmd/handlers/version_handler.h"

#include <CLI/CLI.hpp>

namespace qifeng::scm {

    ResultMsg CliPaser::Parse(int argc, char** argv) {
        CLI::App app {"qf_scmc - Service Control Manager Client"};
        app.set_version_flag("--version,-v", "0.0.1");
        app.require_subcommand(0, 1);

        // 通过注册表自动获取所有已注册命令（新增命令无需修改此文件）
        auto commands = CliCommandRegistry::Instance().BuildAll();

        for (auto &cmd : commands) {
            cmd->Setup(app);
        }

        try {
            app.parse(argc, argv);
        } catch (const CLI::CallForVersion &e) {
            mRequest = MakeRequest(VersionRequest{});
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

    const ScmRequest &CliPaser::GetRequest() const {
        return mRequest;
    }

}  // namespace qifeng::scm
