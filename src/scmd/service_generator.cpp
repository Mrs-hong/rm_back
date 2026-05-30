/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "common/scmd_types.h"
#include "common/utils.h"
#include "service_manger/service_generator.h"
#include <filesystem>
#include <sstream>
namespace {  // 工具函数拆分、降低单函数复杂度
    using qifeng::scm::ServiceDefinition;

    // 将依赖列表格式化为空格分隔的 service 单元名列表
    std::string FormatDependencyList(const std::map<std::string, std::string> &deps) {
        std::ostringstream oss;
        bool first = true;
        for (const auto &dep : deps) {
            if (!first) {
                oss << " ";
            }
            // 依赖格式：<服务名称, 版本号>，映射为 service_name.service
            oss << dep.first << ".service";
            first = false;
        }
        return oss.str();
    }

    // 写入 [Unit] 段
    void WriteUnitSection(std::ostringstream &oss, const ServiceDefinition &def) {
        oss << "[Unit]\n";
        // Description: 服务名称 + 版本号
        oss << "Description=" << def.serviceName;
        if (!def.version.empty()) {
            oss << " " << def.version;
        }
        oss << "\n";

        // After: 依赖服务启动顺序（确保依赖服务先启动）
        // Requires: 强制依赖，依赖服务必须成功启动
        if (!def.dependencies.empty() && def.useful) {
            oss << "After=" << FormatDependencyList(def.dependencies) << "\n";
            oss << "Requires=" << FormatDependencyList(def.dependencies) << "\n";
        }

        oss << "\n";
    }

    // 写入 [Service] 段：执行配置（Type, ExecStart, WorkingDirectory, User）
    void WriteServiceExecConfig(std::ostringstream &oss, const ServiceDefinition &def) {
        oss << "[Service]\n";
        // Type: 简单进程类型，systemd 认为 ExecStart 启动的进程即为服务主进程
        oss << "Type=simple\n";

        // ExecStart: 可执行文件路径 + 命令行参数
        oss << "ExecStart=" << def.execInfo.command;
        for (const auto &arg : def.execInfo.args) {
            oss << " " << arg;
        }
        oss << "\n";

        // WorkingDirectory: 工作目录
        if (!def.execInfo.workDir.empty()) {
            oss << "WorkingDirectory=" << def.execInfo.workDir << "\n";
        }

        // User: 运行用户
        if (!def.execInfo.user.empty()) {
            oss << "User=" << def.execInfo.user << "\n";
        }
    }

    // 写入 [Service] 段：环境变量（env, DATA_DIR, SERVICE_DIR）
    void WriteServiceEnvironment(std::ostringstream &oss, const ServiceDefinition &def) {
        // Environment: 环境变量
        for (const auto &env : def.execInfo.env) {
            oss << "Environment=" << env << "\n";
        }

        // Environment: 数据目录作为 DATA_DIR 环境变量
        if (!def.execInfo.dataDir.empty()) {
            oss << "Environment=DATA_DIR=" << def.execInfo.dataDir << "\n";
        }

        // Environment: 当前服务目录作为 SERVICE_DIR 环境变量
        if (!def.currentServiceDir.empty()) {
            oss << "Environment=SERVICE_DIR=" << def.currentServiceDir << "\n";
        }
    }

    // 写入 [Service] 段：进程控制与资源限制（KillSignal, Restart, MemoryMax, CPUQuota）
    void WriteServiceResourceConfig(std::ostringstream &oss, const ServiceDefinition &def) {
        // KillSignal: 优雅停止信号（默认 SIGTERM=15）
        oss << "KillSignal=" << def.execInfo.gracefulStopSignal << "\n";

        // Restart: 服务异常退出时自动重启
        oss << "Restart=on-failure\n";

        // 资源限制：内存
        if (def.resourcesInfo.memoryMB > 0) {
            oss << "MemoryMax=" << def.resourcesInfo.memoryMB << "M\n";
        }

        // 资源限制：CPU 配额百分比
        if (def.resourcesInfo.cpuPercent > 0) {
            oss << "CPUQuota=" << def.resourcesInfo.cpuPercent << "%\n";
        }

        oss << "\n";
    }

    // 写入 [Install] 段
    void WriteInstallSection(std::ostringstream &oss, const ServiceDefinition &def) {
        oss << "[Install]\n";
        // WantedBy: 自动启动目标（isAutoStart 为 true 时启用）
        if (def.isAutoStart) {
            oss << "WantedBy=multi-user.target\n";
        }
    }

    // 校验输入参数合法性
    qifeng::scm::ResultMsg ValidateParams(const ServiceDefinition &def) {
        // 校验必填字段：服务名称
        if (def.serviceName.empty()) {
            return qifeng::scm::MakeError("Service name is empty");
        }

        // 校验必填字段：可执行命令
        if (def.execInfo.command.empty()) {
            return qifeng::scm::MakeError("Exec command is empty for service: " + def.serviceName);
        }

        // 校验可执行命令路径存在性
        namespace fs = std::filesystem;
        if (!fs::exists(def.execInfo.command)) {
            return qifeng::scm::MakeError("Exec command does not exist: " + def.execInfo.command);
        }

        return qifeng::scm::MakeSuccess();
    }
}  // namespace

namespace qifeng::scm {
    ResultMsg ServiceGenerator::GenerateContent(const ServiceDefinition &serviceDef) {
        try {
            // 参数校验
            auto validateRet = ValidateParams(serviceDef);
            if (!validateRet.IsDefalutSuccess()) {
                return validateRet;
            }

            // 生成 service 文件内容
            std::ostringstream oss;
            WriteUnitSection(oss, serviceDef);
            WriteServiceExecConfig(oss, serviceDef);
            WriteServiceEnvironment(oss, serviceDef);
            WriteServiceResourceConfig(oss, serviceDef);
            WriteInstallSection(oss, serviceDef);
            return ResultMsg {0, oss.str()};
        } catch (const std::exception &e) {
            return MakeError("Failed to generate systemd service file: " + std::string(e.what()));
        }
    }
}  // namespace qifeng::scm