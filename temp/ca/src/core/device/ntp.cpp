/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <arpa/inet.h>
#include <future>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"

#include "core/device/ntp.h"

namespace qifeng_ca {
    namespace {
        // 域名解析结果: res为地址链表(成功时), err为getaddrinfo错误码(失败时, 0表示成功)
        struct ResolveOutcome {
            struct addrinfo* res;
            int err;
        };

        // 域名解析: 带超时 + 失败重试一次
        // 成功返回addrinfo链表(调用方负责freeaddrinfo), 失败返回false并输出错误分类:
        // 超时 -> DnsTemporary(网络故障); 重试后仍失败 -> ResolveFailed(域名不存在或DNS持续异常)
        bool ResolveServerWithTimeout(const std::string &server, uint16_t port, std::chrono::milliseconds timeout,
                                      struct addrinfo** out, NtpTester::Result* errOut) {
            constexpr int kMaxAttempts = 2;  // 失败最多重试一次
            for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
                if (attempt > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                // shared_ptr保活: 超时后worker可能仍在解析, 其set_value时promise必须仍然有效, 避免UB
                auto promise = std::make_shared<std::promise<ResolveOutcome>>();
                auto future = promise->get_future();
                auto worker = std::thread([server, port, promise]() {
                    ResolveOutcome outcome {nullptr, EAI_AGAIN};
                    struct addrinfo hints {};
                    hints.ai_family = AF_UNSPEC;  // 同时解析IPv4/IPv6
                    hints.ai_socktype = SOCK_DGRAM;
                    outcome.err = getaddrinfo(server.c_str(), std::to_string(port).c_str(), &hints, &outcome.res);
                    promise->set_value(outcome);
                });
                worker.detach();

                if (future.wait_for(timeout) != std::future_status::ready) {
                    SLOG_WARN << "NtpTester: resolve timeout for [" << server << "], timeoutMs=" << timeout.count();
                    *errOut = NtpTester::Result::DnsTemporary;
                    return false;
                }
                ResolveOutcome outcome = future.get();
                if (outcome.err == 0 && outcome.res != nullptr) {
                    *out = outcome.res;
                    return true;
                }
                SLOG_WARN << "NtpTester: resolve failed for [" << server << "], attempt=" << attempt
                          << ", err=" << outcome.err << " (" << gai_strerror(outcome.err) << ")";
                // glibc下EAI_NONAME与EAI_AGAIN数值相同无法区分, 统一重试一次;
                // 重试仍失败判为解析失败(域名不存在或DNS持续异常), 超时判为暂时性网络故障
                if (attempt + 1 >= kMaxAttempts) {
                    *errOut = NtpTester::Result::ResolveFailed;
                    return false;
                }
                *errOut = NtpTester::Result::DnsTemporary;
            }
            return false;
        }
        // ICMP可达性辅助探测(复用系统ping, 无nc/sntp等工具的精简系统可用)
        bool IcmpReachable(const std::string &ip) {
            std::string cmd = "ping -c 1 -W 2 " + ip + " >/dev/null 2>&1";
            return std::system(cmd.c_str()) == 0;
        }

