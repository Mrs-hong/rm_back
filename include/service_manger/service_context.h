/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include <memory>
namespace qifeng::scm {
    class ConfigLoader;
    class FileManager;
    class DBusManager;

    /**
     * @brief 共享依赖上下文
     * @details 持有各领域管理器共享的 ConfigLoader、FileManager、DBusManager 智能指针，
     *          由 ServiceManager 构造并提供给 NginxManager/ModelManager/UpgradeService 使用。
     */
    struct ServiceContext {
        std::shared_ptr<ConfigLoader> configLoader;
        std::shared_ptr<FileManager> fileManager;
        std::shared_ptr<DBusManager> dbusManager;
    };
}  // namespace qifeng::scm
