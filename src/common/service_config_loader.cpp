/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/config.h"
#include "common/scmd_def.h"
#include "common/scmd_types.h"
#include "common/types.h"
#include "common/utils/path.h"
#include "common/utils/user.h"
#include "common/utils/yaml_resolve.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <regex>
#include <string>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace {  // 辅助函数
    // 辅助函数：验证路径不包含父目录引用
    bool IsValidRelativePath(const std::string &path) {
        return path.find("..") == std::string::npos;
    }

    /**
     * @brief 验证版本号格式是否合规
     * @param version 版本号字符串
     * @return bool 是否合规
     */
    bool IsValidVersionFormat(const std::string &version) {
        return std::regex_match(version, std::regex(R"(\d+\.\d+\.\d+)"));
    }

    /**
     * @brief 根据服务名称获取数据库类型
     * @param serviceName 服务名称
     * @return qifeng::scm::DatabaseType 数据库类型
     */
    [[maybe_unused]] qifeng::scm::DatabaseType GetDbTypeFromServiceName(const std::string &serviceName) {
        // 使用模糊匹配带有mariadb、mysql、gauss字符串、不区分大小写
        std::string lowerName;
        lowerName.reserve(serviceName.size());
        for (char ch : serviceName) {
            lowerName.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }

        if (lowerName.find("mariadb") != std::string::npos || lowerName.find("mysql") != std::string::npos) {
            return qifeng::scm::DatabaseType::MYSQL;
        }
        if (lowerName.find("gauss") != std::string::npos) {
            return qifeng::scm::DatabaseType::OPENGAUSS;
        }
        return qifeng::scm::DatabaseType::NONE;
    }

    /**
     * @brief 从YAML文件初始化服务定义
     * @param yamlPath YAML文件路径
     * @return qifeng::scm::ServiceDefinition 服务定义
     */
    // 函数大小和复杂度超过阈值，但符合业务逻辑
    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    qifeng::scm::ServiceDefinition InitServiceDefinitionFromYAML(const std::string &yamlPath) {
        qifeng::scm::utils::YamlResolve resolver;
        auto initRet = resolver.Init(yamlPath);
        if (!initRet.IsDefaultSuccess()) {
            throw std::runtime_error("Failed to load service.yaml: " + initRet.msg);
        }

        // 验证关键字段
        auto serviceNameOpt = resolver.GetOptionalNodeValue<std::string>("serviceName");
        auto versionOpt = resolver.GetOptionalNodeValue<std::string>("version");
        if (!serviceNameOpt || !versionOpt) {
            throw std::runtime_error("Invalid service definition: missing required fields");
        }

        qifeng::scm::ServiceDefinition def;
        def.serviceName = *serviceNameOpt;
        if (def.serviceName.empty()) {
            throw std::runtime_error("Service name is empty");
        }

        def.version = *versionOpt;

        // 版本号规定x.x.x格式
        // 验证版本号格式是否合规
        if (!IsValidVersionFormat(def.version)) {
            throw std::runtime_error("Invalid version format: must be x.x.x");
        }

        // 解析 execution 对象
        auto commandOpt = resolver.GetOptionalNodeValue<std::string>("execution.command");
        if (!commandOpt) {
            throw std::runtime_error("Missing required field: execution.command");
        }
        def.execInfo.command = *commandOpt;
        if (def.execInfo.command.empty() || !IsValidRelativePath(def.execInfo.command)) {
            throw std::runtime_error("Invalid execution.command: must be non-empty and cannot contain '..'");
        }

        // 解析可选字段
        if (auto workDirOpt = resolver.GetOptionalNodeValue<std::string>("execution.workDir")) {
            if (!IsValidRelativePath(*workDirOpt)) {
                throw std::runtime_error("Invalid execution.workDir: cannot contain '..'");
            }
            def.execInfo.workDir = *workDirOpt;
        }

        if (auto dataDirOpt = resolver.GetOptionalNodeValue<std::string>("execution.dataDir")) {
            if (!IsValidRelativePath(*dataDirOpt)) {
                throw std::runtime_error("Invalid execution.dataDir: cannot contain '..'");
            }
            def.execInfo.dataDir = *dataDirOpt;
        }

        def.execInfo.gracefulStopSignal = resolver.GetNodeValue<int>("execution.exitSignal", 15);
        def.execInfo.timeoutStopSec = resolver.GetNodeValue<uint32_t>("execution.timeoutStopSec", 5);

        def.isAutoStart = resolver.GetNodeValue<bool>("autoStart", false);
        def.needModel = resolver.GetNodeValue<bool>("need_model", false);

        // 解析 model_link_dir：模型文件软链接路径（相对路径，基于 currentServiceDir）
        if (auto modelLinkDirOpt = resolver.GetOptionalNodeValue<std::string>("model_link_dir")) {
            if (!IsValidRelativePath(*modelLinkDirOpt)) {
                throw std::runtime_error("Invalid model_link_dir: cannot contain '..'");
            }
            def.modelLinkDir = *modelLinkDirOpt;
        }

        // 解析 keep_alive_time_sec：安装/模型变更后服务需保持运行的验证时长（秒）
        // 默认 3 秒，0 表示不验证，范围 [0, 30]，超出范围按边界值修正
        int keepAlive = resolver.GetNodeValue<int>("keep_alive_time_sec", 3);
        if (keepAlive < 0) {
            keepAlive = 0;
        } else if (keepAlive > 30) {
            keepAlive = 30;
        }
        def.keepAliveTimeSec = static_cast<uint32_t>(keepAlive);

        // 解析 upgrade 段（可选）：升级软件包存放目录和升级结果文件路径
        if (resolver.HasNode("upgrade")) {
            if (auto softDirOpt = resolver.GetOptionalNodeValue<std::string>("upgrade.soft_dir")) {
                if (!IsValidRelativePath(*softDirOpt)) {
                    throw std::runtime_error("Invalid upgrade.soft_dir: cannot contain '..'");
                }
                def.upgradeConfig.softDir = *softDirOpt;
            }
            if (auto resultPathOpt = resolver.GetOptionalNodeValue<std::string>("upgrade.result_path")) {
                if (!IsValidRelativePath(*resultPathOpt)) {
                    throw std::runtime_error("Invalid upgrade.result_path: cannot contain '..'");
                }
                def.upgradeConfig.resultPath = *resultPathOpt;
            }
        }

        // 解析数据库配置
        if (auto sqlDirOpt = resolver.GetOptionalNodeValue<std::string>("initDB_sql_dir")) {
            if (!IsValidRelativePath(*sqlDirOpt)) {
                throw std::runtime_error("Invalid sqlDir: cannot contain '..'");
            }
            def.dbInfo.sqlDir = *sqlDirOpt;
            def.dbInfo.outputDir = resolver.GetNodeValue<std::string>("db_output_dir", "");
        }

        // 解析资源信息 (resources)
        try {
            if (resolver.HasNode("resources")) {
                // 解析端口列表 (端口范围 1-65535)
                auto ports = resolver.GetListValues<int>("resources.ports");
                for (int portVal : ports) {
                    if (portVal >= 1 && portVal <= 65535) {
                        def.resourcesInfo.ports.push_back(portVal);
                    }
                }

                // 解析内存限制 "500M" 格式 (systemd: K/M/G/T后缀，无上限)
                std::string memStr = resolver.GetNodeValue<std::string>("resources.Mem", "");
                if (!memStr.empty() && memStr.back() == 'M') {
                    memStr.pop_back();
                    try {
                        int memMB = std::stoi(memStr);
                        if (memMB > 0) {
                            def.resourcesInfo.memoryMB = memMB;
                        }
                    } catch (...) {
                    }
                }

                // 解析CPU限制 (systemd: 百分比，可>100%表示多核)
                int cpu = resolver.GetNodeValue<int>("resources.CPU", 0);
                if (cpu >= 0 && cpu <= 100000) {
                    def.resourcesInfo.cpuPercent = cpu;
                }

                // 解析依赖列表 (requires 为对象数组，需通过 GetNode 遍历)
                auto requiresOpt = resolver.GetNode("resources.requires");
                if (requiresOpt) {
                    for (const auto &req : *requiresOpt) {
                        if (req["serviceName"]) {
                            std::string depName = req["serviceName"].as<std::string>();
                            std::string depVersion = req["version"].as<std::string>("");
                            if (def.serviceName != depName && !def.dependencies.insert({depName, depVersion}).second) {
                                throw std::runtime_error("Duplicate dependency: " + depName);
                            }
                        }
                    }
                }
            }
        } catch (const YAML::Exception &e) {
            throw std::runtime_error(std::string("Failed to parse resources: ") + e.what());
        }

        {
            // 查看数据库的依赖
            qifeng::scm::DatabaseType dbType {qifeng::scm::DatabaseType::NONE};
            auto it = std::find_if(def.dependencies.begin(), def.dependencies.end(), [&dbType](const auto &dep) {
                dbType = GetDbTypeFromServiceName(dep.first);
                return dbType != qifeng::scm::DatabaseType::NONE;
            });
            if (it != def.dependencies.end()) {
                def.dbInfo.dbType = dbType;
            }
        }
        def.isUseful = true;
        return def;
    }
}  // namespace

