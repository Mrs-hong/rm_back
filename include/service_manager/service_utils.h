/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>

namespace qifeng::scm {
    /**
     * @brief 服务管理共享底层工具函数命名空间
     * @details 从 ServiceManager 中提取的纯工具函数（不依赖实例状态），
     *          供 ServiceManager 自身及 NginxManager/ModelManager/UpgradeService
     *          等领域管理器复用，避免重复实现。
     */
    namespace service_utils {
        /**
         * @brief 将服务名转换为 systemd 单元名（添加 scmd_ 前缀）
         * @param serviceName 服务名称
         * @return std::string systemd 单元名
         */
        std::string ToSystemdUnitName(const std::string &serviceName);

        /**
         * @brief 判断升级输入是 tar 包还是已解压目录
         * @param path 输入路径
         * @return bool true 表示是 .tar.gz/.tgz 包，false 表示其他
         */
        bool IsTarPackage(const std::string &path);
    }  // namespace service_utils
}  // namespace qifeng::scm
