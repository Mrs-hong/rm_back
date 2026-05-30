/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/config.h"
#include "common/scmd_def.h"
#include "common/scmd_types.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
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
     * @brief 从YAML文件初始化服务定义
     * @param yamlPath YAML文件路径
     * @param scmdConfig SCMD配置信息
     * @return qifeng::scm::ServiceDefinition 服务定义
     */
    qifeng::scm::ServiceDefinition InitServiceDefinitionFromYAML(const std::string &yamlPath,
                                                                 const qifeng::scm::ConfigInfo &scmdConfig) {
        YAML::Node config = YAML::LoadFile(yamlPath);

        // 验证关键字段
        if (!config["serviceName"] || !config["version"] || !config["execution"]) {
            throw std::runtime_error("Invalid service definition: missing required fields");
        }

        qifeng::scm::ServiceDefinition def;
        def.serviceName = config["serviceName"].as<std::string>("");
        if (def.serviceName.empty()) {
            throw std::runtime_error("Service name is empty");
        }

        def.version = config["version"].as<std::string>();

        // 版本号规定x.x.x格式
        // 验证版本号格式是否合规
        if (!IsValidVersionFormat(def.version)) {
            throw std::runtime_error("Invalid version format: must be x.x.x");
        }

        // 解析 execution 对象
        YAML::Node execution = config["execution"];
        if (!execution["command"]) {
            throw std::runtime_error("Missing required field: execution.command");
        }
        def.execInfo.command = execution["command"].as<std::string>();
        if (def.execInfo.command.empty() || !IsValidRelativePath(def.execInfo.command)) {
            throw std::runtime_error("Invalid execution.command: must be non-empty and cannot contain '..'");
        }

        // 解析可选字段
        if (execution["workDir"]) {
            std::string workDir = execution["workDir"].as<std::string>();
            if (!IsValidRelativePath(workDir)) {
                throw std::runtime_error("Invalid execution.workDir: cannot contain '..'");
            }
            def.execInfo.workDir = workDir;
        }

        if (execution["dataDir"]) {
            std::string dataDir = execution["dataDir"].as<std::string>();
            if (!IsValidRelativePath(dataDir)) {
                throw std::runtime_error("Invalid execution.dataDir: cannot contain '..'");
            }
            def.execInfo.dataDir = dataDir;
        }

        if (execution["args"]) {
            for (const auto &arg : execution["args"]) {
                def.execInfo.args.push_back(arg.as<std::string>());
            }
        }

        def.isAutoStart = config["autoStart"].as<bool>(false);

        // 解析数据库配置
        if (config["needInitDB"] && config["needInitDB"].as<bool>()) {
            def.dbInfo.dbType = qifeng::scm::DatabaseType::MYSQL;  // 默认MYSQL
            if (config["sqlDir"]) {
                std::string sqlDir = config["sqlDir"].as<std::string>();
                if (!IsValidRelativePath(sqlDir)) {
                    throw std::runtime_error("Invalid sqlDir: cannot contain '..'");
                }
                def.dbInfo.sqlDir = sqlDir;
            }
        }

        // 解析健康检查配置
        if (config["healthCheck"]) {
            YAML::Node health = config["healthCheck"];
            def.healthCheckInfo.intervalSec = health["interval_sec"].as<uint32_t>(scmdConfig.healthCheckIntervalSec);
            def.healthCheckInfo.timeoutSec = health["timeout_sec"].as<uint32_t>(scmdConfig.healthCheckTimeoutSec);
            def.healthCheckInfo.retries = health["retries"].as<uint32_t>(scmdConfig.healthCheckRetries);
            def.healthCheckInfo.httpUrl = health["http_url"].as<std::string>("");
            def.healthCheckInfo.expectedHttpStatus = health["expected_status"].as<int>(200);
        }

        // 解析资源信息 (resources)
        if (config["resources"]) {
            YAML::Node res = config["resources"];
            // 解析端口列表 (端口范围 1-65535)
            if (res["ports"]) {
                for (const auto &port : res["ports"]) {
                    int portVal = port.as<int>();
                    if (portVal >= 1 && portVal <= 65535) {
                        def.resourcesInfo.ports.push_back(portVal);
                    }
                }
            }
            // 解析内存限制 "500M" 格式 (systemd: K/M/G/T后缀，无上限)
            if (res["Mem"]) {
                std::string memStr = res["Mem"].as<std::string>();
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
            }
            // 解析CPU限制 (systemd: 百分比，可>100%表示多核)
            if (res["CPU"]) {
                int cpu = res["CPU"].as<int>(0);
                if (cpu >= 0 && cpu <= 100000) {
                    def.resourcesInfo.cpuPercent = cpu;
                }
            }
            if (res["requires"]) {
                for (const auto &req : res["requires"]) {
                    if (req["serviceName"]) {
                        std::string serviceName = req["serviceName"].as<std::string>();
                        std::string version = req["version"].as<std::string>("");
                        if (def.serviceName != serviceName && !def.dependencies.insert({serviceName, version}).second) {
                            throw std::runtime_error("Duplicate dependency: " + serviceName);
                        }
                    }
                }
            }
        }
        def.useful = true;
        return def;
    }
}  // namespace

