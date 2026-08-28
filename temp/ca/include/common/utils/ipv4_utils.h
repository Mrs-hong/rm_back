//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

// Ipv4Utils: IPv4地址工具集
// 职责: 统一所有IPv4相关的验证、转换、子网计算
// 设计: 全部使用inet_pton/inet_ntop替代regex, 纯静态工具类不可实例化

#ifndef QIFENG_CA_INCLUDE_COMMON_UTILS_IPV4_UTILS_H
#define QIFENG_CA_INCLUDE_COMMON_UTILS_IPV4_UTILS_H

#include <cstdint>
#include <string>
#include <vector>

namespace qifeng_ca {

    class Ipv4Utils {
    public:
        Ipv4Utils() = delete;

        // 将IPv4字符串解析为uint32_t(主机字节序), 如 "192.168.1.1" -> 0xC0A80101
        static bool ParseIpv4(const std::string &ip, uint32_t &outBits);

        // 验证IPv4字符串是否合法(使用inet_pton)
        static bool IsValidIpv4(const std::string &ip);

        // 验证子网掩码是否合法(连续1后跟连续0, 排除全0和全1)
        static bool IsValidNetmask(const std::string &netmask);

        // 子网掩码转CIDR前缀长度, 内部先验证掩码合法性
        // 如 "255.255.255.0" -> 24
        static bool NetmaskToCidr(const std::string &netmask, int &outCidr);

        // CIDR前缀长度转子网掩码, 如 24 -> "255.255.255.0"
        static bool CidrToNetmask(int cidr, std::string &outNetmask);

        // 验证IP是否为有效的主机地址(非网络地址, 非广播地址)
        static bool IsValidHostIp(const std::string &ip, const std::string &netmask);

        // 判断两个IP是否在同一子网内
        static bool IsSameSubnet(const std::string &ip, const std::string &netmask, const std::string &other);

        // 将uint32_t(主机字节序)转为IPv4字符串
        static std::string BitsToIpv4(uint32_t bits);

        // 判断两个IPv4字符串是否相同
        static bool IsSameIp(const std::string &a, const std::string &b);

        // 判断IP是否可达(发送单个ICMP包, 1秒超时). 用于IP冲突检测.
        static bool IsIpReachable(const std::string &ip);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_UTILS_IPV4_UTILS_H
