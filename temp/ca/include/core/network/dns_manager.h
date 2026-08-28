//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_NETWORK_DNS_MANAGER_H
#define QIFENG_CA_INCLUDE_CORE_NETWORK_DNS_MANAGER_H

#include <string>
#include <vector>

namespace qifeng_ca {

    struct DnsSourceInfo {
        std::string mSource;
        std::vector<std::string> mDnsList;
    };

    class DnsManager {
    public:
        DnsManager() = default;
        ~DnsManager() = default;

        DnsManager(const DnsManager &) = delete;
        DnsManager &operator=(const DnsManager &) = delete;
        DnsManager(DnsManager &&) = delete;
        DnsManager &operator=(DnsManager &&) = delete;

        // 先通过命令行获取DNS，失败再降级由多个文件分别兜底
        bool GetActiveDns(std::vector<std::string> &outDns);

        bool GetDnsSourceDetail(DnsSourceInfo &outInfo);

        static bool IsValidDns(const std::string &dns);

    private:
        static bool ReadResolvConf(std::vector<std::string> &outDns);

        static bool ReadRealResolvConf(std::vector<std::string> &outDns);

        static bool ReadSystemdResolved(std::vector<std::string> &outDns);

        static bool QueryResolvedViaDbus(std::vector<std::string> &outDns);

        static bool IsSystemdResolvedActive();

        static std::string GetResolvConfRealPath();
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_NETWORK_DNS_MANAGER_H
