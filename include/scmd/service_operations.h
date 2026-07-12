/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/types.h"
#include "service_manger/service_context.h"

#include <string>

namespace qifeng::scm {

    /**
     * @brief 安装服务（含数据库初始化，失败时自动回滚卸载）
     * @details 从 ServiceContainer::Installed 提取的跨领域编排逻辑：
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
     * @details 从 ServiceContainer::UninstallService 提取的跨领域编排逻辑：
     *          1. 校验服务存在
     *          2. 若服务依赖数据库，先清除数据库数据（在删除服务文件前读取密码文件）
     *          3. 调用 ServiceManager::UninstallService 卸载服务
     * @param ctx 服务上下文（需包含 serviceManager、databaseService、configLoader）
     * @param serviceName 服务名称
     * @return ResultMsg 操作结果
     */
    ResultMsg UninstallServiceWithCleanup(const ServiceContext& ctx,
                                           const std::string& serviceName);

}  // namespace qifeng::scm
