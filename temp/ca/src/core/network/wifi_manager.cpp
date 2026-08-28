//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "core/network/wifi_manager.h"

namespace qifeng_ca {

    // Wi-Fi 扫描/连接命令超时(秒)
    static constexpr int32_t WifiCmdTimeoutSec = 10;

    WifiManager::WifiStatusCache WifiManager::StatusCache {};

    // Shell 单引号转义: 将 arg 用单引号包裹, 内部单引号转为 '\''
    // 保证 CLI 参数中的特殊字符不会被 shell 解释
    static std::string ShellEscape(const std::string &arg) {
        std::string out;
        out.reserve(arg.size() + 2);
        out.push_back('\'');
        for (char c : arg) {
            if (c == '\'') {
                out += "'\\''";
            } else {
                out.push_back(c);
            }
        }
        out.push_back('\'');
        return out;
    }

    // 判断是否为非数据行([INFO]/[WARNING]/[ERROR]/[SUCCESS]/---- 等提示行)
    static bool IsNonDataLine(std::string_view line) {
        if (line.empty()) {
            return true;
        }
        char front = line.front();
        if (front == '[') {
            return true;
        }
        // 以 "----" 开头的分隔线
        if (front == '-' && line.size() >= 2 && line[1] == '-') {
            return true;
        }
        return false;
    }

