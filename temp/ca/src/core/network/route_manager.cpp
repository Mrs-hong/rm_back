//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <arpa/inet.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"

#include "common/utils/ipv4_utils.h"
#include "core/network/route_manager.h"

namespace qifeng_ca {

    static int CreateNetlinkSocket() {
        int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        if (sock < 0) {
            SLOG_ERROR << "Failed to create netlink socket: " << strerror(errno);
        }
        return sock;
    }

    static bool BindNetlinkSocket(int sock) {
        struct sockaddr_nl addr {};
        addr.nl_family = AF_NETLINK;
        if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {  // NOLINT
            SLOG_ERROR << "Failed to bind netlink socket: " << strerror(errno);
            return false;
        }
        return true;
    }

    static bool SendNetlinkRouteDump(int sock) {
        struct {
            struct nlmsghdr nlh;
            struct rtmsg rtm;
        } request {};

        request.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
        request.nlh.nlmsg_type = RTM_GETROUTE;
        request.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
        request.nlh.nlmsg_pid = 0;
        request.rtm.rtm_family = AF_INET;
        request.rtm.rtm_table = RT_TABLE_MAIN;

        struct sockaddr_nl kernel {};
        kernel.nl_family = AF_NETLINK;
        ssize_t sent =
            sendto(sock, &request, request.nlh.nlmsg_len, 0, reinterpret_cast<struct sockaddr*>(&kernel),  // NOLINT
                   sizeof(kernel));
        if (sent < 0) {
            SLOG_ERROR << "Failed to send netlink request: " << strerror(errno);
            return false;
        }
        return true;
    }

    static std::string ParseRtaGateway(struct rtattr* attr) {
        auto* gw = reinterpret_cast<uint32_t*>(RTA_DATA(attr));  // NOLINT
        struct in_addr inAddr {};
        inAddr.s_addr = *gw;
        std::array<char, INET_ADDRSTRLEN> buf {};
        if (inet_ntop(AF_INET, &inAddr, buf.data(), sizeof(buf)) == nullptr) {
            return {};
        }
        return std::string(buf.data());
    }

    static std::string ParseRtaInterface(struct rtattr* attr) {
        auto* ifIndex = reinterpret_cast<int*>(RTA_DATA(attr));  // NOLINT
        std::array<char, IF_NAMESIZE> buf {};
        if (if_indextoname(*ifIndex, buf.data()) == nullptr) {
            return {};
        }
        return std::string(buf.data());
    }

    static std::string ParseRtaDst(struct rtattr* attr) {
        auto* dst = reinterpret_cast<uint32_t*>(RTA_DATA(attr));  // NOLINT
        struct in_addr inAddr {};
        inAddr.s_addr = *dst;
        std::array<char, INET_ADDRSTRLEN> buf {};
        if (inet_ntop(AF_INET, &inAddr, buf.data(), sizeof(buf)) == nullptr) {
            return {};
        }
        return std::string(buf.data());
    }

    static int ParseRtaPriority(struct rtattr* attr) {
        auto* prio = reinterpret_cast<uint32_t*>(RTA_DATA(attr));  // NOLINT
        return static_cast<int>(*prio);
    }

    // 从单条rtnetlink消息中提取默认网关信息
    static bool ExtractDefaultGwFromMsg(struct nlmsghdr* nlh, const std::string &iface, std::string &outMatched,
                                        std::string &outFallback) {
        auto* rtm = reinterpret_cast<struct rtmsg*>(NLMSG_DATA(nlh));  // NOLINT
        if (rtm->rtm_family != AF_INET || rtm->rtm_dst_len != 0) {
            return true;
        }

        int attrLen = static_cast<int>(RTM_PAYLOAD(nlh));
        auto* attr = reinterpret_cast<struct rtattr*>(RTM_RTA(rtm));  // NOLINT
        std::string gwAddr;
        std::string ifName;

        for (; RTA_OK(attr, attrLen); attr = RTA_NEXT(attr, attrLen)) {
            if (attr->rta_type == RTA_GATEWAY) {
                gwAddr = ParseRtaGateway(attr);
            }
            if (attr->rta_type == RTA_OIF) {
                ifName = ParseRtaInterface(attr);
            }
        }

        if (gwAddr.empty()) {
            return true;
        }
        if (ifName == iface) {
            outMatched = gwAddr;
        } else if (outFallback.empty()) {
            outFallback = gwAddr;
        }
        return true;
    }

    // 从单条rtnetlink消息中提取路由条目
    static bool ExtractRouteEntryFromMsg(struct nlmsghdr* nlh, RouteEntry &outEntry) {
        auto* rtm = reinterpret_cast<struct rtmsg*>(NLMSG_DATA(nlh));  // NOLINT
        if (rtm->rtm_family != AF_INET) {
            return false;
        }

        int attrLen = static_cast<int>(RTM_PAYLOAD(nlh));
        auto* attr = reinterpret_cast<struct rtattr*>(RTM_RTA(rtm));  // NOLINT

        for (; RTA_OK(attr, attrLen); attr = RTA_NEXT(attr, attrLen)) {
            if (attr->rta_type == RTA_DST) {
                outEntry.mDestination = ParseRtaDst(attr);
            }
            if (attr->rta_type == RTA_GATEWAY) {
                outEntry.mGateway = ParseRtaGateway(attr);
            }
            if (attr->rta_type == RTA_OIF) {
                outEntry.mInterface = ParseRtaInterface(attr);
            }
            if (attr->rta_type == RTA_PRIORITY) {
                outEntry.mPriority = ParseRtaPriority(attr);
            }
        }

        if (rtm->rtm_dst_len == 0) {
            outEntry.mDestination = "0.0.0.0/0";
        }
        return true;
    }

