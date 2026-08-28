/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_CORE_DEVICE_NTP_H
#define QIFENG_CA_INCLUDE_CORE_DEVICE_NTP_H

#include <cstdint>
#include <netdb.h>
#include <string>

namespace qifeng_ca {

    class NtpTester {
    public:
        enum class Result {
            Success,
            ResolveFailed,
            DnsTemporary,  // 域名解析暂时失败(EAI_AGAIN等), 重试可能成功
            SocketFailed,
            SendFailed,
            ReceiveFailed,  // UDP报文发出后静默超时(未细分前, 已被UdpBlocked/Unreachable细分)
            UdpBlocked,     // ICMP可达但UDP/123无响应: 网络策略限制UDP流量(防火墙放行ICMP拦UDP)
            Unreachable,    // 服务器明确拒绝或网络不可达(ICMP不可达/ECONNREFUSED/路由失败)
            InvalidResponse,
        };

#pragma pack(push, 1)
        // NTP 报文结构（RFC 5905，仅需最小 48 字节）
        struct NtpPacket {
            uint8_t flags;  // LI=0, VN=4, Mode=3(client)
            uint8_t stratum;
            uint8_t poll;
            int8_t precision;
            uint32_t rootDelay;
            uint32_t rootDispersion;
            uint32_t refId;
            uint32_t refTm_s, refTm_f;
            uint32_t origTm_s, origTm_f;
            uint32_t rxTm_s, rxTm_f;
            uint32_t txTm_s, txTm_f;
        };
#pragma pack(pop)

        static Result NtpTest(const std::string &server, uint16_t port = 123, int timeoutMs = 5000);

    private:
        // 探测单个已解析地址(收发NTP报文+校验来源), 供NtpTest遍历多地址容错
        static Result ProbeServerAddress(const struct addrinfo* ai, int timeoutMs);
    };
}  // namespace qifeng_ca
#endif  // QIFENG_CA_INCLUDE_CORE_DEVICE_NTP_H