namespace qifeng {
    namespace scm {
        ConfigLoader::ConfigLoader() {
            // 初始化默认配置
            mConfigInfo.logLevel = LogLevel::INFO;
            mConfigInfo.logFileSizeMB = 50;
            mConfigInfo.logFileCount = 7;
            mConfigInfo.udsSocketPath = "./scmd.sock";
            mConfigInfo.healthCheckIntervalSec = 20;
            mConfigInfo.healthCheckTimeoutSec = 3;
            mConfigInfo.healthCheckRetries = 3;
            mConfigInfo.healthCheckEnabled = true;
            mConfigInfo.optTimeoutSec = 10;
            mConfigInfo.configDir = "./.config";
            mConfigInfo.serviceDir = "./.services";
            mConfigInfo.dataDir = "./.data";
            mConfigInfo.backupDir = "./.backup";
            mConfigInfo.logsDir = "./.logs";
            mInitialized = false;
        }

        ResultMsg ConfigLoader::Initialize() {
            // 加载自己配置项
            auto ret = LoadSelfConfigFile();
            if (!ret.IsDefalutSuccess()) {
                std::cerr << "Failed to load self config file: " << ret.msg;
                // 使用默认配置
            }

            mInitialized = true;
            // 扫描services目录
            return ScanServicesDirectory(mConfigInfo.serviceDir);
        }

        ResultMsg ConfigLoader::LoadSelfConfigFile() {
            try {
                std::string configPath = DefaultConfigPath;

                // 检查配置文件是否存在
                if (!fs::exists(configPath)) {
                    return MakeError("Config file not found: " + configPath);
                }

                YAML::Node config = YAML::LoadFile(configPath);

                // 检查scmd根节点是否存在
                if (!config["scmd"]) {
                    return MakeError("Invalid config file: missing 'scmd' root node");
                }

                YAML::Node scmd = config["scmd"];

                // 解析日志配置
                if (scmd["log"]) {
                    YAML::Node log = scmd["log"];

                    // 解析日志级别
                    if (log["level"]) {
                        std::string levelStr = log["level"].as<std::string>();
                        if (levelStr == "trace" || levelStr == "debug") {
                            mConfigInfo.logLevel = LogLevel::DEBUG;
                        } else if (levelStr == "warn") {
                            mConfigInfo.logLevel = LogLevel::WARNING;
                        } else if (levelStr == "error") {
                            mConfigInfo.logLevel = LogLevel::ERROR;
                        } else {
                            mConfigInfo.logLevel = LogLevel::INFO;  // 默认info级别
                        }
                    }

                    // 解析日志文件大小（字节转MB）
                    if (log["max_file_size"]) {
                        uint32_t sizeBytes = log["max_file_size"].as<uint32_t>();
                        mConfigInfo.logFileSizeMB = sizeBytes / (1024 * 1024);
                        if (mConfigInfo.logFileSizeMB == 0) {
                            mConfigInfo.logFileSizeMB = 1;  // 最小1MB
                        }
                    }

                    // 解析日志文件数量
                    if (log["max_files"]) {
                        mConfigInfo.logFileCount = log["max_files"].as<uint32_t>();
                    }

                    // 解析日志路径
                    if (log["path"]) {
                        mConfigInfo.logsDir = log["path"].as<std::string>();
                    }
                }

                // 解析UDS配置
                if (scmd["uds"] && scmd["uds"]["socket_path"]) {
                    mConfigInfo.udsSocketPath = scmd["uds"]["socket_path"].as<std::string>();
                }

                // 解析健康检查配置
                if (scmd["health_check"]) {
                    YAML::Node health = scmd["health_check"];

                    if (health["default_interval_sec"]) {
                        mConfigInfo.healthCheckIntervalSec = health["default_interval_sec"].as<uint32_t>();
                    }

                    if (health["default_timeout_sec"]) {
                        mConfigInfo.healthCheckTimeoutSec = health["default_timeout_sec"].as<uint32_t>();
                    }

                    if (health["default_retry_count"]) {
                        mConfigInfo.healthCheckRetries = health["default_retry_count"].as<uint32_t>();
                    }

                    if (health["enable"]) {
                        mConfigInfo.healthCheckEnabled = health["enable"].as<bool>();
                    }
                }

                // 解析操作超时配置
                if (scmd["opt_timeout_sec"]) {
                    mConfigInfo.optTimeoutSec = scmd["opt_timeout_sec"].as<uint32_t>();
                }

                // 解析服务配置目录
                if (scmd["service_dir"]) {
                    mConfigInfo.serviceDir = scmd["service_dir"].as<std::string>();
                }

                // 解析备份目录
                if (scmd["back_dir"]) {
                    mConfigInfo.backupDir = scmd["back_dir"].as<std::string>();
                }

                // 解析数据目录
                if (scmd["data_dir"]) {
                    mConfigInfo.dataDir = scmd["data_dir"].as<std::string>();
                }

                return MakeSuccess();

            } catch (const YAML::Exception &e) {
                return MakeError(std::string("YAML parse error: ") + e.what());
            } catch (const std::exception &e) {
                return MakeError(std::string("Failed to load self config file: ") + e.what());
            }
        }

