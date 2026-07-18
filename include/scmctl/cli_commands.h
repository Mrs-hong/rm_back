/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once
#include "CLI/CLI.hpp"
#include "common/types.h"
#include "ipc/data_def.h"

#include <string>

using CLI::App;

namespace qifeng::scm {

    /**
     * @brief CLI 子命令抽象基类
     * @details 每个子命令独立负责参数注册和 ScmRequest 组装
     */
    class CliCommand {
    public:
        virtual ~CliCommand() = default;
        CliCommand() = default;
        CliCommand(const CliCommand &) = delete;
        CliCommand &operator=(const CliCommand &) = delete;
        CliCommand(CliCommand &&) = delete;
        CliCommand &operator=(CliCommand &&) = delete;

    public:
        /**
         * @brief 获取命令名称（如 "install"）
         */
        virtual const char* Name() const = 0;

        /**
         * @brief 获取命令描述
         */
        virtual const char* Description() const = 0;

        /**
         * @brief 向 CLI::App 注册子命令和参数
         * @param app 父级 CLI::App
         */
        virtual void Setup(CLI::App &app) = 0;

        /**
         * @brief 若该子命令被命中，组装 ScmRequest
         * @param app 已解析的 CLI::App
         * @param req 待填充的请求
         * @return ResultMsg
         *         code=0: 命中且组装成功
         *         code=-1: 命中但组装失败，msg 包含错误信息
         *         code=2: 未命中该子命令
         */
        virtual ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) = 0;
    };

    class InstallCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mServiceName;
        std::string mDir;  // tar包目录（支持相对路径，发送前转为绝对路径）
    };

    class StartCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mServiceName;
    };

    class StopCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mServiceName;
    };

    class RestartCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mServiceName;
        bool mAll = false;
    };

    class UpgradeCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mServiceName;
        std::string mDir;  // tar包目录（支持相对路径，发送前转为绝对路径）
    };

    /**
     * @brief upgrades 子命令：一体化升级（服务+模型+Nginx）
     * @details -n 必填指定服务名。可选 -d 指定外部升级素材目录/tar包，
     *          不提供 -d 时从服务配置的 upgrade.soft_dir 查找素材。
     *          支持三类素材：服务包(<serviceName>*.tar.gz)、模型(model*前缀)、nginx配置(nginx*前缀目录)。
     *          流程：reset_nginx -w → add_model(排除当前服务) → 升级服务 → 更新nginx/reset_nginx -n。
     */
    class UpgradesCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mServiceName;  // 服务名称（必需）
        std::string mDir;          // 外部升级素材目录/tar包（可选，不提供则用服务内部 soft_dir）
    };

    class ListCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;
    };

    class InfoCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mServiceName;
        bool mShowError {false};  // 是否显示错误原因（崩溃、启动失败、未启动等）
    };

    class LogCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        int mLogType = 0;
        int mLogCount = 50;
    };

    class UninstallCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mServiceName;
    };

    class ReloadCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mServiceName;
        bool mAll = false;
    };

    class KillCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;
    };

    /**
     * @brief slog 子命令：查看服务日志
     * @details 优先读取服务日志文件（<logsDir>/<serviceName>/<serviceName>.log），
     *          文件不存在时回退读取 systemd journal。--name 省略时查 scmd 自身日志
     *          （<logsDir>/qifeng-scm/qifeng-scm.log，与 scmd.log 隔离）。
     */
    class SlogCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mServiceName;  // 服务名称（空表示 scmd 自身）
        int mLogCount {10};        // 日志行数，默认 10 行
    };

    /**
     * @brief check 子命令：触发设备自检
     * @details 通过 scmd 调用 CheckerRunner 执行设备自检，返回各检查项结果。
     */
    class CheckCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mConfigPath;
    };

    /**
     * @brief init_nginx 子命令：独立配置 nginx
     * @details 将指定目录或 tar.gz 中的 nginx 配置安装到系统 nginx 管理目录下，
     *          集成到系统 nginx 的 conf.d/snippets 目录，使静态文件和反向代理配置生效。
     */
    class InitNginxCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mDir;  // nginx 配置源路径（目录或 tar.gz）
    };

    /**
     * @brief reset_nginx 子命令：重置 nginx 配置
     * @details 三种模式可切换：
     *          -wait:  等待状态（所有路由返回 404，用于服务升级期间）
     *          -now:   恢复正常（使 scm_*.conf 生效，移除 waiting 配置）
     *          -back:  全部无效（只能访问 nginx 欢迎页）
     */
    class ResetNginxCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        bool mWait {false};  // -wait 等待状态
        bool mNow {false};   // -now 恢复正常
        bool mBack {false};   // -back 全部无效
    };

    /**
     * @brief add_model 子命令：安装/升级模型文件
     * @details 将指定目录或 tar/tar.gz 包中的模型安装到 scmd.yaml 配置的 model_dir 下，
     *          自动停止依赖模型的服务，验证运行 10s 无影响后完成，否则回退模型。
     */
    class AddModelCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mSrcPath;  // 模型源路径（目录或 tar/tar.gz 包，支持相对路径，发送前转为绝对路径）
    };

    /**
     * @brief clear_model 子命令：停用并备份模型
     * @details 将 model_dir 下指定模型重命名为 <name>.back，
     *          验证依赖服务无影响后完成，否则回退。
     */
    class ClearModelCommand : public CliCommand {
    public:
        const char* Name() const override;
        const char* Description() const override;
        void Setup(CLI::App &app) override;
        ResultMsg BuildRequest(CLI::App &app, ScmRequest &req) override;

    private:
        std::string mModelName;  // 模型名（model_dir 下的文件或目录名）
    };

}  // namespace qifeng::scm
