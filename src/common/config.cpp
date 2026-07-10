/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/config.h"
#include "common/scmd_def.h"
#include "common/scmd_types.h"
#include "common/types.h"
#include "common/utils.h"
#include "common/utils/yaml_resolve.h"

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
        qifeng::scm::utils::YamlResolve resolver;
        auto initRet = resolver.Init(yamlPath);
        if (!initRet.IsDefalutSuccess()) {
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
        ConfigLoader::ConfigLoader() {
            // 初始化默认配置
            mConfigInfo.logLevel = LogLevel::INFO;
            mConfigInfo.logFileSizeMB = 50;
            mConfigInfo.logFileCount = 7;
            mConfigInfo.udsSocketPath = "/run/qifeng-scm/scmd.sock";
            mConfigInfo.optTimeoutSec = 10;
            mConfigInfo.configDir = "/etc/qifeng-scm";
            // 基于 rootDir 派生所有子目录
            mConfigInfo.rootDir = "/var/lib/qifeng-scm";
            mConfigInfo.serviceDir = mConfigInfo.rootDir + "/services";
            mConfigInfo.dataDir = mConfigInfo.rootDir + "/data";
            mConfigInfo.backupDir = mConfigInfo.rootDir + "/backup";
            mConfigInfo.logsDir = mConfigInfo.rootDir + "/log";
            mConfigInfo.tempDir = mConfigInfo.rootDir + "/tmp";
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

                // 解析根目录，并基于它派生所有子目录
                if (scmd["root_dir"]) {
                    mConfigInfo.rootDir = scmd["root_dir"].as<std::string>();
                    // 基于 rootDir 派生子目录
                    mConfigInfo.serviceDir = mConfigInfo.rootDir + "/services";
                    mConfigInfo.dataDir = mConfigInfo.rootDir + "/data";
                    mConfigInfo.backupDir = mConfigInfo.rootDir + "/backup";
                    mConfigInfo.tempDir = mConfigInfo.rootDir + "/tmp";
                    // 如果日志路径未在配置中单独指定，则默认也使用 rootDir 下的 log
                    if (!scmd["log"] || !scmd["log"]["path"]) {
                        mConfigInfo.logsDir = mConfigInfo.rootDir + "/log";
                    }
                }

                // 解析模型文件配置
                if (scmd["model_dir"]) {
                    mConfigInfo.modelDir = scmd["model_dir"].as<std::string>();
                }
                if (scmd["model_env_var"]) {
                    mConfigInfo.modelEnvVar = scmd["model_env_var"].as<std::string>();
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
                // 只写入根目录，子目录由程序运行时自动派生
                root["scmd"]["root_dir"] = mConfigInfo.rootDir;
                // 写入模型文件配置（约定不可修改，仅持久化保持配置完整）
                if (!mConfigInfo.modelDir.empty()) {
                    root["scmd"]["model_dir"] = mConfigInfo.modelDir;
                }
                if (!mConfigInfo.modelEnvVar.empty()) {
                    root["scmd"]["model_env_var"] = mConfigInfo.modelEnvVar;
                }

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

        std::string ConfigLoader::GetKeyOptFilePath() const {
            if (!mInitialized) {
                return "";
            }
            return utils::JoinPath(mConfigInfo.dataDir, KeyOptFileName);
        }

    }  // namespace scm
}  // namespace qifeng
