//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <arpa/inet.h>
#include <array>
#include <cstdio>
#include <sys/wait.h>

#include "common/utils/ipv4_utils.h"

namespace qifeng_ca {

    bool Ipv4Utils::ParseIpv4(const std::string &ip, uint32_t &outBits) {
        if (ip.empty()) {
            return false;
        }
        // inet_pton: 标准POSIX函数, 比regex更安全高效
        struct in_addr addr {};
        if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
            return false;
        }
        // ntohl转为主机字节序, 便于后续位运算
        outBits = ntohl(static_cast<uint32_t>(addr.s_addr));
        return true;
    }

    bool Ipv4Utils::IsValidIpv4(const std::string &ip) {
        uint32_t bits = 0;
        return ParseIpv4(ip, bits);
    }

    bool Ipv4Utils::IsValidNetmask(const std::string &netmask) {
        uint32_t bits = 0;
        if (!ParseIpv4(netmask, bits)) {
            return false;
        }
        // 全0或全1不是有效子网掩码
        if (bits == 0 || bits == 0xFFFFFFFF) {
            return false;
        }
        // 位运算验证: 合法掩码必须是连续的1后跟连续的0
        // 即 ~bits + 1 与 ~bits 按位与必须为0
        uint32_t inverted = ~bits;
        return !((inverted + 1) & inverted);
    }

    bool Ipv4Utils::NetmaskToCidr(const std::string &netmask, int &outCidr) {
        uint32_t bits = 0;
        if (!ParseIpv4(netmask, bits)) {
            return false;
        }
        // 先验证掩码合法性, 非法掩码不允许转换
        uint32_t inverted = ~bits;
        if ((inverted + 1) & inverted) {
            return false;
        }
        // 统计1的个数
        outCidr = __builtin_popcount(bits);
        return true;
    }

    bool Ipv4Utils::CidrToNetmask(int cidr, std::string &outNetmask) {
        if (cidr < 0 || cidr > 32) {
            return false;
        }
        // 高cidr位置1, 低(32-cidr)位置0
        uint32_t bits = 0;
        if (cidr > 0) {
            bits = ~0U << (32 - cidr);
        }
        outNetmask = BitsToIpv4(bits);
        return true;
    }

    bool Ipv4Utils::IsValidHostIp(const std::string &ip, const std::string &netmask) {
        uint32_t ipBits = 0;
        if (!ParseIpv4(ip, ipBits)) {
            return false;
        }
        int cidr = 0;
        if (!NetmaskToCidr(netmask, cidr)) {
            return false;
        }
        uint32_t maskBits = (cidr == 0) ? 0 : (~0U << (32 - cidr));
        // 网络地址: 主机部分全0
        uint32_t networkAddr = ipBits & maskBits;
        // 广播地址: 主机部分全1
        uint32_t broadcastAddr = networkAddr | (~maskBits);
        // 主机地址不能是网络地址或广播地址
        if (ipBits == networkAddr) {
            return false;
        }
        if (ipBits == broadcastAddr) {
            return false;
        }
        return true;
    }

    bool Ipv4Utils::IsSameSubnet(const std::string &ip, const std::string &netmask, const std::string &other) {
        int cidr = 0;
        if (!NetmaskToCidr(netmask, cidr)) {
            return false;
        }
        uint32_t ipBits = 0;
        uint32_t otherBits = 0;
        if (!ParseIpv4(ip, ipBits) || !ParseIpv4(other, otherBits)) {
            return false;
        }
        uint32_t maskBits = (cidr == 0) ? 0 : (~0U << (32 - cidr));
        return (ipBits & maskBits) == (otherBits & maskBits);
    }

    std::string Ipv4Utils::BitsToIpv4(uint32_t bits) {
        struct in_addr addr {};
        addr.s_addr = htonl(bits);
        std::array<char, INET_ADDRSTRLEN> buf {};
        inet_ntop(AF_INET, &addr, buf.data(), sizeof(buf));
        return std::string(buf.data());
    }

    bool Ipv4Utils::IsSameIp(const std::string &a, const std::string &b) {
        return a == b;
    }

    bool Ipv4Utils::IsIpReachable(const std::string &ip) {
        // 调用方应已通过 IsValidIpv4 校验, 此处仅做基本判空防止命令注入
        if (ip.empty()) {
            return false;
        }
        // ping -c 1: 发送1个ICMP包; -W 1: 等待1秒超时
        std::string cmd = "ping -c 1 -W 1 " + ip;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe == nullptr) {
            return false;
        }
        // 读取并丢弃输出, 只关心退出码
        std::array<char, 128> buf {};
        while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
            // 丢弃输出
        }
        int status = pclose(pipe);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

}  // namespace qifeng_ca
