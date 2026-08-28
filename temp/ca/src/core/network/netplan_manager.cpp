//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <cstdlib>
#include <linux/limits.h>
#include <spawn.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "qifeng_framework/common/logger.h"
#include "workflow/WFFacilities.h"
#include "workflow/WFTaskFactory.h"
#include "yaml-cpp/yaml.h"

#include "common/atomic_write_file.h"
#include "core/network/netplan_manager.h"

namespace qifeng_ca {

    bool NetplanManager::ValidateConfigPath(const std::string &path) {
        if (path.empty()) {
            return false;
        }
        const std::string allowedPrefix = "/etc/netplan/";
        if (path.compare(0, allowedPrefix.size(), allowedPrefix) != 0) {
            SLOG_ERROR << "Config path outside allowed directory: " << path;
            return false;
        }
        if (path.find("..") != std::string::npos) {
            SLOG_ERROR << "Config path contains path traversal: " << path;
            return false;
        }

        std::array<char, PATH_MAX> resolved {};
        if (realpath(path.c_str(), resolved.data()) == nullptr) {
            SLOG_ERROR << "Config path does not exist or cannot be resolved: " << path;
            return false;
        }
        std::string resolvedPath(resolved.data());

        if (resolvedPath.compare(0, allowedPrefix.size(), allowedPrefix) != 0) {
            SLOG_ERROR << "Resolved path outside allowed directory: " << resolvedPath;
            return false;
        }
        return true;
    }

    std::string NetplanManager::BuildYaml(const NetplanConfig &config) {
        YAML::Emitter emitter;
        emitter << YAML::BeginMap;
        emitter << YAML::Key << "network" << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "version" << YAML::Value << 2;
        emitter << YAML::Key << "renderer" << YAML::Value << "networkd";
        emitter << YAML::Key << "ethernets" << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << config.mInterface << YAML::Value << YAML::BeginMap;

        emitter << YAML::Key << "dhcp4" << YAML::Value << "no";
        emitter << YAML::Key << "addresses" << YAML::Value << YAML::BeginSeq;
        std::ostringstream addr;
        addr << config.mIp << "/" << config.mCidr;
        emitter << addr.str();
        emitter << YAML::EndSeq;

        emitter << YAML::Key << "routes" << YAML::Value << YAML::BeginSeq;
        emitter << YAML::BeginMap;
        emitter << YAML::Key << "to" << YAML::Value << "default";
        emitter << YAML::Key << "via" << YAML::Value << config.mGateway;
        emitter << YAML::EndMap;
        emitter << YAML::EndSeq;

        if (!config.mDnsList.empty()) {
            emitter << YAML::Key << "nameservers" << YAML::Value << YAML::BeginMap;
            emitter << YAML::Key << "addresses" << YAML::Value << YAML::BeginSeq;
            for (const auto &dns : config.mDnsList) {
                emitter << dns;
            }
            emitter << YAML::EndSeq;
            emitter << YAML::EndMap;
        }

        emitter << YAML::EndMap;
        emitter << YAML::EndMap;
        emitter << YAML::EndMap;
        return emitter.c_str();
    }

    bool NetplanManager::WriteConfig(const std::string &yamlContent, const std::string &configFile) {
        if (!AtomicFileWriter::WriteAtomic(configFile, yamlContent, 0644)) {
            SLOG_ERROR << "WriteAtomic failed for: " << configFile;
            return false;
        }
        return true;
    }

    // 使用create_go_task处理方便带上超时能力
    bool NetplanManager::RunNetplanCommandAsync(const std::string &command, const std::string &arg, int timeoutMs) {
        auto wg = std::make_shared<WFFacilities::WaitGroup>(1);
        auto success = std::make_shared<bool>(false);

        auto task = WFTaskFactory::create_go_task("netplan_cmd", [command, arg, success, wg]() {
            pid_t pid = 0;
            std::string cmdPath = "/usr/sbin/" + command;
            std::array<char*, 3> argv = {const_cast<char*>(command.c_str()),  // NOLINT
                                         const_cast<char*>(arg.c_str()),      // NOLINT
                                         nullptr};
            int ret = posix_spawn(&pid, cmdPath.c_str(), nullptr, nullptr, argv.data(), ::environ);
            if (ret != 0) {
                SLOG_ERROR << "posix_spawn " << command << " " << arg << " failed: " << strerror(ret);
                return;
            }

            int status = 0;
            if (waitpid(pid, &status, 0) < 0) {
                SLOG_ERROR << "waitpid failed: " << strerror(errno);
                return;
            }
            *success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            if (!success) {
                SLOG_ERROR << command << " " << arg << " exited with status: " << status;
            }

            wg->done();
        });

        task->start();

        // 等待任务完成或超时
        switch (wg->wait(timeoutMs)) {
            case std::future_status::ready:
                break;
            case std::future_status::timeout:
            case std::future_status::deferred:
            default:
                SLOG_ERROR << command << " " << arg << " timed out after " << timeoutMs << "ms";
                return false;
        }

        return *success;
    }

