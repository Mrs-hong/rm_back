/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "scmctl/cli_commands.h"
#include "scmctl/cli_paser.h"

#include <CLI/CLI.hpp>
#include <memory>
#include <vector>

namespace qifeng::scm {

    ResultMsg CliPaser::Parse(int argc, char** argv) {
        CLI::App app {"qifeng_scmctl - Service Control Manager Client"};
        app.set_version_flag("--version,-v", "0.0.1");
        app.require_subcommand(0, 1);

        // 注册所有命令
        std::vector<std::unique_ptr<CliCommand>> commands;
        commands.emplace_back(std::make_unique<InstallCommand>());
        commands.emplace_back(std::make_unique<StartCommand>());
        commands.emplace_back(std::make_unique<StopCommand>());
        commands.emplace_back(std::make_unique<RestartCommand>());
        commands.emplace_back(std::make_unique<ReloadCommand>());
        commands.emplace_back(std::make_unique<UpgradeCommand>());
        commands.emplace_back(std::make_unique<ListCommand>());
        commands.emplace_back(std::make_unique<InfoCommand>());
        commands.emplace_back(std::make_unique<LogCommand>());
        commands.emplace_back(std::make_unique<UninstallCommand>());
        commands.emplace_back(std::make_unique<KillCommand>());

        for (auto &cmd : commands) {
            cmd->Setup(app);
        }

        try {
            app.parse(argc, argv);
        } catch (const CLI::CallForVersion &e) {
            mRequest.command = ScmCommand::VERSION;
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
