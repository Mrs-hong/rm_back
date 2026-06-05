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
        std::string mTarDir;
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
        std::string mTarDir;
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

}  // namespace qifeng::scm