    bool NetplanManager::RunNetplanGenerate() {
        // 先尝试Workflow异步执行, 超时1秒
        if (RunNetplanCommandAsync("netplan", "generate", 1)) {
            return true;
        }

        // fallback到同步执行
        SLOG_WARN << "Async netplan generate failed, fallback to sync";
        pid_t pid = 0;
        std::array<char*, 3> argv = {const_cast<char*>("netplan"), const_cast<char*>("generate"), nullptr};  // NOLINT
        int ret = posix_spawn(&pid, "/usr/sbin/netplan", nullptr, nullptr, argv.data(), ::environ);
        if (ret != 0) {
            SLOG_ERROR << "posix_spawn netplan generate failed: " << strerror(ret);
            return false;
        }

        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            SLOG_ERROR << "waitpid failed: " << strerror(errno);
            return false;
        }
        bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (!ok) {
            SLOG_ERROR << "netplan generate exited with status: " << status;
        }
        return ok;
    }

    bool NetplanManager::RunNetplanApply() {
        // 先尝试Workflow异步执行, 超时2秒(apply比generate耗时更长)
        if (RunNetplanCommandAsync("netplan", "apply", 2)) {
            return true;
        }

        // fallback到同步执行
        SLOG_WARN << "Async netplan apply failed, fallback to sync";
        pid_t pid = 0;
        std::array<char*, 3> argv = {const_cast<char*>("netplan"), const_cast<char*>("apply"), nullptr};  // NOLINT
        int ret = posix_spawn(&pid, "/usr/sbin/netplan", nullptr, nullptr, argv.data(), ::environ);
        if (ret != 0) {
            SLOG_ERROR << "posix_spawn netplan apply failed: " << strerror(ret);
            return false;
        }

        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            SLOG_ERROR << "waitpid failed: " << strerror(errno);
            return false;
        }
        bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (!ok) {
            SLOG_ERROR << "netplan apply exited with status: " << status;
        }
        return ok;
    }

    bool NetplanManager::BeginTransaction(const std::string &configFile) {
        if (!ValidateConfigPath(configFile)) {
            SLOG_ERROR << "Invalid config file path: " << configFile;
            return false;
        }
        if (!AtomicFileWriter::Backup(configFile)) {
            SLOG_ERROR << "Backup failed for: " << configFile;
            return false;
        }
        mBackupPath = configFile;
        return true;
    }

    bool NetplanManager::CommitTransaction() {
        mBackupPath.clear();
        return true;
    }

    bool NetplanManager::RollbackTransaction() {
        if (mBackupPath.empty()) {
            return false;
        }
        bool ok = AtomicFileWriter::Restore(mBackupPath);
        if (!ok) {
            SLOG_ERROR << "Rollback restore failed for: " << mBackupPath;
            return false;
        }

        // 回滚后需要重新apply使配置生效
        if (!RunNetplanApply()) {
            SLOG_ERROR << "Rollback netplan apply failed";
            return false;
        }

        mBackupPath.clear();
        return true;
    }

    bool NetplanManager::ExecuteApplyWithRollback(const std::string &yamlContent, const std::string &configFile) {
        if (!WriteConfig(yamlContent, configFile)) {
            SLOG_ERROR << "Write config failed";
            return false;
        }

        if (!RunNetplanGenerate()) {
            SLOG_ERROR << "netplan generate failed, rolling back";
            RollbackTransaction();
            return false;
        }

        // 应用配置到系统
        if (!RunNetplanApply()) {
            SLOG_ERROR << "netplan apply failed, rolling back";
            RollbackTransaction();
            return false;
        }

        return true;
    }

    bool NetplanManager::ReadDnsFromConfig(const std::string &iface, const std::string &configFile,
                                           std::vector<std::string> &outDns) {
        outDns.clear();
        std::string content;
        Status readStatus = AtomicFileWriter::ReadFile(configFile, content);
        if (readStatus.GetCode() != 0) {
            SLOG_WARN << "Failed to read netplan config: " << configFile;
            return false;
        }

        try {
            YAML::Node root = YAML::Load(content);
            if (!root["network"]) {
                return false;
            }
            YAML::Node network = root["network"];
            if (!network["ethernets"]) {
                return false;
            }
            YAML::Node ethernets = network["ethernets"];
            if (!ethernets[iface]) {
                return false;
            }
            YAML::Node ifaceNode = ethernets[iface];
            if (!ifaceNode["nameservers"]) {
                return false;
            }
            YAML::Node ns = ifaceNode["nameservers"];
            if (!ns["addresses"]) {
                return false;
            }
            YAML::Node addrs = ns["addresses"];
            for (std::size_t i = 0; i < addrs.size(); ++i) {
                std::string dns = addrs[i].as<std::string>();
                outDns.push_back(dns);
            }
            return !outDns.empty();
        } catch (const YAML::Exception &e) {
            SLOG_ERROR << "YAML parse error: " << e.what();
            return false;
        }
    }

    Status NetplanManager::ApplyConfig(const NetplanConfig &config, const std::string &configFile) {
        if (!BeginTransaction(configFile)) {
            return Status {-1, "配置事务启动失败"};
        }

        std::string yamlContent = BuildYaml(config);

        if (!ExecuteApplyWithRollback(yamlContent, configFile)) {
            return Status {-1, "网络配置应用失败"};
        }

        CommitTransaction();
        return Status {};
    }

}  // namespace qifeng_ca
