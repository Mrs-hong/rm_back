/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/config.h"
#include "common/scmd_def.h"
#include "common/scmd_types.h"
#include "common/types.h"
#include "common/utils.h"

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
        if (execution["exitSignal"]) {
            def.execInfo.gracefulStopSignal = execution["exitSignal"].as<int>(15);
        }

        if (execution["timeoutStopSec"]) {
            def.execInfo.timeoutStopSec = execution["timeoutStopSec"].as<uint32_t>(5);
        }

        def.isAutoStart = config["autoStart"].as<bool>(false);

        // 解析数据库配置

        if (config["initDB_sql_dir"]) {
            std::string sqlDir = config["initDB_sql_dir"].as<std::string>();
            if (!IsValidRelativePath(sqlDir)) {
                throw std::runtime_error("Invalid sqlDir: cannot contain '..'");
            }
            def.dbInfo.sqlDir = sqlDir;
            if (config["db_output_dir"]) {
                def.dbInfo.outputDir = config["db_output_dir"].as<std::string>();
            }
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
        ConfigLoader::ConfigLoader() {
            // 初始化默认配置
            mConfigInfo.logLevel = LogLevel::INFO;
            mConfigInfo.logFileSizeMB = 50;
            mConfigInfo.logFileCount = 7;
            mConfigInfo.udsSocketPath = "/run/qifeng-scm/scmd.sock";
            mConfigInfo.optTimeoutSec = 10;
            mConfigInfo.configDir = "/etc/qifeng-scm";
            mConfigInfo.serviceDir = "/var/lib/qifeng-scm/services";
            mConfigInfo.dataDir = "/var/lib/qifeng-scm/data";
            mConfigInfo.backupDir = "/var/lib/qifeng-scm/backup";
            mConfigInfo.logsDir = "/var/log/qifeng-scm";
            mConfigInfo.tempDir = "/tmp";
            mInitialized = false;
        }

        ResultMsg ConfigLoader::Initialize(bool isScanServices) {
            // 加载自己配置项
            auto ret = LoadSelfConfigFile();
            if (!ret.IsDefalutSuccess()) {
                std::cerr << "Failed to load self config file: " << ret.msg << std::endl;
                // 使用默认配置
            }

            mInitialized = true;
            // 扫描services目录
            if (isScanServices) {
                return ScanServicesDirectory(mConfigInfo.serviceDir);
            }
            return MakeSuccess();
        }
        // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
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
                if (scmd["uds"]) {
                    if (scmd["uds"]["socket_path"]) {
                        mConfigInfo.udsSocketPath = scmd["uds"]["socket_path"].as<std::string>();
                    }
                    if (scmd["uds"]["socket_mode"]) {
                        // 支持八进制格式如 0666 或十进制格式
                        std::string modeStr = scmd["uds"]["socket_mode"].as<std::string>();
                        mConfigInfo.udsSocketMode = std::stoi(modeStr, nullptr, 8);
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

                // 解析临时目录
                if (scmd["temp_dir"]) {
                    mConfigInfo.tempDir = scmd["temp_dir"].as<std::string>();
                }

                // 解析自检配置
                if (scmd["selftest"]) {
                    YAML::Node selftest = scmd["selftest"];
                    mConfigInfo.selftestEnabled = selftest["enabled"].as<bool>(true);
                    if (selftest["config_path"]) {
                        mConfigInfo.selftestConfigPath = selftest["config_path"].as<std::string>();
                    }
                    if (selftest["fail_action"]) {
                        mConfigInfo.selftestFailAction = selftest["fail_action"].as<std::string>("warn");
                    }
                }

                return MakeSuccess();

            } catch (const YAML::Exception &e) {
                return MakeError(std::string("YAML parse error: ") + e.what());
            } catch (const std::exception &e) {
                return MakeError(std::string("Failed to load self config file: ") + e.what());
            }
        }

        // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
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

                // 组装根节点
                root["scmd"] = YAML::Node(YAML::NodeType::Map);
                root["scmd"]["log"] = log;
                root["scmd"]["uds"] = uds;
                root["scmd"]["opt_timeout_sec"] = mConfigInfo.optTimeoutSec;
                root["scmd"]["service_dir"] = mConfigInfo.serviceDir;
                root["scmd"]["back_dir"] = mConfigInfo.backupDir;
                root["scmd"]["data_dir"] = mConfigInfo.dataDir;

                // 构建自检配置
                YAML::Node selftest;
                selftest["enabled"] = mConfigInfo.selftestEnabled;
                if (!mConfigInfo.selftestConfigPath.empty()) {
                    selftest["config_path"] = mConfigInfo.selftestConfigPath;
                }
                selftest["fail_action"] = mConfigInfo.selftestFailAction;
                root["scmd"]["selftest"] = selftest;

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
                            def.currentServiceDir = utils::JoinPath(mConfigInfo.serviceDir, entry.path().filename().string());
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
                    def.currentServiceDir = utils::JoinPath(mConfigInfo.serviceDir, fs::path(softwareDir).filename().string());
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
            try {
                YAML::Node config = YAML::LoadFile(yamlPath);
                std::string serviceName = config["serviceName"].as<std::string>("");
                if (serviceName.empty()) {
                    return MakeError("Service name is empty in service.yaml");
                }
                // 先移除旧版本配置，再添加新版本，确保配置完全更新
                RemoveService(serviceName);
                return AddService(softwareDir);
            } catch (const std::exception &e) {
                return MakeError("Failed to upgrade service config: " + std::string(e.what()));
            }
        }

        std::string ConfigLoader::GetServiceRootDir(const std::string &serviceName) const {
            auto* svc = GetServiceByName(serviceName);
            if (!svc) {
                return "";
            }
            return svc->currentServiceDir;
        }

        std::string ConfigLoader::GetKeyOptFilePath() const {
            if (!mInitialized) {
                return "";
            }
            return utils::JoinPath(mConfigInfo.dataDir, KeyOptFileName);
        }

    }  // namespace scm
}  // namespace qifeng
