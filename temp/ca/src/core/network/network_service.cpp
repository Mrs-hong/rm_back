//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <cstdio>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <vector>

#include "common/config/network_config.h"
#include "common/utils/ipv4_utils.h"
#include "core/network/network_service.h"
#include "qifeng_framework/common/logger.h"

namespace qifeng_ca {

    namespace {
        // 去除首尾空白字符
        std::string TrimStr(const std::string &s) {
            size_t begin = s.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                return "";
            }
            size_t end = s.find_last_not_of(" \t\r\n");
            return s.substr(begin, end - begin + 1);
        }

        // 执行 netplan 命令, 返回退出码
        int RunNetplanCmd(const std::string &args, std::string &outOutput) {
            std::string cmd = "timeout ";
            cmd += std::to_string(30);
            cmd += " netplan ";
            cmd += args;
            cmd += " 2>&1";
            SLOG_DEBUG << "RunNetplanCmd: " << cmd;
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe == nullptr) {
                SLOG_ERROR << "RunNetplanCmd: popen failed, cmd=" << cmd;
                return -1;
            }
            std::array<char, 256> buf {};
            while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
                outOutput += buf.data();
            }
            int ret = pclose(pipe);
            int exitCode = WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
            SLOG_DEBUG << "RunNetplanCmd: exitCode=" << exitCode;
            return exitCode;
        }

        // 从 "key: value" 行提取 value, 值为 null 返回空串
        static std::string ExtractKeyValue(const std::string &line, const std::string &key) {
            if (line.find(key) != 0) {
                return "";
            }
            std::string value = TrimStr(line.substr(key.size()));
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            return (value == "null") ? "" : value;
        }

        // 从地址项行("- ...")提取去引号的地址, 非地址行返回空串
        static std::string ExtractAddressItem(const std::string &line) {
            if (line.find("- ") != 0) {
                return "";
            }
            std::string addr = TrimStr(line.substr(2));
            if (addr.size() >= 2 && addr.front() == '"' && addr.back() == '"') {
                addr = addr.substr(1, addr.size() - 2);
            }
            return addr;
        }

        // 解析 netplan get 输出的一行, 提取 IP/前缀与网关(带状态)
        static void ParseNetplanGetLine(const std::string &trimmed, bool &inRoutes, std::string &ipWithPrefix,
                                        std::string &gateway) {
            if (trimmed.empty() || trimmed == "null") {
                return;
            }
            // routes 块: 取第一个 via 作为默认网关(set 端写入的格式)
            if (trimmed.find("routes:") == 0) {
                inRoutes = true;
                return;
            }
            if (inRoutes) {
                // routes 块为 set 端写入的权威格式: via 直接覆盖 gateway4，优先采用 routes
                std::string via = ExtractKeyValue(trimmed, "via:");
                if (!via.empty()) {
                    gateway = via;
                }
                return;
            }
            // gateway4: 旧格式网关于入口处兜底
            std::string gw = ExtractKeyValue(trimmed, "gateway4:");
            if (!gw.empty()) {
                gateway = gw;
                return;
            }
            // 地址项: "- "192.168.112.166/24"" 或 "- 8.8.8.8"(仅接受IPv4, 忽略IPv6地址项)
            std::string addr = ExtractAddressItem(trimmed);
            size_t slash = addr.find('/');
            if (slash != std::string::npos && Ipv4Utils::IsValidIpv4(addr.substr(0, slash))) {
                ipWithPrefix = addr;
            }
        }

        // 通过 netplan get 查询接口 IP/CIDR -> 掩码 与网关
        // 输出示例(netplan get ethernets.eth0):
        //   optional: true
        //   addresses:
        //   - "192.168.112.166/24"
        //   gateway4: 192.168.112.1
        //   dhcp4: false
        bool QueryNetplanIface(const std::string &iface, std::string &outIp, std::string &outNetmask,
                               std::string &outGateway) {
            std::string output;
            if (RunNetplanCmd("get ethernets." + iface, output) != 0) {
                SLOG_ERROR << "QueryNetplanIface: netplan get failed, iface=" << iface << ", output=" << output;
                return false;
            }

            std::istringstream stream(output);
            std::string line;
            std::string ipWithPrefix;
            bool inRoutes = false;
            while (std::getline(stream, line)) {
                ParseNetplanGetLine(TrimStr(line), inRoutes, ipWithPrefix, outGateway);
            }

            if (ipWithPrefix.empty()) {
                SLOG_WARN << "QueryNetplanIface: no address parsed, iface=" << iface << ", output=" << output;
                return false;
            }
            size_t slash = ipWithPrefix.find('/');
            outIp = ipWithPrefix.substr(0, slash);
            int prefix = std::stoi(ipWithPrefix.substr(slash + 1));
            if (!Ipv4Utils::CidrToNetmask(prefix, outNetmask)) {
                SLOG_ERROR << "QueryNetplanIface: CidrToNetmask failed, prefix=" << prefix;
                return false;
            }
            return true;
        }

        // 通过 netplan get 查询接口 DNS
        // 输出示例: "null" 或:
        //   addresses:
        //   - 8.8.8.8
        //   - 1.1.1.1
        bool QueryNetplanDns(const std::string &iface, std::vector<std::string> &outDns) {
            std::string output;
            if (RunNetplanCmd("get ethernets." + iface + ".nameservers", output) != 0) {
                SLOG_ERROR << "QueryNetplanDns: netplan get nameservers failed, iface=" << iface
                           << ", output=" << output;
                return false;
            }
            std::istringstream stream(output);
            std::string line;
            while (std::getline(stream, line)) {
                std::string trimmed = TrimStr(line);
                if (trimmed.find("- ") != 0) {
                    continue;
                }
                std::string dns = TrimStr(trimmed.substr(2));
                if (dns.size() >= 2 && dns.front() == '"' && dns.back() == '"') {
                    dns = dns.substr(1, dns.size() - 2);
                }
                if (DnsManager::IsValidDns(dns)) {
                    outDns.push_back(dns);
                }
            }
            return true;
        }

        // 逗号拼接 DNS 列表(供 netplan set 的列表参数使用)
        static std::string JoinDnsList(const std::vector<std::string> &dnsList) {
            std::string joined;
            for (size_t i = 0; i < dnsList.size(); ++i) {
                if (i > 0) {
                    joined += ", ";
                }
                joined += dnsList[i];
            }
            return joined;
        }

        // 使用 netplan set 写入接口配置并应用(与 database/docx/network 规范一致)
        // 命令序列(与设备管理命令一致):
        //   netplan set ethernets.<iface>.addresses=null          # 先清空旧地址, 避免列表合并残留
        //   netplan set ethernets.<iface>.addresses='[ip/cidr]'
        //   netplan set ethernets.<iface>.routes=null             # 先清空旧路由
        //   netplan set ethernets.<iface>.routes='[{"to":"default","via":"<gateway>"}]'
        //   netplan set ethernets.<iface>.nameservers.addresses='[dns...]'  # DNS非空时写入
        //   netplan set ethernets.<iface>.dhcp4=false             # 静态IP必须关闭DHCP
        //   netplan apply
        bool ApplyNetplanSetConfig(const NetplanConfig &netplanCfg) {
            const std::string &iface = netplanCfg.mInterface;
            const std::string &ip = netplanCfg.mIp;
            std::vector<std::string> steps {
                // 1. 配置IP: 先置 null 清空旧地址列表, 再写入静态地址
                "set ethernets." + iface + ".addresses=null",
                "set ethernets." + iface + ".addresses='[" + ip + "/" + std::to_string(netplanCfg.mCidr) + "]'",
                // 2. 配置网关: 先置 null 清空旧路由, 再写入默认路由
                "set ethernets." + iface + ".routes=null",
                "set ethernets." + iface + ".routes='[{\"to\":\"default\",\"via\":\"" + netplanCfg.mGateway + "\"}]'",
            };
            // 3. 配置DNS: 非空时写入(为空时保留现有DNS, 避免误清空)
            if (!netplanCfg.mDnsList.empty()) {
                steps.emplace_back("set ethernets." + iface + ".nameservers.addresses='[" +
                                   JoinDnsList(netplanCfg.mDnsList) + "]'");
            }
            // 4. 配置DHCP: 静态IP模式下关闭DHCP
            steps.emplace_back("set ethernets." + iface + ".dhcp4=false");

            std::string output;
            for (const auto &step : steps) {
                if (RunNetplanCmd(step, output) != 0) {
                    SLOG_ERROR << "ApplyNetplanSetConfig: netplan " << step << " failed, output=" << output;
                    return false;
                }
            }
            // 清除旧 gateway4 字段, 避免与 routes 默认路由同时声明导致冲突(netplan 告警/生效路由不确定)
            // 尽力而为: 键不存在时失败仅告警, 不影响主流程
            if (RunNetplanCmd("unset ethernets." + iface + ".gateway4", output) != 0) {
                SLOG_WARN << "ApplyNetplanSetConfig: unset gateway4 skipped, output=" << output;
            }
            // 5. 设置生效
            if (RunNetplanCmd("apply", output) != 0) {
                SLOG_ERROR << "ApplyNetplanSetConfig: netplan apply failed, output=" << output;
                return false;
            }
            return true;
        }
    }  // namespace

    std::shared_mutex NetworkService::RwMutex;
    NetworkInfoCache NetworkService::NetCache;

    Status NetworkService::ValidateNetworkParams(const std::string &ip, const std::string &netmask,
                                                 const std::string &gateway, SetNetworkResponse* resp) {
        // 验证子网掩码合法性(连续1+连续0)
        if (!Ipv4Utils::IsValidNetmask(netmask)) {
            if (resp) {
                resp->set_netmask(netmask);
            }
            return Status {-1, "子网掩码不合法"};
        }
        // 验证IP是否为有效主机地址(非网络地址/广播地址)
        if (!Ipv4Utils::IsValidHostIp(ip, netmask)) {
            if (resp) {
                resp->set_ip(ip);
            }
            return Status {-1, "IP地址不合法"};
        }
        // 验证网关是否为有效主机地址
        if (!Ipv4Utils::IsValidHostIp(gateway, netmask)) {
            if (resp) {
                resp->set_gateway(gateway);
            }
            return Status {-1, "网关地址不合法"};
        }
        // IP与网关不能相同
        if (Ipv4Utils::IsSameIp(ip, gateway)) {
            if (resp) {
                resp->set_ip(ip);
                resp->set_gateway(gateway);
            }
            return Status {-1, "IP地址与网关不能相同"};
        }
        // IP与网关必须在同一子网
        if (!Ipv4Utils::IsSameSubnet(ip, netmask, gateway)) {
            if (resp) {
                resp->set_ip(ip);
                resp->set_netmask(netmask);
                resp->set_gateway(gateway);
            }
            return Status {-1, "IP与网关不在同一子网"};
        }
        return Status {};
    }

    Status NetworkService::CarrierOrError(const std::string &iface, const std::string &origMsg, int code) {
        if (!mInterfaceManager.IsCarrierUp(iface)) {
            return Status {-1, "当前网线未正确连接"};
        }
        return Status {code, origMsg};
    }

    Status NetworkService::CollectNetworkInfo() {
        auto &cfg = NetworkConfig::GetInstance();
        std::string iface = cfg.GetDefaultInterface();

        // 从InterfaceManager获取网卡IP和掩码
        InterfaceInfo ifaceInfo;
        if (!mInterfaceManager.GetInterfaceInfo(iface, ifaceInfo)) {
            return CarrierOrError(iface, "获取网卡信息失败");
        }
        if (ifaceInfo.mIpv4.empty()) {
            return CarrierOrError(iface, "获取IP地址失败");
        }
        if (ifaceInfo.mNetmask.empty()) {
            return CarrierOrError(iface, "获取子网掩码失败");
        }

        // 从RouteManager获取默认网关
        std::string gateway;
        if (!mRouteManager.GetDefaultGateway(iface, gateway)) {
            return Status {-1, "获取网关失败"};
        }

        // 从netplan YAML配置文件读取DNS (由SetNetwork/ResetNetwork写入, 是唯一权威来源)
        std::vector<std::string> dnsList;
        if (!mNetplanManager.ReadDnsFromConfig(iface, cfg.GetNetplanConfigPath(), dnsList)) {
            // return Status {-1, "获取DNS失败"}; // 失败则是无结果
        }

        // 同步更新缓存
        NetCache.mIp = ifaceInfo.mIpv4;
        NetCache.mNetmask = ifaceInfo.mNetmask;
        NetCache.mGateway = gateway;
        NetCache.mDnsList = dnsList;

        return Status {};
    }

    void NetworkService::InvalidateCache() {
        NetCache.mValid = false;
        NetCache.mIp.clear();
        NetCache.mNetmask.clear();
        NetCache.mGateway.clear();
        NetCache.mDnsList.clear();
    }

    Status NetworkService::ApplyNetplanConfig(const NetplanConfig &netplanCfg) {
        auto &cfg = NetworkConfig::GetInstance();
        std::unique_lock<std::shared_mutex> writeLock(RwMutex);
        Status result = mNetplanManager.ApplyConfig(netplanCfg, cfg.GetNetplanConfigPath());
        if (result.IsSuccess()) {
            InvalidateCache();
        }
        return result;
    }

    Status NetworkService::GetNetworkInfo(GetNetworkResponse* resp) {
        // 版本开关: v2 走新版逻辑(含域名管理), v1 走原逻辑(保持不变)
        if (NetworkConfig::GetInstance().GetFeatureVersion() == "v2") {
            return GetNetworkInfoV2(resp);
        }
        // 优先从缓存获取(读锁保护)
        {
            std::shared_lock<std::shared_mutex> readLock(RwMutex);
            if (NetCache.mValid) {
                resp->set_ip(NetCache.mIp);
                resp->set_netmask(NetCache.mNetmask);
                resp->set_gateway(NetCache.mGateway);
                for (const auto &dns : NetCache.mDnsList) {
                    resp->add_dns(dns);
                }
                return Status {};
            }
        }

        // 缓存未命中, 实时采集(写锁保护, 防止重复采集)
        std::unique_lock<std::shared_mutex> writeLock(RwMutex);
        // double-check: 其他线程可能已经更新了缓存
        if (NetCache.mValid) {
            resp->set_ip(NetCache.mIp);
            resp->set_netmask(NetCache.mNetmask);
            resp->set_gateway(NetCache.mGateway);
            for (const auto &dns : NetCache.mDnsList) {
                resp->add_dns(dns);
            }
            return Status {};
        }

        Status status = CollectNetworkInfo();
        if (status.IsSuccess()) {
            resp->set_ip(NetCache.mIp);
            resp->set_netmask(NetCache.mNetmask);
            resp->set_gateway(NetCache.mGateway);
            for (const auto &dns : NetCache.mDnsList) {
                resp->add_dns(dns);
            }
            NetCache.mValid = true;
        }
        return status;
    }

    Status NetworkService::SetNetwork(const SetNetworkRequest &req, SetNetworkResponse* resp) {
        // 版本开关: v2 走新版逻辑(含域名管理), v1 走原逻辑(保持不变)
        if (NetworkConfig::GetInstance().GetFeatureVersion() == "v2") {
            return SetNetworkV2(req, resp);
        }
        if (req.ip().empty() || req.netmask().empty() || req.gateway().empty()) {
            return Status {-1, "参数不完整"};
        }

        std::vector<std::string> dnsList;
        for (int i = 0; i < req.dns_size(); ++i) {
            if (!DnsManager::IsValidDns(req.dns(i))) {
                return Status {-1, "DNS地址不合法: " + req.dns(i)};
            }
            dnsList.push_back(req.dns(i));
        }

        Status validStatus = ValidateNetworkParams(req.ip(), req.netmask(), req.gateway(), resp);
        if (!validStatus.IsSuccess()) {
            return validStatus;
        }

        int cidr = 0;
        if (!Ipv4Utils::NetmaskToCidr(req.netmask(), cidr)) {
            return Status {-1, "子网掩码转换CIDR失败"};
        }

        // IP冲突检测: 仅在IP有变化时才做检测(同IP跳过)
        {
            std::shared_lock<std::shared_mutex> readLock(RwMutex);
            if (!NetCache.mValid || NetCache.mIp != req.ip()) {
                readLock.unlock();
                if (Ipv4Utils::IsIpReachable(req.ip())) {
                    return Status {-1, "IP地址冲突: " + req.ip()};
                }
            }
        }

        NetplanConfig netplanCfg;
        netplanCfg.mIp = req.ip();
        netplanCfg.mCidr = cidr;
        netplanCfg.mGateway = req.gateway();
        netplanCfg.mDnsList = dnsList;
        netplanCfg.mInterface = NetworkConfig::GetInstance().GetDefaultInterface();

        return ApplyNetplanConfig(netplanCfg);
    }

    Status NetworkService::ResetNetwork(SetNetworkResponse* resp) {
        // 版本开关: v2 走新版逻辑(含域名管理), v1 走原逻辑(保持不变)
        if (NetworkConfig::GetInstance().GetFeatureVersion() == "v2") {
            return ResetNetworkV2(resp);
        }
        // 从配置文件读取默认网络参数
        auto &cfg = NetworkConfig::GetInstance();
        std::string ip = cfg.GetDefaultIp();
        std::string netmask = cfg.GetDefaultNetmask();
        std::string gateway = cfg.GetDefaultGateway();

        // 解析默认DNS(逗号分隔)
        std::vector<std::string> dnsList;
        std::istringstream dnsStream(cfg.GetDefaultDns());
        std::string dns;
        while (std::getline(dnsStream, dns, ',')) {
            if (!dns.empty()) {
                dnsList.push_back(dns);
            }
        }

        // 校验默认参数合法性
        Status validStatus = ValidateNetworkParams(ip, netmask, gateway, resp);
        if (!validStatus.IsSuccess()) {
            return validStatus;
        }

        int cidr = 0;
        if (!Ipv4Utils::NetmaskToCidr(netmask, cidr)) {
            return Status {-1, "子网掩码转换CIDR失败"};
        }

        NetplanConfig netplanCfg;
        netplanCfg.mIp = ip;
        netplanCfg.mCidr = cidr;
        netplanCfg.mGateway = gateway;
        netplanCfg.mDnsList = dnsList;
        netplanCfg.mInterface = cfg.GetDefaultInterface();

        return ApplyNetplanConfig(netplanCfg);
    }

    // ===================== V2 版本实现 =====================
    // V2 与 V1 的差异: 额外集成域名/Wi-Fi 状态管理, 网络操作后同步域名配置

    Status NetworkService::GetNetworkInfoV2(GetNetworkResponse* resp) {
        // V2: 通过 netplan get 获取 IP/掩码/网关/DNS(与 SetNetworkV2/ResetNetworkV2 写入端同一数据源)
        auto &cfg = NetworkConfig::GetInstance();
        std::string iface = cfg.GetDefaultInterface();

        std::string ip;
        std::string netmask;
        std::string gateway;
        if (!QueryNetplanIface(iface, ip, netmask, gateway)) {
            SLOG_ERROR << "GetNetworkInfoV2: netplan get failed, iface=" << iface;
            return CarrierOrError(iface, "网络信息查询失败");
        }

        // 通过 netplan get 读取 DNS, 与写入端使用同一数据源
        std::vector<std::string> dnsList;
        (void)QueryNetplanDns(iface, dnsList);

        std::unique_lock<std::shared_mutex> writeLock(RwMutex);
        NetCache.mIp = ip;
        NetCache.mNetmask = netmask;
        NetCache.mGateway = gateway;
        NetCache.mDnsList = dnsList;
        NetCache.mValid = true;

        resp->set_ip(NetCache.mIp);
        resp->set_netmask(NetCache.mNetmask);
        resp->set_gateway(NetCache.mGateway);
        for (const auto &dns : NetCache.mDnsList) {
            resp->add_dns(dns);
        }
        writeLock.unlock();
        return Status {};
    }

    Status NetworkService::SetNetworkV2(const SetNetworkRequest &req, SetNetworkResponse* resp) {
        // V2: 参数校验, 然后使用 netplan set 设置并应用 IP/网关/DNS
        if (req.ip().empty() || req.netmask().empty() || req.gateway().empty()) {
            return Status {-1, "参数不完整"};
        }
        std::vector<std::string> dnsList;
        for (int i = 0; i < req.dns_size(); ++i) {
            if (!DnsManager::IsValidDns(req.dns(i))) {
                return Status {-1, "DNS地址不合法: " + req.dns(i)};
            }
            dnsList.push_back(req.dns(i));
        }
        Status validStatus = ValidateNetworkParams(req.ip(), req.netmask(), req.gateway(), resp);
        if (!validStatus.IsSuccess()) {
            return validStatus;
        }
        auto &cfg = NetworkConfig::GetInstance();
        int cidr = 0;
        if (!Ipv4Utils::NetmaskToCidr(req.netmask(), cidr)) {
            return Status {-1, "子网掩码转换CIDR失败"};
        }
        // IP冲突检测: 仅在IP有变化时才做检测(同IP跳过)
        {
            std::shared_lock<std::shared_mutex> readLock(RwMutex);
            if (!NetCache.mValid || NetCache.mIp != req.ip()) {
                readLock.unlock();
                if (Ipv4Utils::IsIpReachable(req.ip())) {
                    return Status {-1, "IP地址冲突: " + req.ip()};
                }
            }
        }
        std::unique_lock<std::shared_mutex> writeLock(RwMutex);
        NetplanConfig netplanCfg;
        netplanCfg.mInterface = cfg.GetDefaultInterface();
        netplanCfg.mIp = req.ip();
        netplanCfg.mCidr = cidr;
        netplanCfg.mGateway = req.gateway();
        netplanCfg.mDnsList = dnsList;
        if (!ApplyNetplanSetConfig(netplanCfg)) {
            return Status {-1, "网络配置应用失败"};
        }
        InvalidateCache();
        writeLock.unlock();
        return Status {};
    }

    Status NetworkService::ResetNetworkV2(SetNetworkResponse* resp) {
        // V2: 先执行 V1 原有逻辑(恢复默认网络), 再恢复默认域名
        auto &cfg = NetworkConfig::GetInstance();
        std::string ip = cfg.GetDefaultIp();
        std::string netmask = cfg.GetDefaultNetmask();
        std::string gateway = cfg.GetDefaultGateway();
        std::vector<std::string> dnsList;
        std::istringstream dnsStream(cfg.GetDefaultDns());
        std::string dns;
        while (std::getline(dnsStream, dns, ',')) {
            if (!dns.empty()) {
                dnsList.push_back(dns);
            }
        }
        Status validStatus = ValidateNetworkParams(ip, netmask, gateway, resp);
        if (!validStatus.IsSuccess()) {
            return validStatus;
        }
        int cidr = 0;
        if (!Ipv4Utils::NetmaskToCidr(netmask, cidr)) {
            return Status {-1, "子网掩码转换CIDR失败"};
        }
        std::unique_lock<std::shared_mutex> writeLock(RwMutex);
        NetplanConfig netplanCfg;
        netplanCfg.mInterface = cfg.GetDefaultInterface();
        netplanCfg.mIp = ip;
        netplanCfg.mCidr = cidr;
        netplanCfg.mGateway = gateway;
        netplanCfg.mDnsList = dnsList;
        if (!ApplyNetplanSetConfig(netplanCfg)) {
            return Status {-1, "网络配置应用失败"};
        }
        InvalidateCache();
        writeLock.unlock();
        return Status {};
    }

}  // namespace qifeng_ca
