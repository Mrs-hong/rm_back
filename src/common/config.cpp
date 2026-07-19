/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/config.h"
#include "common/scmd_def.h"
#include "common/scmd_types.h"
#include "common/types.h"
#include "common/utils/path.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

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
            if (!ret.IsDefaultSuccess()) {
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

        std::string ConfigLoader::GetKeyOptFilePath() const {
            if (!mInitialized) {
                return "";
            }
            return utils::JoinPath(mConfigInfo.dataDir, KeyOptFileName);
        }

    }  // namespace scm
}  // namespace qifeng
