/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "service_manger/key_recoder.h"

#include "qifeng_framework/common/logger.h"

#include <filesystem>
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace qifeng::scm {

    void KeyOperationRecorder::RecordOperation(const KeyOperationRecord &record) {
        if (mFilePath.empty()) {
            SLOG_ERROR << "KeyOperationRecorder: file path not set";
            return;
        }

        try {
            fs::path dir = fs::path(mFilePath).parent_path();
            if (!dir.empty() && !fs::exists(dir)) {
                fs::create_directories(dir);
            }

            YAML::Node root;
            root["opt_name"] = record.optName;
            root["service_name"] = record.serviceName;
            root["result"] = record.result;
            root["tar_dir"] = record.tarDir;
            root["sql_dir"] = record.sqlDir;

            std::ofstream ofs(mFilePath);
            if (!ofs.is_open()) {
                SLOG_ERROR << "KeyOperationRecorder: failed to open file: " << mFilePath;
                return;
            }
            ofs << "# 记录最后一次操作的执行状态、持久化文件、用于恢复\n";
            ofs << root;
            ofs.close();
        } catch (const std::exception &e) {
            SLOG_ERROR << "KeyOperationRecorder::RecordOperation failed: " << e.what();
        }
    }

    void KeyOperationRecorder::UpdateResult(int result) {
        if (mFilePath.empty()) {
            SLOG_ERROR << "KeyOperationRecorder: file path not set";
            return;
        }

        try {
            if (!fs::exists(mFilePath)) {
                SLOG_ERROR << "KeyOperationRecorder: file not found: " << mFilePath;
                return;
            }

            YAML::Node root = YAML::LoadFile(mFilePath);
            root["result"] = result;

            std::ofstream ofs(mFilePath);
            if (!ofs.is_open()) {
                SLOG_ERROR << "KeyOperationRecorder: failed to open file: " << mFilePath;
                return;
            }
            ofs << "# 记录最后一次操作的执行状态、持久化文件、用于恢复\n";
            ofs << root;
            ofs.close();
        } catch (const std::exception &e) {
            SLOG_ERROR << "KeyOperationRecorder::UpdateResult failed: " << e.what();
        }
    }

    bool KeyOperationRecorder::LoadLastOperation(KeyOperationRecord &record) {
        if (mFilePath.empty()) {
            return false;
        }

        try {
            if (!fs::exists(mFilePath)) {
                return false;
            }

            YAML::Node root = YAML::LoadFile(mFilePath);

            if (!root["opt_name"] || !root["result"]) {
                return false;
            }

            record.optName = root["opt_name"].as<std::string>("");
            record.serviceName = root["service_name"].as<std::string>("");
            record.result = root["result"].as<int>(0);
            record.tarDir = root["tar_dir"] ? root["tar_dir"].as<std::string>("") : "";
            record.sqlDir = root["sql_dir"] ? root["sql_dir"].as<std::string>("") : "";

            return record.IsValid();
        } catch (const std::exception &e) {
            SLOG_ERROR << "KeyOperationRecorder::LoadLastOperation failed: " << e.what();
            return false;
        }
    }

    void KeyOperationRecorder::Clear() {
        if (mFilePath.empty()) {
            return;
        }

        try {
            if (!fs::exists(mFilePath)) {
                return;
            }

            YAML::Node root;
            root["opt_name"] = "";
            root["service_name"] = "";
            root["result"] = 0;
            root["tar_dir"] = "";
            root["sql_dir"] = "";

            std::ofstream ofs(mFilePath);
            if (ofs.is_open()) {
                ofs << "# 记录最后一次操作的执行状态、持久化文件、用于恢复\n";
                ofs << root;
                ofs.close();
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "KeyOperationRecorder::Clear failed: " << e.what();
        }
    }

    void KeyOperationRecorder::SetFilePath(const std::string &filePath) {
        mFilePath = filePath;
    }

    const std::string &KeyOperationRecorder::GetFilePath() const {
        return mFilePath;
    }

}  // namespace qifeng::scm
