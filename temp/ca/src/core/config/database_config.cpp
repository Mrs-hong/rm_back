//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//
#include "common/config/database_config.h"
#include "qifeng_framework/common/logger.h"
#include <fstream>
#include <string>
namespace {
    // 检查基础配置格式是否正确
    bool CheckDefaultFormat(const std::string &secret) {
        if (secret.empty()) {
            return false;
        }
        // 校验是否存在空格或换行符
        for (auto c : secret) {
            if (c == ' ' || c == '\n' || c == '\t') {
                return false;
            }
        }
        return true;
    }
}  // namespace
namespace qifeng_ca {
    std::string DatabaseConfig::GetMysqlUser() const {
        std::string secretDir = CONFIG_MANAGER.GetString("database", "mysql_start_dir", "data/db_output");
        if (secretDir.back() != '/') {
            secretDir += '/';
        }
        std::string serviceName = CONFIG_MANAGER.GetString("service", "name", "qifeng_ca");
        std::string secretFilePath = secretDir + serviceName;
        SLOG_INFO << "[GetDBUser] secretFilePath: " << secretFilePath;
        std::ifstream ifs(secretFilePath);
        if (!ifs.is_open()) {
            SLOG_ERROR << "[GetDBUser] open secretFilePath failed: " << secretFilePath;
            return "";
        }
        std::string line;
        std::getline(ifs, line);
        if (line.empty()) {
            SLOG_ERROR << "[GetDBUser] user line is empty: " << secretFilePath;
            return "";
        }
        if (!CheckDefaultFormat(line)) {
            SLOG_ERROR << "[GetDBUser] user line format is invalid: " << line;
            return "";
        }
        return line;
    }

    std::string DatabaseConfig::GetMysqlPassword() const {
        std::string secretDir = CONFIG_MANAGER.GetString("database", "mysql_start_dir", "data/db_output");
        if (secretDir.back() != '/') {
            secretDir += '/';
        }
        std::string serviceName = CONFIG_MANAGER.GetString("service", "name", "qifeng_ca");
        std::string secretFilePath = secretDir + serviceName;
        SLOG_INFO << "[GetDBPassword] secretFilePath: " << secretFilePath;
        std::ifstream ifs(secretFilePath);
        if (!ifs.is_open()) {
            SLOG_ERROR << "[GetDBPassword] open secretFilePath failed: " << secretFilePath;
            return "";
        }
        std::string line;
        auto &ret = std::getline(ifs, line);
        if (!ret || line.empty()) {
            SLOG_ERROR << "[GetDBPassword] user line is empty: " << secretFilePath;
            return "";
        }
        std::getline(ifs, line);
        if (line.empty()) {
            SLOG_ERROR << "[GetDBPassword] password line is empty: " << secretFilePath;
            return "";
        }
        if (!CheckDefaultFormat(line)) {
            SLOG_ERROR << "[GetDBPassword] password line format is invalid: " << line;
            return "";
        }
        return line;
    }
}  // namespace qifeng_ca