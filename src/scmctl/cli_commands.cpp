/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "scmctl/cli_commands.h"

namespace qifeng::scm {

    // -------------------- InstallCommand --------------------
    const char* InstallCommand::Name() const {
        return "install";
    }
    const char* InstallCommand::Description() const {
        return "安装服务";
    }

    void InstallCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--name,-n", mServiceName, "服务名称")->required();
        cmd->add_option("--tar_dir", mTarDir, "tar包目录");
    }

    ResultMsg InstallCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req.command = ScmCommand::INSTALL;
        req.serviceName = mServiceName;
        req.tarDir = mTarDir;
        return MakeSuccess();
    }

    // -------------------- StartCommand --------------------
    const char* StartCommand::Name() const {
        return "start";
    }
    const char* StartCommand::Description() const {
        return "启动服务";
    }

    void StartCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--name,-n", mServiceName, "服务名称")->required();
    }

    ResultMsg StartCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req.command = ScmCommand::START;
        req.serviceName = mServiceName;
        return MakeSuccess();
    }

    // -------------------- StopCommand --------------------
    const char* StopCommand::Name() const {
        return "stop";
    }
    const char* StopCommand::Description() const {
        return "停止服务";
    }

    void StopCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--name,-n", mServiceName, "服务名称")->required();
    }

    ResultMsg StopCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req.command = ScmCommand::STOP;
        req.serviceName = mServiceName;
        return MakeSuccess();
    }

    // -------------------- RestartCommand --------------------
    const char* RestartCommand::Name() const {
        return "restart";
    }
    const char* RestartCommand::Description() const {
        return "重启服务";
    }

    void RestartCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--name,-n", mServiceName, "服务名称");
        cmd->add_flag("-a", mAll, "重启所有服务");
    }

    ResultMsg RestartCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        if (mAll) {
            req.command = ScmCommand::RESTART_ALL;
            return MakeSuccess();
        }
        if (mServiceName.empty()) {
            return MakeError("restart requires --name or -a flag");
        }
        req.command = ScmCommand::RESTART;
        req.serviceName = mServiceName;
        return MakeSuccess();
    }

    // -------------------- UpgradeCommand --------------------
    const char* UpgradeCommand::Name() const {
        return "upgrade";
    }
    const char* UpgradeCommand::Description() const {
        return "升级服务";
    }

    void UpgradeCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--name,-n", mServiceName, "服务名称")->required();
        cmd->add_option("--tar_dir", mTarDir, "新版本tar包目录")->required();
    }

    ResultMsg UpgradeCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req.command = ScmCommand::UPGRADE;
        req.serviceName = mServiceName;
        req.tarDir = mTarDir;
        return MakeSuccess();
    }

    // -------------------- ListCommand --------------------
    const char* ListCommand::Name() const {
        return "list";
    }
    const char* ListCommand::Description() const {
        return "查看所有服务";
    }

    void ListCommand::Setup(CLI::App &app) {
        app.add_subcommand(Name(), Description());
    }

    ResultMsg ListCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req.command = ScmCommand::LIST;
        return MakeSuccess();
    }

    // -------------------- InfoCommand --------------------
    const char* InfoCommand::Name() const {
        return "info";
    }
    const char* InfoCommand::Description() const {
        return "查看服务详情";
    }

    void InfoCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--name,-n", mServiceName, "服务名称")->required();
    }

    ResultMsg InfoCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req.command = ScmCommand::INFO;
        req.serviceName = mServiceName;
        return MakeSuccess();
    }

    // -------------------- LogCommand --------------------
    const char* LogCommand::Name() const {
        return "log";
    }
    const char* LogCommand::Description() const {
        return "查看操作日志";
    }

    void LogCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--type,-t", mLogType, "日志级别过滤");
        cmd->add_option("-n", mLogCount, "日志条数");
    }

    ResultMsg LogCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req.command = ScmCommand::LOG;
        req.logLevel = mLogType;
        req.logCount = mLogCount;
        return MakeSuccess();
    }

    // -------------------- UninstallCommand --------------------
    const char* UninstallCommand::Name() const {
        return "uninstall";
    }
    const char* UninstallCommand::Description() const {
        return "卸载服务";
    }

    void UninstallCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--name,-n", mServiceName, "服务名称")->required();
    }

    ResultMsg UninstallCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req.command = ScmCommand::UNINSTALL;
        req.serviceName = mServiceName;
        return MakeSuccess();
    }

    // -------------------- ReloadCommand --------------------
    const char* ReloadCommand::Name() const {
        return "reload";
    }
    const char* ReloadCommand::Description() const {
        return "重载服务配置";
    }

    void ReloadCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--name,-n", mServiceName, "服务名称");
        cmd->add_flag("-a", mAll, "重载所有服务");
    }

    ResultMsg ReloadCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        if (mAll) {
            req.command = ScmCommand::RELOAD;
            req.serviceName = "";
            return MakeSuccess();
        }
        if (mServiceName.empty()) {
            return MakeError("reload requires --name or -a flag");
        }
        req.command = ScmCommand::RELOAD;
        req.serviceName = mServiceName;
        return MakeSuccess();
    }

    // -------------------- KillCommand --------------------
    const char* KillCommand::Name() const {
        return "kill";
    }
    const char* KillCommand::Description() const {
        return "使scmd服务端优雅退出";
    }

    void KillCommand::Setup(CLI::App &app) {
        app.add_subcommand(Name(), Description());
    }

    ResultMsg KillCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req.command = ScmCommand::KILL;
        return MakeSuccess();
    }

}  // namespace qifeng::scm
