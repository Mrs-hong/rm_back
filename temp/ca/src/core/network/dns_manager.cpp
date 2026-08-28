//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <arpa/inet.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "qifeng_framework/common/logger.h"

#include "common/utils/ipv4_utils.h"
#include "core/network/dns_manager.h"

namespace qifeng_ca {

    bool DnsManager::IsValidDns(const std::string &dns) {
        return Ipv4Utils::IsValidIpv4(dns);
    }

    std::string DnsManager::GetResolvConfRealPath() {
        std::array<char, PATH_MAX> resolved {};
        if (realpath("/etc/resolv.conf", resolved.data()) != nullptr) {
            return "/etc/resolv.conf";
        }
        return std::string(resolved.data());
    }

    bool DnsManager::IsSystemdResolvedActive() {
        struct stat st {};
        if (stat("/run/systemd/resolve/resolv.conf", &st) != 0) {
            return false;
        }
        return S_ISREG(st.st_mode);
    }

    bool DnsManager::ReadResolvConf(std::vector<std::string> &outDns) {
        std::string realPath = GetResolvConfRealPath();
        std::ifstream ifs(realPath);
        if (!ifs.is_open()) {
            SLOG_WARN << "Cannot open resolv.conf at: " << realPath;
            return false;
        }

        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            std::istringstream iss(line);
            std::string keyword;
            iss >> keyword;
            if (keyword != "nameserver") {
                continue;
            }
            std::string dnsAddr;
            iss >> dnsAddr;
            if (dnsAddr == "127.0.0.53") {
                SLOG_DEBUG << "Skipping systemd-resolved stub: 127.0.0.53";
                continue;
            }
            if (DnsManager::IsValidDns(dnsAddr)) {
                outDns.push_back(dnsAddr);
            }
        }
        return !outDns.empty();
    }

    static bool ParseResolvConfNameservers(std::ifstream &ifs, std::vector<std::string> &outDns) {
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            std::istringstream iss(line);
            std::string keyword;
            iss >> keyword;
            if (keyword != "nameserver") {
                continue;
            }
            std::string dnsAddr;
            iss >> dnsAddr;
            if (DnsManager::IsValidDns(dnsAddr)) {
                outDns.push_back(dnsAddr);
            }
        }
        return !outDns.empty();
    }

    bool DnsManager::ReadRealResolvConf(std::vector<std::string> &outDns) {
        std::array<const char*, 2> realPaths {
            "/run/systemd/resolve/resolv.conf",
            "/var/run/systemd/resolve/resolv.conf",
        };

        for (const auto* path : realPaths) {
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                continue;
            }
            SLOG_DEBUG << "Reading real resolv.conf from: " << path;
            if (ParseResolvConfNameservers(ifs, outDns)) {
                return true;
            }
        }
        return false;
    }

    // 通过resolvectl命令行实现
    bool DnsManager::QueryResolvedViaDbus(std::vector<std::string> &outDns) {
        std::string cmd = "resolvectl dns 2>/dev/null";
        // 使用简单的管道
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe == nullptr) {
            SLOG_WARN << "Failed to execute resolvectl";
            return false;
        }

        std::array<char, 512> buf {};
        std::string output;
        while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
            output += buf.data();
        }
        int ret = pclose(pipe);
        if (ret != 0) {
            SLOG_WARN << "resolvectl exited with code: " << ret;
            return false;
        }

        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            std::istringstream lineIss(line);
            std::string token;
            while (lineIss >> token) {
                if (IsValidDns(token)) {
                    outDns.push_back(token);
                }
            }
        }
        return !outDns.empty();
    }

    bool DnsManager::ReadSystemdResolved(std::vector<std::string> &outDns) {
        if (!IsSystemdResolvedActive()) {
            SLOG_DEBUG << "systemd-resolved not active";
            return false;
        }

        // 先通过命令获取
        if (QueryResolvedViaDbus(outDns)) {
            SLOG_DEBUG << "Got DNS from resolvectl, count: " << outDns.size();
            return true;
        }

        // 最后采用配置文件兜底
        if (ReadRealResolvConf(outDns)) {
            SLOG_DEBUG << "Got DNS from systemd-resolved resolv.conf, count: " << outDns.size();
            return true;
        }

        return false;
    }

    // 先通过命令行获取DNS，失败再降级由多个文件分别兜底
    bool DnsManager::GetActiveDns(std::vector<std::string> &outDns) {
        return false;  // 暂时不处理

        outDns.clear();

        if (ReadSystemdResolved(outDns)) {
            SLOG_DEBUG << "Got DNS from systemd-resolved, count: " << outDns.size();
            return true;
        }

        if (ReadResolvConf(outDns)) {
            SLOG_DEBUG << "Got DNS from /etc/resolv.conf, count: " << outDns.size();
            return true;
        }

        SLOG_WARN << "Failed to get active DNS from any source";
        return false;
    }

    bool DnsManager::GetDnsSourceDetail(DnsSourceInfo &outInfo) {
        return false;  // 暂时不处理

        outInfo.mDnsList.clear();

        if (IsSystemdResolvedActive()) {
            outInfo.mSource = "systemd-resolved";
            if (ReadSystemdResolved(outInfo.mDnsList)) {
                return true;
            }
        }

        outInfo.mSource = "/etc/resolv.conf";
        if (ReadResolvConf(outInfo.mDnsList)) {
            return true;
        }

        outInfo.mSource = "unknown";
        return false;
    }

}  // namespace qifeng_ca
