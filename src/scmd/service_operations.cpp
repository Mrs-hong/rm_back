/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/service_operations.h"

#include "common/config.h"
#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/database_service.h"
#include "service_manger/service_manager.h"

namespace qifeng::scm {

    ResultMsg InstallServiceWithDb(const ServiceContext& ctx,
                                    const std::string& serviceName,
                                    const std::string& tarPath) {
        SLOG_INFO << "Installing service: " << serviceName << " from " << tarPath;
        auto result = ctx.serviceManager->InstallService(tarPath, serviceName);
        if (result.code != 0 && result.code != 1) {
            // 安装失败（非警告），直接返回错误
            SLOG_ERROR << "Failed to install service: " << serviceName << " from " << tarPath
                       << ", error: " << result.msg;
            return result;
        }

        // code=0 表示安装成功；code=1 表示安装成功但启动验证失败（警告）
        // 两种情况均需提取纯服务名继续后续处理（数据库初始化等）
        std::string actualServiceName = result.msg;
        std::string verifyWarning;
        if (result.code == 1) {
            // 警告消息格式："<serviceName> installed, but <reason>"
            // 提取服务名（第一个空格前的部分）
            auto spacePos = actualServiceName.find(' ');
            if (spacePos != std::string::npos) {
                verifyWarning = actualServiceName;
                actualServiceName = actualServiceName.substr(0, spacePos);
            }
            SLOG_WARN << "Service installed with verification warning: " << verifyWarning;
        }

        result = ctx.databaseService->InitServiceDatabase(actualServiceName);
        if (!result.IsDefaultSuccess()) {
            // 数据建库操作失败，回滚安装
            UninstallServiceWithCleanup(ctx, actualServiceName);
            return MakeError("install service " + actualServiceName + " failed: " + result.msg);
        }

        // 数据库初始化成功后，若存在验证警告则返回警告（code=1），否则返回成功
        if (!verifyWarning.empty()) {
            return MakeWarning(verifyWarning);
        }
        return MakeSuccess();
    }

    ResultMsg UninstallServiceWithCleanup(const ServiceContext& ctx,
                                           const std::string& serviceName) {
        SLOG_INFO << "Uninstalling service: " << serviceName;
        auto svc = ctx.configLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }

        // 先清除数据库相关（在删除服务文件之前，以便读取密码文件发现所有数据库）
        if (svc->dbInfo.dbType != DatabaseType::NONE) {
            ResultMsg result = ctx.databaseService->ClearDatabaseData(*svc);
            if (result.code == -1) {
                return MakeError("clear database data failed:" + result.msg);
            }
        }

        // 卸载服务（停止服务 + 删除服务文件 + 从配置中移除）
        ResultMsg ret = ctx.serviceManager->UninstallService(serviceName);
        if (ret.code == -1) {
            return MakeError("uninstall service failed:" + ret.msg);
        }
        return ret;
    }

}  // namespace qifeng::scm
