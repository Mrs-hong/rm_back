/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "common/types.h"
#include "service_manager/file_manager.h"

#include "common/utils/path.h"
#include "qifeng_framework/common/logger.h"

#include <filesystem>
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace qifeng::scm {

    // --- 私有工具方法（service.yaml 解析）------

    ResultMsg FileManager::VerifySoftwarePackage(const std::string &softwareDir) {
        // 检查软件包目录存在
        if (!fs::exists(softwareDir)) {
            return MakeError("Software package directory does not exist: " + softwareDir);
        }

        // 检查 service.yaml 存在
        std::string yamlPath = utils::JoinPath(softwareDir, ServiceYamlName);
        if (!fs::exists(yamlPath)) {
            return MakeError("Software package missing " + std::string(ServiceYamlName));
        }

        // 检查 service.yaml 可读取
        std::ifstream ifs(yamlPath);
        if (!ifs.is_open()) {
            return MakeError("Cannot read " + std::string(ServiceYamlName) + " in package");
        }
        ifs.close();

        // 基本YAML格式验证
        try {
            YAML::LoadFile(yamlPath);
        } catch (const YAML::Exception &e) {
            return MakeError("Invalid YAML format in " + std::string(ServiceYamlName) + ": " + e.what());
        }

        return MakeSuccess();
    }

    std::string FileManager::ReadServiceDataDirName(const std::string &serviceName) const {
        std::string yamlPath = utils::JoinPath(GetServiceDir(serviceName), ServiceYamlName);

        if (!fs::exists(yamlPath)) {
            return "";
        }

        try {
            YAML::Node config = YAML::LoadFile(yamlPath);

            // 读取 execution.dataDir 字段
            if (config["execution"] && config["execution"]["dataDir"]) {
                return config["execution"]["dataDir"].as<std::string>("");
            }
        } catch (const YAML::Exception &) {
            // YAML 解析失败，返回空字符串
        }

        return "";
    }

    // --- 细粒度升级 ---

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg FileManager::ParseUpgradeDetail(const std::string &detailPath, UpgradeDetail &outDetail) {
        outDetail.replaceDirs.clear();
        outDetail.addDirs.clear();
        outDetail.removeDirs.clear();

        if (detailPath.empty()) {
            return MakeError("up_detail.yaml path is empty");
        }
        if (!fs::exists(detailPath)) {
            return MakeError("up_detail.yaml not found: " + detailPath);
        }

        try {
            YAML::Node root = YAML::LoadFile(detailPath);
            if (root["replace"]) {
                for (const auto &item : root["replace"]) {
                    std::string name = item.as<std::string>("");
                    if (!name.empty()) {
                        outDetail.replaceDirs.push_back(name);
                    }
                }
            }
            if (root["add"]) {
                for (const auto &item : root["add"]) {
                    std::string name = item.as<std::string>("");
                    if (!name.empty()) {
                        outDetail.addDirs.push_back(name);
                    }
                }
            }
            if (root["remove"]) {
                for (const auto &item : root["remove"]) {
                    std::string name = item.as<std::string>("");
                    if (!name.empty()) {
                        outDetail.removeDirs.push_back(name);
                    }
                }
            }
        } catch (const YAML::Exception &e) {
            return MakeError("Failed to parse up_detail.yaml: " + std::string(e.what()));
        }

        SLOG_INFO << "Parsed upgrade detail: replace=" << outDetail.replaceDirs.size()
                  << " add=" << outDetail.addDirs.size() << " remove=" << outDetail.removeDirs.size();
        return MakeSuccess();
    }

}  // namespace qifeng::scm