        ResultMsg ConfigLoader::WriteSelfConfigFile() {
            try {
                // 确保配置目录存在
                std::string configDir = fs::path(DefaultConfigPath).parent_path().string();
                if (!configDir.empty() && !fs::exists(configDir)) {
                    fs::create_directories(configDir);
                }

                YAML::Node root;

                // 构建日志级别字符串
                std::string logLevelStr;
                switch (mConfigInfo.logLevel) {
                    case LogLevel::DEBUG:
                        logLevelStr = "debug";
                        break;
                    case LogLevel::INFO:
                        logLevelStr = "info";
                        break;
                    case LogLevel::WARNING:
                        logLevelStr = "warn";
                        break;
                    case LogLevel::ERROR:
                        logLevelStr = "error";
                        break;
                    case LogLevel::FATAL:
                        logLevelStr = "fatal";
                        break;
                    default:
                        logLevelStr = "info";
                        break;
                }

                // 构建日志配置
                YAML::Node log;
                log["level"] = logLevelStr;
                log["max_file_size"] = mConfigInfo.logFileSizeMB * 1024 * 1024;  // MB转字节
                log["max_files"] = static_cast<int>(mConfigInfo.logFileCount);
                log["path"] = mConfigInfo.logsDir;

                // 构建UDS配置
                YAML::Node uds;
                uds["socket_path"] = mConfigInfo.udsSocketPath;

                // 构建健康检查配置
                YAML::Node healthCheck;
                healthCheck["default_interval_sec"] = mConfigInfo.healthCheckIntervalSec;
                healthCheck["default_timeout_sec"] = mConfigInfo.healthCheckTimeoutSec;
                healthCheck["default_retry_count"] = mConfigInfo.healthCheckRetries;
                healthCheck["enable"] = mConfigInfo.healthCheckEnabled;

                // 组装根节点
                root["scmd"] = YAML::Node(YAML::NodeType::Map);
                root["scmd"]["log"] = log;
                root["scmd"]["uds"] = uds;
                root["scmd"]["health_check"] = healthCheck;
                root["scmd"]["opt_timeout_sec"] = mConfigInfo.optTimeoutSec;
                root["scmd"]["service_dir"] = mConfigInfo.serviceDir;
                root["scmd"]["back_dir"] = mConfigInfo.backupDir;
                root["scmd"]["data_dir"] = mConfigInfo.dataDir;

                // 写入文件
                std::ofstream ofs(DefaultConfigPath);
                if (!ofs.is_open()) {
                    return MakeError("Failed to open config file for writing: " + std::string(DefaultConfigPath));
                }
                ofs << root;
                ofs.close();
                return MakeSuccess();

            } catch (const YAML::Exception &e) {
                return MakeError(std::string("YAML error: ") + e.what());
            } catch (const std::exception &e) {
                return MakeError(std::string("Failed to write self config file: ") + e.what());
            }
        }

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
                    std::string yamlPath = serviceDir + "/" + DefaultServiceName;

                    if (!fs::exists(yamlPath)) {
                        std::cerr << "Skipping service without " << DefaultServiceName << ": " << serviceDir
                                  << std::endl;
                        continue;
                    }

                    try {
                        auto def = InitServiceDefinitionFromYAML(yamlPath, mConfigInfo);
                        if (def.useful && mServices.find(def.serviceName) == mServices.end()) {
                            def.currentServiceDir = entry.path().filename();
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

            std::string yamlPath = softwareDir + "/" + DefaultServiceName;
            if (!fs::exists(yamlPath)) {
                return MakeError("Service.yaml not found in: " + softwareDir);
            }

            try {
                auto def = InitServiceDefinitionFromYAML(yamlPath, mConfigInfo);
                if (def.useful && mServices.find(def.serviceName) == mServices.end()) {
                    def.currentServiceDir = fs::path(softwareDir).filename().string();
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
            RemoveService(serviceName);
            std::string fileDir = mConfigInfo.serviceDir + "/" + svc->currentServiceDir;
            return AddService(fileDir);
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
            // 先添加新版本，然后删除旧版本
            ResultMsg result = AddService(softwareDir);
            if (!result.IsDefalutSuccess()) {
                return result;
            }

            return MakeSuccess();
        }

        std::string ConfigLoader::GetServiceRootDir(const std::string &serviceName) const {
            auto* svc = GetServiceByName(serviceName);
            if (!svc) {
                return "";
            }
            return mConfigInfo.serviceDir + "/" + svc->currentServiceDir;
        }

    }  // namespace scm
}  // namespace qifeng