    // 解析信号强度: 从 "-82.00 dBm" 中提取 -82
    static bool ParseSignalDbm(std::string_view line, int32_t &outDbm) {
        auto dbmPos = line.find("dBm");
        if (dbmPos == std::string_view::npos) {
            return false;
        }
        auto scanEnd = dbmPos;
        while (scanEnd > 0 && std::isspace(static_cast<unsigned char>(line[scanEnd - 1]))) {
            --scanEnd;
        }
        auto scanStart = scanEnd;
        while (scanStart > 0) {
            char c = line[scanStart - 1];
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-') {
                --scanStart;
            } else {
                break;
            }
        }
        if (scanStart >= scanEnd) {
            return false;
        }
        try {
            outDbm = std::stoi(std::string {line.substr(scanStart, scanEnd - scanStart)});
        } catch (...) {
            return false;
        }
        return true;
    }

    // 解析 SSID: 从 '"SSID"' 中提取双引号内的内容
    static bool ParseQuotedSsid(std::string_view line, std::string &outSsid) {
        auto firstQuote = line.find('"');
        if (firstQuote == std::string_view::npos) {
            return false;
        }
        auto secondQuote = line.find('"', firstQuote + 1);
        if (secondQuote == std::string_view::npos) {
            return false;
        }
        outSsid = std::string {line.substr(firstQuote + 1, secondQuote - firstQuote - 1)};
        return !outSsid.empty();
    }

    // 从 status 输出行解析 key=value 对
    static bool ParseStatusKV(std::string_view line, std::string &outKey, std::string &outVal) {
        auto pos = line.find('=');
        if (pos == std::string_view::npos) {
            return false;
        }
        outKey = std::string {line.substr(0, pos)};
        outVal = std::string {line.substr(pos + 1)};
        return !outKey.empty();
    }

    // 从一行扫描输出中解析 SSID 和 dBm
    // 实际格式: "-82.00 dBm      "ES-CA500-999999"" (信号在前, SSID 在双引号内)
    static bool ParseScanLine(std::string_view line, WifiScanItem &outInfo) {
        if (IsNonDataLine(line)) {
            return false;
        }
        if (!ParseSignalDbm(line, outInfo.mSignalDbm)) {
            return false;
        }

        return ParseQuotedSsid(line, outInfo.mSsid);
    }

    int WifiManager::RunCmd(std::string_view cmd, std::string &outOutput) {
        std::string fullCmd {cmd};
        fullCmd += " 2>&1";  // 合并 stderr 到 stdout 便于诊断
        SLOG_DEBUG << "WifiManager RunCmd: " << fullCmd;
        FILE* pipe = popen(fullCmd.c_str(), "r");
        if (pipe == nullptr) {
            SLOG_ERROR << "WifiManager RunCmd: popen failed, cmd=" << fullCmd;
            return -1;
        }
        std::array<char, 256> buf {};
        while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
            outOutput += buf.data();
        }
        int ret = pclose(pipe);
        int exitCode = WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
        SLOG_DEBUG << "WifiManager RunCmd: exitCode=" << exitCode << ", outputLen=" << outOutput.size();
        return exitCode;
    }

    Status WifiManager::ScanWifi(std::vector<WifiScanItem> &outList) {
        outList.clear();
        std::string cmd = "timeout ";
        cmd += std::to_string(WifiCmdTimeoutSec);
        cmd += " qifeng_wifi_manage scan";
        std::string output;
        int code = RunCmdWithRetry(cmd, output, 2, "ScanWifi");
        if (code != 0) {
            SLOG_ERROR << "ScanWifi: command failed, code=" << code;
            return Status {-1, "Wi-Fi 扫描失败"};
        }
        // 按行解析输出
        size_t start = 0;
        while (start < output.size()) {
            auto end = output.find('\n', start);
            std::string line = (end == std::string::npos) ? output.substr(start) : output.substr(start, end - start);
            WifiScanItem info;
            if (ParseScanLine(line, info)) {
                outList.push_back(std::move(info));
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        SLOG_DEBUG << "ScanWifi: found " << outList.size() << " networks";
        return {};
    }

    Status WifiManager::ConnectWifi(const std::string &ssid, const std::string &password) {
        // 无论成功失败都清除缓存
        {
            std::lock_guard<std::mutex> lock(StatusCache.mStatusCacheMutex);
            StatusCache.mStatusCacheValid = false;
        }
        // 1. 配置 SSID 和密码
        std::string cfgCmd = "timeout ";
        cfgCmd += std::to_string(WifiCmdTimeoutSec);
        cfgCmd += " qifeng_wifi_manage -s ";
        cfgCmd += ShellEscape(ssid);
        cfgCmd += " -p ";
        cfgCmd += ShellEscape(password);
        std::string cfgOutput;
        int cfgCode = RunCmd(cfgCmd, cfgOutput);
        if (cfgCode != 0) {
            SLOG_ERROR << "ConnectWifi: config failed, code=" << cfgCode;
            return Status {-1, "Wi-Fi 配置失败"};
        }
        // 2. 触发连接
        std::string connCmd = "timeout ";
        connCmd += std::to_string(WifiCmdTimeoutSec);
        connCmd += " qifeng_wifi_manage connect";
        std::string connOutput;
        int connCode = RunCmd(connCmd, connOutput);
        if (connCode != 0) {
            SLOG_ERROR << "ConnectWifi: connect failed, code=" << connCode << ", output=" << connOutput;
            return Status {-1, "Wi-Fi 连接失败"};
        }
        SLOG_DEBUG << "ConnectWifi: connected to ssid=" << ssid;
        // 连接wifi后主动关闭热点, 保证wifi与热点不共存
        Status apDownStatus = ApInterfaceDown();
        if (!apDownStatus.IsSuccess()) {
            SLOG_WARN << "ConnectWifi: disable hotspot failed after wifi connect, err=" << apDownStatus.GetMsg();
        }
        return {};
    }

    int WifiManager::RunCmdWithRetry(std::string_view cmd, std::string &outOutput, int maxRetries,
                                     const std::string &logTag) {
        int code = -1;
        for (int attempt = 0; attempt <= maxRetries; ++attempt) {
            outOutput.clear();
            code = RunCmd(cmd, outOutput);
            if (code == 0) {
                return 0;
            }
            SLOG_WARN << logTag << ": command failed, attempt=" << attempt << ", code=" << code;
        }
        SLOG_ERROR << logTag << ": command failed after retries, code=" << code;
        return code;
    }

    // 实际执行 status 命令并解析结果(无缓存, 无锁)
    Status WifiManager::FetchWifiStatus(WifiConnectionStatus &outStatus) {
        std::string cmd = "timeout ";
        cmd += std::to_string(WifiCmdTimeoutSec);
        cmd += " qifeng_wifi_manage status";
        std::string output;
        int code = RunCmdWithRetry(cmd, output, 2, "GetWifiStatus");
        if (code != 0) {
            SLOG_ERROR << "GetWifiStatus: command failed, code=" << code;
            return Status {-1, "Wi-Fi 状态查询失败"};
        }
        for (size_t start = 0; start < output.size();) {
            auto end = output.find('\n', start);
            std::string line = (end == std::string::npos) ? output.substr(start) : output.substr(start, end - start);
            std::string key, val;
            if (ParseStatusKV(line, key, val)) {
                if (key == "ssid") {
                    outStatus.mSsid = std::move(val);
                } else if (key == "key_mgmt") {
                    outStatus.mKeyMgmt = std::move(val);
                } else if (key == "wpa_state") {
                    outStatus.mConnected = (val == "COMPLETED");
                    outStatus.mWpaState = std::move(val);
                } else if (key == "ip_address") {
                    outStatus.mIpAddress = std::move(val);
                }
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        SLOG_DEBUG << "GetWifiStatus: ssid=" << outStatus.mSsid << ", connected=" << outStatus.mConnected;
        if (!outStatus.mConnected) {
            outStatus.mInterfaceDown = WifiManager::CheckInterfaceDown();
            // wifi未连接时主动开启热点, 保证wifi与热点不共存
            EnsureHotspotWhenWifiDisconnected();
        } else if (outStatus.mConnected && CheckApInterfaceUp()) {
            // 因为有时wifi已经连接但是Connect却返回失败，所以这里还要做校验和关闭，防止AC连接同时AP又开启
            ApInterfaceDown();
        }
        return {};
    }

    void WifiManager::EnsureHotspotWhenWifiDisconnected() {
        // 仅在热点当前关闭时才执行, 避免重复调用
        if (CheckApInterfaceUp()) {
            return;
        }
        SLOG_INFO << "EnsureHotspotWhenWifiDisconnected: wifi disconnected, enabling hotspot";
        Status apUpStatus = ApInterfaceUp();
        if (!apUpStatus.IsSuccess()) {
            SLOG_WARN << "EnsureHotspotWhenWifiDisconnected: enable hotspot failed, err=" << apUpStatus.GetMsg();
        }
    }

    // 查询当前 Wi-Fi 连接信息(带缓存: 成功60s)
    Status WifiManager::GetWifiStatus(WifiConnectionStatus &outStatus) {
        int64_t nowMs = static_cast<int64_t>(GetTimeMs());
        {
            std::lock_guard<std::mutex> lock(StatusCache.mStatusCacheMutex);
            if (StatusCache.mStatusCacheValid && (nowMs - StatusCache.mStatusCacheMs) < StatusCache.mStatusCacheTtlMs) {
                outStatus = StatusCache.mStatusCache;
                return {};
            }
        }
        WifiConnectionStatus fresh;
        bool ok = FetchWifiStatus(fresh).IsSuccess();
        {
            std::lock_guard<std::mutex> lock(StatusCache.mStatusCacheMutex);
            nowMs = static_cast<int64_t>(GetTimeMs());
            if (ok && fresh.mConnected && !fresh.mIpAddress.empty()) {
                StatusCache.mStatusCacheTtlMs = 60000;  // 连接成功: 缓存60s
                StatusCache.mStatusCache = fresh;
                StatusCache.mStatusCacheValid = true;
            } else {
                StatusCache.mStatusCacheValid = false;  // 没有结果直接标记为无效缓存
            }
            StatusCache.mStatusCacheMs = nowMs;
            outStatus = std::move(fresh);
        }
        return {};
    }

    bool WifiManager::CheckInterfaceDown() {
        std::ifstream ifs("/sys/class/net/wlan0/operstate");
        if (!ifs.is_open()) {
            return false;
        }
        std::string state;
        std::getline(ifs, state);
        state.erase(0, state.find_first_not_of(" \t\r\n"));
        state.erase(state.find_last_not_of(" \t\r\n") + 1);
        return state == "down";
    }

    bool WifiManager::CheckApInterfaceUp() {
        // 通过 ip a show ap0 输出解析 flags 行, 判断是否包含 UP 标志
        std::string output;
        int code = RunCmd("ip a show ap0", output);
        if (code != 0) {
            return false;
        }
        size_t pos = output.find("state ");
        if (pos == std::string::npos) {
            SLOG_ERROR << "CheckApInterfaceUp: state not found in output";
            return false;
        }
        pos += 6;
        size_t end = output.find(' ', pos);
        if (end == std::string::npos) {
            end = output.size();
        }
        return output.substr(pos, end - pos) == "UP";
    }

    Status WifiManager::SetDomain(const std::string &domain) {
        // 1. 修改域名(仅更新 dnsmasq 配置, 不立即生效)
        std::string setCmd = "timeout ";
        setCmd += std::to_string(WifiCmdTimeoutSec);
        setCmd += " qifeng_network_manage -d ";
        setCmd += ShellEscape(domain);
        std::string setOutput;
        int setCode = RunCmd(setCmd, setOutput);
        if (setCode != 0) {
            SLOG_ERROR << "SetDomain: set failed, code=" << setCode << ", output=" << setOutput;
            return Status {-1, "域名设置失败"};
        }
        // 2. 应用配置(netplan apply + 重启 dnsmasq)使域名生效
        std::string applyCmd = "timeout ";
        applyCmd += std::to_string(WifiCmdTimeoutSec);
        applyCmd += " qifeng_network_manage -a";
        std::string applyOutput;
        int applyCode = RunCmd(applyCmd, applyOutput);
        if (applyCode != 0) {
            SLOG_ERROR << "SetDomain: apply failed, code=" << applyCode << ", output=" << applyOutput;
            return Status {-1, "域名应用失败"};
        }
        SLOG_DEBUG << "SetDomain: domain=" << domain << " applied";
        return {};
    }

    Status WifiManager::GetDomain(std::string &outDomain) {
        std::string cmd = "timeout ";
        cmd += std::to_string(WifiCmdTimeoutSec);
        cmd += " qifeng_network_manage -g";
        std::string output;
        int code = RunCmd(cmd, output);
        if (code != 0) {
            SLOG_ERROR << "GetDomain: command failed, code=" << code << ", output=" << output;
            return Status {-1, "域名查询失败"};
        }
        // 去除尾部换行符
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
            output.pop_back();
        }
        outDomain = std::move(output);
        SLOG_DEBUG << "GetDomain: domain=" << outDomain;
        return {};
    }

    Status WifiManager::GetApConfig(ApConfigInfo &outConfig) {
        return {}; // m50 没有热点信息

        std::string cmd = "timeout ";
        cmd += std::to_string(WifiCmdTimeoutSec);
        cmd += " qifeng_ap_manage -v";
        std::string output;
        int code = RunCmd(cmd, output);
        if (code != 0) {
            SLOG_ERROR << "GetApConfig: command failed, code=" << code << ", output=" << output;
            return Status {-1, "热点配置查询失败"};
        }
        // 解析输出: "SSID: xxx" 和 "密码: xxx"
        size_t ssidPos = output.find("SSID:");
        if (ssidPos != std::string::npos) {
            auto start = ssidPos + 5;
            auto end = output.find('\n', start);
            std::string ssid = output.substr(start, end - start);
            ssid.erase(0, ssid.find_first_not_of(" \t\r\n"));
            ssid.erase(ssid.find_last_not_of(" \t\r\n") + 1);
            outConfig.mSsid = std::move(ssid);
        }
        size_t pwdPos = output.find("密码:");
        if (pwdPos != std::string::npos) {
            auto colonPos = output.find(':', pwdPos);
            if (colonPos != std::string::npos) {
                auto start = colonPos + 1;
                auto end = output.find('\n', start);
                std::string pwd = output.substr(start, end - start);
                pwd.erase(0, pwd.find_first_not_of(" \t\r\n"));
                pwd.erase(pwd.find_last_not_of(" \t\r\n") + 1);
                outConfig.mPassword = std::move(pwd);
            }
        }
        SLOG_DEBUG << "GetApConfig: ssid=" << outConfig.mSsid;
        // 查询 ap0 网卡是否已开启(ifconfig flags 中是否包含 UP)
        outConfig.mApEnabled = CheckApInterfaceUp();
        SLOG_DEBUG << "GetApConfig: apEnabled=" << outConfig.mApEnabled;
        return {};
    }

    Status WifiManager::GetApClients(ApClientsInfo &outInfo) {
        return {}; // m50 没有热点信息
        
        std::string cmd = "timeout ";
        cmd += std::to_string(WifiCmdTimeoutSec);
        cmd += " qifeng_ap_manage -c";
        std::string output;
        int code = RunCmd(cmd, output);
        if (code != 0) {
            SLOG_ERROR << "GetApClients: command failed, code=" << code;
            return Status {-1, "热点客户端查询失败"};
        }
        // 解析输出: "已连接设备: X台"
        auto pos = output.find(":");
        if (pos != std::string::npos) {
            auto start = pos + 1;
            auto end = output.find_first_of("台\n", start);
            std::string numStr = output.substr(start, end - start);
            numStr.erase(0, numStr.find_first_not_of(" \t\r\n"));
            try {
                outInfo.mClientCount = std::stoi(numStr);
            } catch (...) {
                outInfo.mClientCount = 0;
            }
        }
        SLOG_DEBUG << "GetApClients: clientCount=" << outInfo.mClientCount;
        return {};
    }

    Status WifiManager::SetApConfig(const std::string &ssid, const std::string &password) {
        std::string cmd = "timeout ";
        cmd += std::to_string(WifiCmdTimeoutSec);
        cmd += " qifeng_ap_manage -s ";
        cmd += ShellEscape(ssid);
        cmd += " -p ";
        cmd += ShellEscape(password);
        std::string output;
        int code = RunCmd(cmd, output);
        if (code != 0) {
            SLOG_ERROR << "SetApConfig: command failed, code=" << code;
            return Status {-1, "热点配置修改失败"};
        }
        SLOG_DEBUG << "SetApConfig: ssid=" << ssid << " configured";
        return {};
    }

    Status WifiManager::ApplyApConfig() {
        std::string cmd = "timeout ";
        cmd += std::to_string(WifiCmdTimeoutSec);
        cmd += " qifeng_ap_manage -a";
        std::string output;
        int code = RunCmd(cmd, output);
        if (code != 0) {
            SLOG_ERROR << "ApplyApConfig: command failed, code=" << code;
            return Status {-1, "热点服务重启失败"};
        }
        SLOG_DEBUG << "ApplyApConfig: service restarted";
        return {};
    }

    Status WifiManager::WifiInterfaceDown() {
        {
            std::lock_guard<std::mutex> lock(StatusCache.mStatusCacheMutex);
            StatusCache.mStatusCacheValid = false;
        }
        std::string cmd = "ifconfig wlan0 down 2>&1";
        std::string output;
        int code = RunCmd(cmd, output);
        if (code != 0) {
            SLOG_ERROR << "WifiInterfaceDown: failed, code=" << code << ", output=" << output;
            return Status {-1, "关闭Wi-Fi失败"};
        }
        SLOG_DEBUG << "WifiInterfaceDown: wlan0 down";
        return {};
    }

    Status WifiManager::WifiInterfaceUp() {
        {
            std::lock_guard<std::mutex> lock(StatusCache.mStatusCacheMutex);
            StatusCache.mStatusCacheValid = false;
        }
        std::string cmd = "ifconfig wlan0 up 2>&1";
        std::string output;
        int code = RunCmd(cmd, output);
        if (code != 0) {
            SLOG_ERROR << "WifiInterfaceUp: failed, code=" << code << ", output=" << output;
            return Status {-1, "开启Wi-Fi失败"};
        }
        SLOG_DEBUG << "WifiInterfaceUp: wlan0 up";
        return {};
    }

    Status WifiManager::WifiDisconnect() {
        {
            std::lock_guard<std::mutex> lock(StatusCache.mStatusCacheMutex);
            StatusCache.mStatusCacheValid = false;
        }
        std::string cmd = "wpa_cli -i wlan0 disconnect 2>&1";
        std::string output;
        int code = RunCmd(cmd, output);
        if (code != 0) {
            SLOG_ERROR << "WifiDisconnect: failed, code=" << code << ", output=" << output;
            return Status {-1, "断开Wi-Fi连接失败"};
        }
        // 检查返回是否为 OK
        std::string trimmed = output;
        while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
            trimmed.pop_back();
        }
        if (trimmed != "OK") {
            SLOG_WARN << "WifiDisconnect: unexpected response, output=" << output;
        }
        SLOG_DEBUG << "WifiDisconnect: disconnected";
        return {};
    }

    Status WifiManager::ApInterfaceDown() {
        // ifconfig ap0 down 有时返回退出码0但网卡仍为UP, 需要验证实际状态并重试
        constexpr int kMaxRetries = 5;
        std::string cmd = "systemctl stop hostapd 2>&1";
        for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
            std::string output;
            int code = RunCmd(cmd, output);
            if (code != 0) {
                SLOG_WARN << "ApInterfaceDown: command failed, attempt=" << attempt << ", code=" << code
                          << ", output=" << output;
            }
            // 验证 ap0 是否确实已关闭( flags 中不含 UP )
            if (!CheckApInterfaceUp()) {
                SLOG_INFO << "ApInterfaceDown: ap0 down verified, attempt=" << attempt;
                return {};
            }
            SLOG_WARN << "ApInterfaceDown: ap0 still UP after attempt=" << attempt << ", retrying";
        }
        SLOG_ERROR << "ApInterfaceDown: failed to bring ap0 down after " << kMaxRetries << " attempts";
        return Status {-1, "关闭热点失败"};
    }

    Status WifiManager::ApInterfaceUp() {
        // 与 ApInterfaceDown 对称: 验证实际状态并重试
        constexpr int kMaxRetries = 5;
        std::string cmd = "systemctl start hostapd 2>&1";
        for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
            std::string output;
            int code = RunCmd(cmd, output);
            if (code != 0) {
                SLOG_WARN << "ApInterfaceUp: command failed, attempt=" << attempt << ", code=" << code
                          << ", output=" << output;
            }
            // 验证 ap0 是否确实已开启( flags 中包含 UP )
            if (CheckApInterfaceUp()) {
                SLOG_INFO << "ApInterfaceUp: ap0 up verified, attempt=" << attempt;
                return {};
            }
            SLOG_WARN << "ApInterfaceUp: ap0 still DOWN after attempt=" << attempt << ", retrying";
        }
        SLOG_ERROR << "ApInterfaceUp: failed to bring ap0 up after " << kMaxRetries << " attempts";
        return Status {-1, "开启热点失败"};
    }

    bool WifiManager::HasWifiDevice() {
        static int64_t LastCheckMs = 0;
        static bool CachedResult = false;
        static std::mutex CacheMutex;

        int64_t nowMs = static_cast<int64_t>(GetTimeMs());
        {
            std::lock_guard<std::mutex> lock(CacheMutex);
            if (nowMs - LastCheckMs < 60000 && LastCheckMs > 0) {
                return CachedResult;
            }
        }

        // 1. 检查 wlan0 网卡是否存在
        std::string ifCmd = "ifconfig wlan0 2>&1";
        std::string ifOutput;
        int ifCode = RunCmd(ifCmd, ifOutput);
        bool interfaceExists = (ifCode == 0) && (ifOutput.find("wlan0") != std::string::npos ||
                                                 ifOutput.find("flags") != std::string::npos);

        // 2. 检查 wpa_cli 命令是否存在(Wi-Fi 管理依赖 wpa_cli)
        bool exists = interfaceExists;
        if (interfaceExists) {
            std::string wpaCmd = "command -v wpa_cli 2>&1";
            std::string wpaOutput;
            int wpaCode = RunCmd(wpaCmd, wpaOutput);
            if (wpaCode != 0 || wpaOutput.find("wpa_cli") == std::string::npos) {
                SLOG_WARN << "HasWifiDevice: wlan0 exists but wpa_cli not found";
                exists = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(CacheMutex);
            LastCheckMs = nowMs;
            CachedResult = exists;
        }

        SLOG_DEBUG << "HasWifiDevice: " << (exists ? "yes" : "no");
        return exists;
    }

}  // namespace qifeng_ca
