//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_NETWORK_NETPLAN_MANAGER_H
#define QIFENG_CA_INCLUDE_CORE_NETWORK_NETPLAN_MANAGER_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "common/status.h"

namespace qifeng_ca {

    struct NetplanConfig {
        std::string mIp;
        int mCidr = 0;
        std::string mGateway;
        std::vector<std::string> mDnsList;
        std::string mInterface;
    };

    class NetplanManager {
    public:
        NetplanManager() = default;
        ~NetplanManager() = default;

        NetplanManager(const NetplanManager &) = delete;
        NetplanManager &operator=(const NetplanManager &) = delete;
        NetplanManager(NetplanManager &&) = delete;
        NetplanManager &operator=(NetplanManager &&) = delete;

        bool ReadDnsFromConfig(const std::string &iface, const std::string &configFile,
                               std::vector<std::string> &outDns);

        Status ApplyConfig(const NetplanConfig &config, const std::string &configFile);

    private:
        static bool ValidateConfigPath(const std::string &path);

        static std::string BuildYaml(const NetplanConfig &config);

        static bool WriteConfig(const std::string &yamlContent, const std::string &configFile);

        static bool RunNetplanGenerate();

        static bool RunNetplanApply();

        static bool RunNetplanCommandAsync(const std::string &command, const std::string &arg, int timeoutMs);

        bool BeginTransaction(const std::string &configFile);

        bool CommitTransaction();

        bool RollbackTransaction();

        bool ExecuteApplyWithRollback(const std::string &yamlContent, const std::string &configFile);

    private:
        std::mutex mMutex;
        std::string mBackupPath;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_NETWORK_NETPLAN_MANAGER_H
