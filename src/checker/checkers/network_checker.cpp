/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/network_checker.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include "common/cmd_execute.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

namespace qifeng::scm {

namespace {

/**
 * @brief 枚举处于 UP 状态的非 lo 接口
 */
std::vector<std::string> ListUpInterfaces() {
    std::vector<std::string> ifs;
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return ifs;
    }

    struct ifconf ifc {};
    std::vector<char> buf(8192);
    ifc.ifc_len = static_cast<int>(buf.size());
    ifc.ifc_buf = buf.data();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    if (::ioctl(sock, SIOCGIFCONF, &ifc) != 0) {
        ::close(sock);
        return ifs;
    }

    char *p = buf.data();
    char *end = p + ifc.ifc_len;
    while (p < end) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        auto *ifr = reinterpret_cast<struct ifreq *>(p);
        p += sizeof(*ifr);
        std::string n = ifr->ifr_name;
        if (n == "lo") {
            continue;
        }
        struct ifreq fl{};
        std::memcpy(fl.ifr_name, ifr->ifr_name, IFNAMSIZ);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        if (::ioctl(sock, SIOCGIFFLAGS, &fl) == 0 && (fl.ifr_flags & IFF_UP)) {
            ifs.push_back(n);
        }
    }
    ::close(sock);
    return ifs;
}

}  // namespace

CheckResult NetworkChecker::Run(const Context &ctx) {
    CheckResult r(Name());
    auto t0 = GetTimeMs();

    auto ifs = ListUpInterfaces();
    std::string iflist;
    for (const auto &n : ifs) {
        if (!iflist.empty()) {
            iflist += ",";
        }
        iflist += n;
    }
    r.details.emplace_back("interfaces", iflist.empty() ? "none" : iflist);

    if (ifs.empty()) {
        r.status = Status::FAIL;
        r.message = "no up interface";
        r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
        SLOG_INFO << "[network] " << r.message << " (" << r.elapsed_ms << "ms)";
        return r;
    }

    // ping 网关验证连通性
    const auto &gw = ctx.config.network.gateway;
    int count = ctx.config.network.ping_count;
    if (count <= 0) {
        count = 3;
    }
    int timeout = ctx.config.network.ping_timeout_sec;
    if (timeout <= 0) {
        timeout = 1;
    }
    std::vector<std::string> pingArgs = {"-c", std::to_string(count), "-W", std::to_string(timeout), gw};
    auto pr = RunCmd("ping", pingArgs);
    bool gwOk = (pr.exit_code == 0);
    r.details.emplace_back("gateway", gw);
    r.details.emplace_back("gateway_reachable", gwOk ? "yes" : "no");

    r.status = gwOk ? Status::PASS : Status::WARNING;
    r.message = gwOk ? "network ok" : "gateway unreachable";
    r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
    SLOG_INFO << "[network] " << r.message << " (" << r.elapsed_ms << "ms)";
    return r;
}

}  // namespace qifeng::scm