namespace qifeng {
    namespace scm {
        // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
        ResultMsg ConfigLoader::ScanServicesDirectory(const std::string &servicesDir) {
            mServices.clear();
            if (!fs::exists(servicesDir)) {
                return MakeWarning("Services directory does not exist: " + servicesDir);
            }

            try {
                for (const auto &entry : fs::directory_iterator(servicesDir)) {
                    if (!entry.is_directory()) {
                        continue;
                    }

                    std::string serviceDir = entry.path().string();
                    std::string yamlPath = utils::JoinPath(serviceDir, DefaultServiceName);

                    if (!fs::exists(yamlPath)) {
                        std::cerr << "Skipping service without " << DefaultServiceName << ": " << serviceDir
                                  << std::endl;
                        continue;
                    }

                    try {
                        auto def = InitServiceDefinitionFromYAML(yamlPath);
                        if (def.isUseful && mServices.find(def.serviceName) == mServices.end()) {
                            def.currentServiceDir =
                                utils::JoinPath(mConfigInfo.serviceDir, entry.path().filename().string());
                            def.execInfo.user = utils::GetCurrentUserName();
                            mServices[def.serviceName] = def;
                        }
                    } catch (const std::exception &e) {
                        std::cerr << "Failed to parse service.yaml in " << serviceDir << ": " << e.what() << std::endl;
                    }
                }
                return MakeSuccess();
            } catch (const std::exception &e) {
                return MakeError(e.what());
            }
        }

