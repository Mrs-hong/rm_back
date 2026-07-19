/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/types.h"
#include "service_manager/service_context.h"

#include <string>

namespace qifeng::scm::service_operations {

    /**
     * @brief 安装服务（含数据库初始化，失败时自动回滚卸载）
     * @details 从 ServiceControl::Installed 提取的跨领域编排逻辑：
     *          1. 调用 ServiceManager::InstallService 安装服务
     *          2. 提取实际服务名（处理 code=1 警告场景）
     *          3. 调用 DatabaseService::InitServiceDatabase 初始化数据库
     *          4. 数据库初始化失败时回滚（调用 UninstallServiceWithCleanup）
     * @param ctx 服务上下文（需包含 serviceManager、databaseService、configLoader）
     * @param serviceName 服务名称
     * @param tarPath 服务 tar 包路径
     * @return ResultMsg 成功返回 code=0；安装成功但启动验证失败返回 code=1（警告）；失败返回 code=-1
     */
    ResultMsg InstallServiceWithDb(const ServiceContext& ctx,
                                    const std::string& serviceName,
                                    const std::string& tarPath);

    /**
     * @brief 卸载服务（含数据库数据清理）
     * @details 从 ServiceControl::UninstallService 提取的跨领域编排逻辑：
     *          1. 校验服务存在
     *          2. 若服务依赖数据库，先清除数据库数据（在删除服务文件前读取密码文件）
     *          3. 调用 ServiceManager::UninstallService 卸载服务
     * @param ctx 服务上下文（需包含 serviceManager、databaseService、configLoader）
     * @param serviceName 服务名称
     * @return ResultMsg 操作结果
     */
    ResultMsg UninstallServiceWithCleanup(const ServiceContext& ctx,
                                           const std::string& serviceName);

    /**
     * @brief 重启所有服务（用于 RESTART_ALL 命令）
     * @details 按依赖逆序停止所有运行中服务，再按依赖顺序启动 autoStart 服务。
     *          从 ServiceControl::RestartAllServices 提取的薄编排逻辑。
     * @param ctx 服务上下文（需包含 serviceManager）
     * @return ResultMsg 全部成功返回成功；任一阶段失败返回警告
     */
    ResultMsg RestartAllServices(const ServiceContext& ctx);

    /**
     * @brief 获取操作日志（用于 LOG 命令）
     * @details 读取 scmd.log 文件的最后 logCount 行。
     *          从 ServiceControl::GetOperationLog 提取。
     * @param ctx 服务上下文（需包含 configLoader）
     * @param logLevel 日志级别过滤（当前未使用，保留接口）
     * @param logCount 日志条数限制（<=0 时返回全部）
     * @return ResultMsg 成功时 msg 为日志内容
     */
    ResultMsg GetOperationLog(const ServiceContext& ctx, int logLevel, int logCount);

    /**
     * @brief 获取服务日志（用于 SLOG 命令）
     * @details 新需求3.1：优先读日志文件，回退 journal。
     *          读取顺序：
     *            1. 优先读取服务日志文件 <logsDir>/<serviceName>/<serviceName>.log 的最后 logCount 行
     *            2. 文件不存在或为空时，回退读取 systemd journal
     * @param ctx 服务上下文（需包含 configLoader）
     * @param serviceName 服务名称（空表示 scmd 自身，读取 <logsDir>/qifeng-scm/qifeng-scm.log）
     * @param logCount 日志行数（<=0 时使用默认 10）
     * @return ResultMsg 成功时 msg 为日志内容
     */
    ResultMsg GetServiceLog(const ServiceContext& ctx, const std::string& serviceName, int logCount);

}  // namespace qifeng::scm::service_operations
