/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include "common/scmd_types.h"
#include "common/types.h"
#include "ipc/data_def.h"
#include <string>

namespace qifeng::scm {
    class ServiceContext;
    class ServiceManager;

    /**
     * @brief Nginx 配置管理器
     * @details 从 ServiceManager 中提取的 nginx 配置管理领域逻辑，
     *          负责独立配置 nginx（InitNginx）和重置 nginx 配置（ResetNginx）。
     *          持有 ServiceContext 引用以访问 FileManager 等共享依赖，
     *          持有 ServiceManager 引用以调用共享辅助方法（解压、临时目录清理等）。
     */
    class NginxManager {
    public:
        /**
         * @brief 构造函数
         * @param ctx 共享依赖上下文（需由调用方保活）
         * @param serviceManager 服务管理器引用（用于调用共享辅助方法）
         */
        NginxManager(const ServiceContext &ctx, ServiceManager &serviceManager);

        ~NginxManager();

        NginxManager(const NginxManager &) = delete;
        NginxManager &operator=(const NginxManager &) = delete;
        NginxManager(NginxManager &&) = delete;
        NginxManager &operator=(NginxManager &&) = delete;

        /**
         * @brief 独立配置 nginx
         * @details 将指定路径（目录或 tar.gz）中的 nginx 配置安装到系统 nginx 管理目录，
         *          集成到系统 nginx 的 conf.d/snippets 目录，使静态文件和反向代理配置生效。
         *          配置测试失败时自动回退。
         * @param dirPath nginx 配置源路径（目录或 tar.gz）
         * @return ResultMsg 操作结果
         */
        ResultMsg InitNginx(const std::string &dirPath);

        /**
         * @brief 重置 nginx 配置
         * @details 三种模式：
         *          WAIT: 安装 waiting.conf，所有路由返回 404（服务升级期间）
         *          NORMAL: 移除 waiting.conf，恢复 scm_*.conf 生效
         *          BACK: 移除所有 scm 配置和 nginx 目录，恢复系统默认欢迎页
         * @param mode 重置模式
         * @return ResultMsg 操作结果
         */
        ResultMsg ResetNginx(NginxResetMode mode);

    private:
        const ServiceContext &mCtx;
        ServiceManager &mServiceManager;
    };
}  // namespace qifeng::scm