        ResultMsg ConfigLoader::GetServiceDefFromPath(const std::string &servicePath,
                                                      [[maybe_unused]] ServiceDefinition &serviceDef) {
            if (!mInitialized) {
                return MakeError("ConfigLoader is not initialized");
            }
            if (!fs::exists(servicePath)) {
                return MakeError("Service path does not exist: " + servicePath);
            }
            try {
                serviceDef = InitServiceDefinitionFromYAML(servicePath);
                return MakeSuccess();
            } catch (const std::exception &e) {
                return MakeError(e.what());
            }
        }

        std::vector<ServiceDefinition> ConfigLoader::GetAllServices() const {
            std::vector<ServiceDefinition> services;
            for (const auto &svc : mServices) {
                services.push_back(svc.second);
            }
            return services;
        }

        const ServiceDefinition* ConfigLoader::GetServiceByName(const std::string &serviceName) const {
            auto it = mServices.find(serviceName);
            if (it != mServices.end()) {
                return &it->second;
            }
            return nullptr;
        }

        ServiceDefinition* ConfigLoader::GetServiceByName(const std::string &serviceName) {
            auto it = mServices.find(serviceName);
            if (it != mServices.end()) {
                return &it->second;
            }
            return nullptr;
        }

        ResultMsg ConfigLoader::AddService(const std::string &softwareDir) {
            if (!mInitialized) {
                return MakeError("ConfigLoader is not initialized");
            }

            if (!fs::exists(softwareDir)) {
                return MakeError("Software directory does not exist: " + softwareDir);
            }

            std::string yamlPath = utils::JoinPath(softwareDir, DefaultServiceName);
            if (!fs::exists(yamlPath)) {
                return MakeError("Service.yaml not found in: " + softwareDir);
            }

            try {
                auto def = InitServiceDefinitionFromYAML(yamlPath);
                if (def.isUseful && mServices.find(def.serviceName) == mServices.end()) {
                    def.currentServiceDir =
                        utils::JoinPath(mConfigInfo.serviceDir, fs::path(softwareDir).filename().string());
                    mServices[def.serviceName] = def;
                    std::cout << "Added service: " << def.serviceName << " from " << yamlPath << std::endl;
                    return MakeSuccess();
                }
                return MakeWarning("Service already exists: " + def.serviceName);
            } catch (const std::exception &e) {
                std::cerr << "Failed to parse service.yaml in " << softwareDir << ": " << e.what() << std::endl;
                return MakeError(e.what());
            }
        }

        ResultMsg ConfigLoader::ReloadService(const std::string &serviceName) {
            auto* svc = const_cast<ServiceDefinition*>(GetServiceByName(serviceName));
            if (!svc) {
                return MakeError("Service not found: " + serviceName);
            }
            // 先保存目录路径，再移除服务，避免 svc 指针在 RemoveService 后失效
            std::string currentDir = svc->currentServiceDir;
            RemoveService(serviceName);
            // currentServiceDir 存储的是绝对路径，直接使用即可
            return AddService(currentDir);
        }

        ResultMsg ConfigLoader::RemoveService(const std::string &serviceName) {
            auto it = mServices.find(serviceName);
            if (it != mServices.end()) {
                mServices.erase(it);
                std::cout << "Removed service: " << serviceName << std::endl;
                return MakeSuccess();
            }
            return MakeError("Service not found: " + serviceName);
        }

        ResultMsg ConfigLoader::UpgradeService(const std::string &softwareDir) {
            std::string yamlPath = utils::JoinPath(softwareDir, DefaultServiceName);
            // 直接复用 YamlResolve 解析 serviceName，避免重复的 LoadFile 逻辑
            std::string serviceName = utils::ReadServiceName(yamlPath);
            if (serviceName.empty()) {
                return MakeError("Service name is empty or failed to parse service.yaml: " + yamlPath);
            }
            // 先移除旧版本配置，再添加新版本，确保配置完全更新
            RemoveService(serviceName);
            return AddService(softwareDir);
        }

        std::string ConfigLoader::GetServiceRootDir(const std::string &serviceName) const {
            auto* svc = GetServiceByName(serviceName);
            if (!svc) {
                return "";
            }
            return svc->currentServiceDir;
        }

    }  // namespace scm
}  // namespace qifeng