        // 多地址UDP探测全超时后的细分: ICMP通->UdpBlocked(防火墙特征), 不通->Unreachable
        NtpTester::Result ClassifyTimeout(const struct addrinfo* resHead, const std::string &server) {
            const struct addrinfo* first = resHead;
            if (first == nullptr) {
                return NtpTester::Result::ReceiveFailed;
            }
            char addrBuf[INET6_ADDRSTRLEN] {};
            if (first->ai_family == AF_INET) {
                inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(first->ai_addr)->sin_addr, addrBuf,
                          sizeof(addrBuf));
            } else {
                inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(first->ai_addr)->sin6_addr, addrBuf,
                          sizeof(addrBuf));
            }
            bool icmpOk = IcmpReachable(addrBuf);
            SLOG_WARN << "NtpTester: UDP probe timeout for [" << server << "] firstAddr=" << addrBuf
                      << ", icmp=" << (icmpOk ? "reachable" : "unreachable");
            return icmpOk ? NtpTester::Result::UdpBlocked : NtpTester::Result::Unreachable;
        }
    }  // namespace

    NtpTester::Result NtpTester::NtpTest(const std::string &server, uint16_t port, int timeoutMs) {
        // 每个地址的超时 = 总预算均摊, 最多遍历3个地址
        // 多A记录域名(如cn.ntp.org.cn)首地址不可达时自动尝试下一个, 避免单点故障导致整体失败
        const int kMaxAddresses = 3;
        int perAddrTimeoutMs = (timeoutMs / kMaxAddresses) + 1;
        if (perAddrTimeoutMs < 200) {
            perAddrTimeoutMs = 200;
        }

        struct addrinfo* resHead = nullptr;
        Result resolveErr = Result::ResolveFailed;
        // 解析阶段超时预算减半, 与探测阶段均摊总timeoutMs(内部重试2次最坏约2×timeoutMs/2)
        if (!ResolveServerWithTimeout(server, port, std::chrono::milliseconds(timeoutMs / 2), &resHead, &resolveErr)) {
            return resolveErr;
        }
        std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resGuard(resHead, freeaddrinfo);

        Result lastErr = Result::ReceiveFailed;
        for (struct addrinfo* ai = resHead; ai != nullptr && lastErr != Result::Success; ai = ai->ai_next) {
            lastErr = ProbeServerAddress(ai, perAddrTimeoutMs);
        }
        // 全部地址静默超时: 用ICMP细分"网络限制UDP"与"服务器不可达", 供上层给出准确提示
        if (lastErr == Result::ReceiveFailed) {
            lastErr = ClassifyTimeout(resHead, server);
        }
        return lastErr;
    }

    NtpTester::Result NtpTester::ProbeServerAddress(const struct addrinfo* ai, int timeoutMs) {
        int sockFd = socket(ai->ai_family, SOCK_DGRAM, IPPROTO_UDP);
        if (sockFd < 0) {
            SLOG_ERROR << "NtpTester: socket() failed: " << strerror(errno);
            return Result::SocketFailed;
        }
        ScopeExit sockGuard([sockFd] { close(sockFd); });

        struct timeval tv {};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = static_cast<long>(timeoutMs % 1000) * 1000;
        setsockopt(sockFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sockFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        char addrBuf[INET6_ADDRSTRLEN] {};
        if (ai->ai_family == AF_INET) {
            inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(ai->ai_addr)->sin_addr, addrBuf, sizeof(addrBuf));
        } else if (ai->ai_family == AF_INET6) {
            inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(ai->ai_addr)->sin6_addr, addrBuf,
                      sizeof(addrBuf));
        } else {
            SLOG_WARN << "NtpTester: unsupported address family " << ai->ai_family;
            return Result::ReceiveFailed;
        }

        // connect(UDP): 绑定对端后内核自动过滤其他来源的报文(无需手动校验源地址),
        // 且对端不可达时ICMP错误会映射为send/recv的errno(ECONNREFUSED/EHOSTUNREACH),
        // 能把"网络明确拒绝"与"静默丢包超时"区分开
        if (connect(sockFd, ai->ai_addr, ai->ai_addrlen) < 0) {
            SLOG_WARN << "NtpTester: connect failed for [" << addrBuf << "]: " << strerror(errno);
            return Result::Unreachable;
        }

        // 构造规范客户端请求: 填充发送时间戳(transmit ts).
        // 部分严格配置的服务器会丢弃全零时间戳的异常报文, 导致探测假失败
        NtpPacket pkt {};
        pkt.flags = 0x23;  // LI=0, VN=4, Mode=3(client)
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(now).count() % 1000000;
        pkt.txTm_s = htonl(static_cast<uint32_t>(secs + 2208988800));  // NTP纪元(1900)偏移
        pkt.txTm_f = htonl(static_cast<uint32_t>((static_cast<uint64_t>(usecs) << 32) / 1000000));

        ssize_t sent = send(sockFd, &pkt, sizeof(pkt), 0);
        if (sent != sizeof(pkt)) {
            SLOG_WARN << "NtpTester: send failed: " << strerror(errno);
            if (errno == ECONNREFUSED || errno == EHOSTUNREACH || errno == ENETUNREACH) {
                return Result::Unreachable;
            }
            return Result::SendFailed;
        }

        NtpPacket resp {};
        ssize_t recvd = recv(sockFd, &resp, sizeof(resp), 0);
        if (recvd < static_cast<ssize_t>(sizeof(NtpPacket))) {
            SLOG_WARN << "NtpTester: no valid response from [" << addrBuf << "]" << " (" << strerror(errno) << ")";
            if (errno == ECONNREFUSED || errno == EHOSTUNREACH || errno == ENETUNREACH) {
                return Result::Unreachable;  // 对端/中间设备明确拒绝
            }
            return Result::ReceiveFailed;  // 静默超时(SO_RCVTIMEO), 由上层ICMP细分
        }

        // 校验
        uint8_t mode = resp.flags & 0x07;
        if (mode != 4 && mode != 5) {  // 4=server, 5=broadcast
            SLOG_WARN << "NtpTester: unexpected NTP mode " << (int)mode << " from [" << addrBuf << "]";
            return Result::InvalidResponse;
        }
        if (resp.stratum == 0) {  // stratum 0 = Kiss-o'-Death, 服务器明确拒绝(限流/策略)
            SLOG_WARN << "NtpTester: server [" << addrBuf << "] sent KoD (stratum 0), denied";
            return Result::InvalidResponse;
        }

        SLOG_INFO << "NtpTester: server [" << addrBuf << "] responded OK" << ", stratum=" << (int)resp.stratum;
        return Result::Success;
    }

}  // namespace qifeng_ca
