//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_NETWORK_WIFI_MANAGER_H
#define QIFENG_CA_INCLUDE_CORE_NETWORK_WIFI_MANAGER_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "common/status.h"

namespace qifeng_ca {

    // Wi-Fi 扫描结果项
    struct WifiScanItem {
        std::string mSsid;
        int32_t mSignalDbm {0};
    };

    // Wi-Fi 连接状态信息
    struct WifiConnectionStatus {
        std::string mSsid;
        std::string mKeyMgmt;   // 如 WPA2-PSK
        std::string mWpaState;  // COMPLETED / DISCONNECTED 等
        std::string mIpAddress;
        bool mConnected {false};
        bool mInterfaceDown {false};  // 网卡是否关闭
    };

    // 热点配置与连接设备信息
    struct ApConfigInfo {
        std::string mSsid;
        std::string mPassword;
        bool mApEnabled {false};  // ap0 网卡是否已 UP
    };

    struct ApClientsInfo {
        int32_t mClientCount {0};
    };

    // Wi-Fi 与域名管理器
    class WifiManager {
    public:
        WifiManager() = default;
        ~WifiManager() = default;

        WifiManager(const WifiManager &) = delete;
        WifiManager &operator=(const WifiManager &) = delete;
        WifiManager(WifiManager &&) = delete;
        WifiManager &operator=(WifiManager &&) = delete;

        // 扫描可连接 Wi-Fi 列表 (信号强度 dBm)
        Status ScanWifi(std::vector<WifiScanItem> &outList);

        // 连接指定 Wi-Fi
        Status ConnectWifi(const std::string &ssid, const std::string &password);

        // 查询当前 Wi-Fi 连接信息
        Status GetWifiStatus(WifiConnectionStatus &outStatus);

        // 设置域名
        Status SetDomain(const std::string &domain);

        // 查询当前域名
        Status GetDomain(std::string &outDomain);

        // 查询热点配置(SSID/密码)
        Status GetApConfig(ApConfigInfo &outConfig);

        // 查询热点已连接设备数
        Status GetApClients(ApClientsInfo &outInfo);

        // 修改热点配置(SSID/密码, 不重启服务)
        Status SetApConfig(const std::string &ssid, const std::string &password);

        // 重启热点服务(使配置生效)
        Status ApplyApConfig();

        // Wi-Fi 接口开关
        Status WifiInterfaceUp();
        Status WifiInterfaceDown();

        // 断开当前 Wi-Fi 连接
        Status WifiDisconnect();

        // 热点接口上下电
        Status ApInterfaceUp();
        Status ApInterfaceDown();

        // 查询 Wi-Fi 设备是否存在(wlan0网卡)
        bool HasWifiDevice();

    private:
        // 执行 CLI 命令并捕获 stdout 输出, 返回退出码 (0=成功)
        // cmd 为完整命令字符串 (调用方负责 shell 转义)
        static int RunCmd(std::string_view cmd, std::string &outOutput);

        // 执行 CLI 命令, 失败时重试指定次数, 返回最终退出码
        static int RunCmdWithRetry(std::string_view cmd, std::string &outOutput, int maxRetries,
                                   const std::string &logTag);

        // 通过 scan 探测网卡是否关闭(Network is down)
        static bool CheckInterfaceDown();

        // 通过 ifconfig ap0 解析 flags 中是否包含 UP, 判断热点网卡是否已开启
        static bool CheckApInterfaceUp();

        // 实际执行 status 命令并解析结果(无缓存)
        Status FetchWifiStatus(WifiConnectionStatus &outStatus);

        // wifi未连接时主动开启热点, 保证wifi与热点不共存
        void EnsureHotspotWhenWifiDisconnected();

    private:
        struct WifiStatusCache {
            // Wi-Fi 状态缓存(避免频繁执行外部命令)
            WifiConnectionStatus mStatusCache;
            int64_t mStatusCacheMs {0};         // 缓存写入时间(ms)
            int64_t mStatusCacheTtlMs {60000};  // 当前缓存有效期(ms)
            bool mStatusCacheValid {false};     // 是否已有缓存数据(含失败结果)
            std::mutex mStatusCacheMutex;
        };

        static WifiStatusCache StatusCache;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_NETWORK_WIFI_MANAGER_H
