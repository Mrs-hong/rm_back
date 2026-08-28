//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <fstream>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include "qifeng_framework/common/logger.h"

#include "common/utils/ipv4_utils.h"
#include "core/network/interface_manager.h"

namespace qifeng_ca {

    bool InterfaceManager::IsValidInterfaceName(const std::string &iface) {
        if (iface.empty() || iface.length() > IFNAMSIZ) {
            return false;
        }
        for (char c : iface) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
                return false;
            }
        }
        return true;
    }

    bool InterfaceManager::ScanInterfaces() {
        struct ifaddrs* ifList = nullptr;
        if (getifaddrs(&ifList) != 0) {
            SLOG_ERROR << "getifaddrs() failed: " << strerror(errno);
            return false;
        }
        std::unique_ptr<struct ifaddrs, decltype(&freeifaddrs)> guard(ifList, freeifaddrs);

        mCachedInterfaces.clear();
        for (auto* ifa = ifList; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr) {
                continue;
            }
            // 仅处理IPv4地址
            if (ifa->ifa_addr->sa_family != AF_INET) {
                continue;
            }

            InterfaceInfo info;
            info.mName = ifa->ifa_name;

            auto* addr = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);  // NOLINT
            std::array<char, INET_ADDRSTRLEN> ipBuf {};
            if (inet_ntop(AF_INET, &addr->sin_addr, ipBuf.data(), sizeof(ipBuf)) == nullptr) {
                continue;
            }
            info.mIpv4 = std::string(ipBuf.data());

            if (ifa->ifa_netmask != nullptr) {
                auto* mask = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_netmask);  // NOLINT
                std::array<char, INET_ADDRSTRLEN> maskBuf {};
                if (inet_ntop(AF_INET, &mask->sin_addr, maskBuf.data(), sizeof(maskBuf)) != nullptr) {
                    info.mNetmask = std::string(maskBuf.data());
                }
            }

            mCachedInterfaces.push_back(std::move(info));
        }

        mScanned = true;
        return true;
    }

    bool InterfaceManager::GetInterfaceInfo(const std::string &iface, InterfaceInfo &outInfo) {
        if (!IsValidInterfaceName(iface)) {
            SLOG_ERROR << "Invalid interface name: " << iface;
            return false;
        }
        if (!ScanInterfaces()) {
            return false;
        }
        for (const auto &info : mCachedInterfaces) {
            if (info.mName == iface) {
                outInfo = info;
                return true;
            }
        }
        SLOG_ERROR << "Interface not found: " << iface;
        return false;
    }

    std::vector<InterfaceInfo> InterfaceManager::GetAllInterfaces() {
        if (!ScanInterfaces()) {
            return {};
        }
        return mCachedInterfaces;
    }

    bool InterfaceManager::IsInterfaceUp(const std::string &iface) {
        InterfaceInfo info;
        if (!GetInterfaceInfo(iface, info)) {
            return false;
        }
        // 有IPv4地址即视为UP
        return !info.mIpv4.empty();
    }

    bool InterfaceManager::GetInterfaceCidr(const std::string &iface, int &outCidr) {
        InterfaceInfo info;
        if (!GetInterfaceInfo(iface, info)) {
            return false;
        }
        if (info.mNetmask.empty()) {
            return false;
        }
        return Ipv4Utils::NetmaskToCidr(info.mNetmask, outCidr);
    }

    bool InterfaceManager::IsCarrierUp(const std::string &iface) {
        if (!IsValidInterfaceName(iface)) {
            return false;
        }
        // 网线未插入时 carrier 文件可能不存在, 此时保守返回 false 不阻断正常流程
        std::string path = "/sys/class/net/" + iface + "/carrier";
        std::ifstream carrierFile(path);
        if (!carrierFile.is_open()) {
            return false;
        }
        std::string content;
        std::getline(carrierFile, content);
        return content == "1";
    }

}  // namespace qifeng_ca
