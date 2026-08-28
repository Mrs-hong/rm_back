//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_TIMER_MANAGER_H
#define QIFENG_CA_INCLUDE_COMMON_TIMER_MANAGER_H

#include <mutex>
#include <string>
#include <vector>

namespace qifeng_ca {

    // 全局定时器名称映射表(用于命名定时器 + 优雅退出时主动取消)
    namespace TimerName {
        // 设备信息采集定时器
        static constexpr std::string_view DeviceCollectTimer = "DeviceCollectTimer";
        // 实时音频读取定时器
        static constexpr std::string_view AudioReadTimer = "AudioReadTimer";
        // 显示屏定时刷新定时器(1s周期, 录音中跳过)
        static constexpr std::string_view DisplayRefreshTimer = "DisplayRefreshTimer";
        // 指纹录入结束后延时切回空闲页定时器(单次)
        static constexpr std::string_view FingerprintPageSwitchTimer = "FingerprintPageSwitchTimer";
        // 指纹录入2分钟总超时定时器(单次, 超时自动取消录入)
        static constexpr std::string_view FingerprintEnrollTimeoutTimer = "FingerprintEnrollTimeoutTimer";
        // 数据清理定时任务(1小时周期)
        static constexpr std::string_view CleanupTaskTimer = "CleanupTaskTimer";
        // OTA 升级包探测定时器(网络+USB统一探测, 跨来源选最新版本)
        static constexpr std::string_view OtaDetectTimer = "OtaDetectTimer";
    }  // namespace TimerName

    // 定时器管理器: 管理所有命名定时器, 支持优雅退出时批量取消
    class TimerManager {
    public:
        static TimerManager &GetInstance() {
            static TimerManager Instance;
            return Instance;
        }

        // 注册所有默认定时器名称(在创建定时器前调用)
        void DefaultRegisterAll();

        // 注册定时器名称(在创建定时器前调用)
        void Register(std::string_view timerName);

        // 取消所有已注册的命名定时器(优雅退出时调用)
        // 仿造application_template: 对每个定时器cancel_by_name并记录日志
        void CancelAll();

        // 取消指定名称的定时器
        void CancelByName(std::string_view timerName);

    private:
        TimerManager() = default;
        ~TimerManager() = default;
        TimerManager(const TimerManager &) = delete;
        TimerManager &operator=(const TimerManager &) = delete;
        TimerManager(TimerManager &&) = delete;
        TimerManager &operator=(TimerManager &&) = delete;

        void RegisterLocked(std::string_view timerName);

    private:
        std::mutex mTimerMutex;
        std::vector<std::string> mRegisteredTimers;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_TIMER_MANAGER_H
