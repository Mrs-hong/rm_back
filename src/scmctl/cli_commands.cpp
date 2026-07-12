/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "scmctl/cli_command_registry.h"
#include "scmctl/cli_commands.h"

#include <filesystem>

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
        cmd->add_option("--dir,-d", mDir, "tar包目录（支持相对路径）");
    }

    ResultMsg InstallCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        // 将相对路径转换为绝对路径
        std::string absDir = mDir.empty() ? mDir : std::filesystem::absolute(mDir).string();
        req = MakeRequest(InstallRequest {mServiceName, absDir});
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
        cmd->add_option("--name,-n", mServiceName, "服务名称（不指定时操作scmd自身）");
    }

    ResultMsg StartCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req = MakeRequest(StartRequest {mServiceName});
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
        cmd->add_option("--name,-n", mServiceName, "服务名称（不指定时操作scmd自身）");
        cmd->add_flag("-a", mAll, "停止所有已安装服务");
    }

    ResultMsg StopCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        if (mAll) {
            req = MakeRequest(StopAllRequest {});
        } else {
            req = MakeRequest(StopRequest {mServiceName});
        }
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
            req = MakeRequest(RestartAllRequest {});
            return MakeSuccess();
        }
        // 无 --name 且无 -a 时，操作 scmd 自身（发送空 serviceName）
        req = MakeRequest(RestartRequest {mServiceName});
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
        cmd->add_option("--dir,-d", mDir, "新版本tar包目录（支持相对路径）")->required();
    }

    ResultMsg UpgradeCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        // 将相对路径转换为绝对路径
        std::string absDir = mDir.empty() ? mDir : std::filesystem::absolute(mDir).string();
        req = MakeRequest(UpgradeRequest {mServiceName, absDir});
        return MakeSuccess();
    }

    // -------------------- UpgradesCommand --------------------
    const char* UpgradesCommand::Name() const {
        return "upgrades";
    }
    const char* UpgradesCommand::Description() const {
        return "一体化升级（服务+模型+Nginx）：-n 指定服务，可选 -d 指定外部素材目录/tar包";
    }

    void UpgradesCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--name,-n", mServiceName, "服务名称")->required();
        // -d 可选：指定外部升级素材目录或 tar 包；不提供则使用服务内部 upgrade.soft_dir
        cmd->add_option("--dir,-d", mDir, "外部升级素材目录或tar包（可选，不提供则使用服务内部soft_dir）");
    }

    ResultMsg UpgradesCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        // 将相对路径转换为绝对路径，空值保持为空
        std::string absDir = mDir.empty() ? mDir : std::filesystem::absolute(mDir).string();
        req = MakeRequest(UpgradesRequest {mServiceName, absDir});
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
        req = MakeRequest(ListRequest {});
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
        cmd->add_flag("--error", mShowError, "显示错误原因（崩溃、启动失败、未启动等）");
    }

    ResultMsg InfoCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req = MakeRequest(InfoRequest {mServiceName, mShowError});
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
        req = MakeRequest(LogRequest {mLogType, mLogCount});
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
        cmd->add_option("--name,-n", mServiceName, "服务名称");
        cmd->add_flag("-a", mAll, "卸载所有已安装服务（保留 scmd 自身）");
    }

    ResultMsg UninstallCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        if (mAll) {
            req = MakeRequest(UninstallAllRequest {});
        } else {
            if (mServiceName.empty()) {
                return ResultMsg(-1, "uninstall 需要指定 --name 或 -a");
            }
            req = MakeRequest(UninstallRequest {mServiceName});
        }
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
            req = MakeRequest(ReloadAllRequest {});
            return MakeSuccess();
        }
        if (mServiceName.empty()) {
            return MakeError("reload requires --name or -a flag");
        }
        req = MakeRequest(ReloadRequest {mServiceName});
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
        req = MakeRequest(KillRequest {});
        return MakeSuccess();
    }

    // -------------------- SlogCommand --------------------
    const char* SlogCommand::Name() const {
        return "slog";
    }
    const char* SlogCommand::Description() const {
        return "查看服务 journal 日志";
    }

    void SlogCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--name,-n", mServiceName, "服务名称")->required();
        cmd->add_option("--count", mLogCount, "日志行数（默认10）");
    }

    ResultMsg SlogCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req = MakeRequest(SlogRequest {mServiceName, mLogCount});
        return MakeSuccess();
    }

    // -------------------- CheckCommand --------------------
    const char* CheckCommand::Name() const {
        return "check";
    }
    const char* CheckCommand::Description() const {
        return "设备自检";
    }

    void CheckCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--config", mConfigPath, "自检配置文件路径");
    }

    ResultMsg CheckCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        req = MakeRequest(CheckRequest {mConfigPath});
        return MakeSuccess();
    }

    // -------------------- InitNginxCommand --------------------
    const char* InitNginxCommand::Name() const {
        return "init_nginx";
    }
    const char* InitNginxCommand::Description() const {
        return "独立配置 nginx（使静态文件和反向代理配置生效）";
    }

    void InitNginxCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("--dir,-d", mDir, "nginx 配置源路径（目录或 tar.gz，支持相对路径）")->required();
    }

    ResultMsg InitNginxCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        // 将相对路径转换为绝对路径
        std::string absDir = mDir.empty() ? mDir : std::filesystem::absolute(mDir).string();
        req = MakeRequest(InitNginxRequest {absDir});
        return MakeSuccess();
    }

    // -------------------- ResetNginxCommand --------------------
    const char* ResetNginxCommand::Name() const {
        return "reset_nginx";
    }
    const char* ResetNginxCommand::Description() const {
        return "重置 nginx 配置（-wait 等待404/-now 恢复正常/-back 全部无效）";
    }

    void ResetNginxCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_flag("--wait,-w", mWait, "等待状态：所有路由返回 404（服务升级期间使用）");
        cmd->add_flag("--now,-n", mNow, "恢复正常：使 scm_*.conf 生效，移除 waiting 配置");
        cmd->add_flag("--back,-b", mBack, "全部无效：只能访问 nginx 欢迎页");
    }

    ResultMsg ResetNginxCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }

        // 互斥校验：三个标志只能指定一个
        int flagCount = (mWait ? 1 : 0) + (mNow ? 1 : 0) + (mBack ? 1 : 0);
        if (flagCount == 0) {
            return MakeError("reset_nginx requires one of: --wait, --now, --back");
        }
        if (flagCount > 1) {
            return MakeError("reset_nginx flags are mutually exclusive: --wait, --now, --back");
        }

        NginxResetMode mode {NginxResetMode::BACK};
        if (mWait) {
            mode = NginxResetMode::WAIT;
        } else if (mNow) {
            mode = NginxResetMode::NORMAL;
        }

        req = MakeRequest(ResetNginxRequest {mode});
        return MakeSuccess();
    }

    // -------------------- AddModelCommand --------------------
    const char* AddModelCommand::Name() const {
        return "add_model";
    }
    const char* AddModelCommand::Description() const {
        return "安装/升级模型文件（停止依赖服务→替换模型→验证10s→恢复服务状态）";
    }

    void AddModelCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        // 同时支持位置参数和 -d/--dir 选项，至少需要一个非空输入
        cmd->add_option("src,-d,--dir", mSrcPath, "模型源路径（目录或 tar/tar.gz，支持相对路径）")->required();
    }

    ResultMsg AddModelCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        // 将相对路径转换为绝对路径，便于 scmd 端定位文件
        std::string absPath = mSrcPath.empty() ? mSrcPath : std::filesystem::absolute(mSrcPath).string();
        req = MakeRequest(AddModelRequest {absPath});
        return MakeSuccess();
    }

    // -------------------- ClearModelCommand --------------------
    const char* ClearModelCommand::Name() const {
        return "clear_model";
    }
    const char* ClearModelCommand::Description() const {
        return "清除模型（删除指定模型并验证依赖服务无影响）";
    }

    void ClearModelCommand::Setup(CLI::App &app) {
        auto* cmd = app.add_subcommand(Name(), Description());
        cmd->add_option("-n,--name", mModelName, "模型名（model_dir 下的目录名）")->required();
    }

    ResultMsg ClearModelCommand::BuildRequest(CLI::App &app, ScmRequest &req) {
        if (!app.got_subcommand(Name())) {
            return ResultMsg(2, "");
        }
        if (mModelName.empty()) {
            return MakeError("clear_model requires --name");
        }
        req = MakeRequest(ClearModelRequest {mModelName});
        return MakeSuccess();
    }

    // -------------------- CLI 命令自注册 --------------------
    // 各命令通过宏自动注册到 CliCommandRegistry，新增命令只需在实现末尾添加 REGISTER_CLI_COMMAND 宏
    REGISTER_CLI_COMMAND(InstallCommand)
    REGISTER_CLI_COMMAND(StartCommand)
    REGISTER_CLI_COMMAND(StopCommand)
    REGISTER_CLI_COMMAND(RestartCommand)
    REGISTER_CLI_COMMAND(UpgradeCommand)
    REGISTER_CLI_COMMAND(UpgradesCommand)
    REGISTER_CLI_COMMAND(ListCommand)
    REGISTER_CLI_COMMAND(InfoCommand)
    REGISTER_CLI_COMMAND(LogCommand)
    REGISTER_CLI_COMMAND(UninstallCommand)
    REGISTER_CLI_COMMAND(ReloadCommand)
    REGISTER_CLI_COMMAND(KillCommand)
    REGISTER_CLI_COMMAND(SlogCommand)
    REGISTER_CLI_COMMAND(CheckCommand)
    REGISTER_CLI_COMMAND(InitNginxCommand)
    REGISTER_CLI_COMMAND(ResetNginxCommand)
    REGISTER_CLI_COMMAND(AddModelCommand)
    REGISTER_CLI_COMMAND(ClearModelCommand)

}  // namespace qifeng::scm