    // 通过rtnetlink获取默认网关
    static bool GetGatewayViaRtnetlink(const std::string &iface, std::string &outGateway) {  // NOLINT
        int sock = CreateNetlinkSocket();
        if (sock < 0) {
            return false;
        }
        ScopeExit([sock]() { close(sock); });

        if (!BindNetlinkSocket(sock) || !SendNetlinkRouteDump(sock)) {
            return false;
        }

        std::vector<char> buf(8192);
        std::string matched;
        std::string fallback;

        while (true) {
            ssize_t len = recv(sock, buf.data(), buf.size(), 0);
            if (len < 0) {
                return false;
            }
            auto* nlh = reinterpret_cast<struct nlmsghdr*>(buf.data());  // NOLINT
            for (; NLMSG_OK(nlh, static_cast<unsigned int>(len)); nlh = NLMSG_NEXT(nlh, len)) {
                if (nlh->nlmsg_type == NLMSG_DONE) {
                    if (!matched.empty()) {
                        outGateway = matched;
                    } else if (!fallback.empty()) {
                        outGateway = fallback;
                    }
                    return !outGateway.empty();
                }
                if (nlh->nlmsg_type == NLMSG_ERROR) {
                    return false;
                }
                ExtractDefaultGwFromMsg(nlh, iface, matched, fallback);
            }
        }
    }

    // 通过rtnetlink获取所有路由
    static bool GetAllRoutesViaRtnetlink(std::vector<RouteEntry> &outRoutes) {  // NOLINT
        int sock = CreateNetlinkSocket();
        if (sock < 0) {
            return false;
        }
        ScopeExit([sock]() { close(sock); });

        if (!BindNetlinkSocket(sock) || !SendNetlinkRouteDump(sock)) {
            return false;
        }

        std::vector<char> buf(8192);
        while (true) {
            ssize_t len = recv(sock, buf.data(), buf.size(), 0);
            if (len < 0) {
                return false;
            }
            auto* nlh = reinterpret_cast<struct nlmsghdr*>(buf.data());  // NOLINT
            for (; NLMSG_OK(nlh, static_cast<unsigned int>(len)); nlh = NLMSG_NEXT(nlh, len)) {
                if (nlh->nlmsg_type == NLMSG_DONE) {
                    return !outRoutes.empty();
                }
                if (nlh->nlmsg_type == NLMSG_ERROR) {
                    return false;
                }
                RouteEntry entry;
                if (ExtractRouteEntryFromMsg(nlh, entry)) {
                    outRoutes.push_back(std::move(entry));
                }
            }
        }
    }

    bool RouteManager::ParseRouteFile(const std::string &iface, std::string &outGateway) {
        std::ifstream ifs("/proc/net/route");
        if (!ifs.is_open()) {
            SLOG_ERROR << "Failed to open /proc/net/route";
            return false;
        }

        std::string line;
        std::string fallbackGw;
        while (std::getline(ifs, line)) {
            std::istringstream iss(line);
            std::string ifName;
            std::string dest;
            std::string gw;
            iss >> ifName >> dest >> gw;
            if (dest != "00000000" || gw.empty()) {
                continue;
            }

            uint32_t gwAddr = 0;
            std::stringstream ss;
            ss << std::hex << gw;
            ss >> gwAddr;
            if (gwAddr == 0) {
                continue;
            }

            struct in_addr addr {};
            addr.s_addr = gwAddr;
            std::array<char, INET_ADDRSTRLEN> gwStr {};
            if (inet_ntop(AF_INET, &addr, gwStr.data(), sizeof(gwStr)) == nullptr) {
                continue;
            }

            if (ifName == iface) {
                outGateway = std::string(gwStr.data());
                return true;
            }
            if (fallbackGw.empty()) {
                fallbackGw = std::string(gwStr.data());
            }
        }

        if (!fallbackGw.empty()) {
            outGateway = fallbackGw;
            return true;
        }
        return false;
    }

    bool RouteManager::GetDefaultGateway(const std::string &iface, std::string &outGateway) {
        if (GetGatewayViaRtnetlink(iface, outGateway)) {
            SLOG_DEBUG << "Got gateway via rtnetlink for " << iface << ": " << outGateway;
            return true;
        }

        SLOG_WARN << "rtnetlink failed, fallback to /proc/net/route";
        return ParseRouteFile(iface, outGateway);
    }

    std::vector<RouteEntry> RouteManager::GetAllRoutes() {
        std::vector<RouteEntry> routes;
        if (GetAllRoutesViaRtnetlink(routes)) {
            return routes;
        }
        return {};
    }

}  // namespace qifeng_ca
