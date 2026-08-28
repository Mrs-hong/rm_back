/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "qifeng_ca/upgrade.pb.h"

#include "common/status.h"
#include "core/upgrade/pkg_net_detector.h"
#include "core/upgrade/pkg_usb_detector.h"
#include "core/upgrade/upgrade_detector_base.h"

namespace qifeng_ca {

    /**
     * @brief OTA 实时升级管理类(编排核心)
     * @details 编排 OTA 升级全流程:
     *   1. 启动单一探测定时器(按开关决定查询网络/USB/两者)
     *   2. 启动时 SN 白名单校验(sophon_sn), 不合法则不启动 OTA 探测
     *   3. 探测到新版本 → 下载/拷贝到 OTA 临时目录 → SHA256 校验
     *   4. WS 通知页面(dataId=版本号, messageType=9), 等待用户确认(超时自动取消, 不推送)
     *   5. 用户确认且非实时会议 → 复用 UpgradeService::Upgrade 触发升级
     *   6. 全过程清理 OTA 临时目录残留(启动/失败/取消/超时)
     *  状态机: Idle → Downloading → PendingConfirm → Upgrading → Idle
     *
     *  定时器模式遵循 DeviceCollectTask 范式: WFTaskFactory 命名定时器 + go_task + series。
     *  网络与USB同时开启时, DoDetect 合并两来源候选并选最新版本(需求#5)。
     */
    class UpgradeRealtimeManager {
    public:
        static UpgradeRealtimeManager &GetInstance() {
            static UpgradeRealtimeManager Instance;
            return Instance;
        }

        ~UpgradeRealtimeManager() = default;

        UpgradeRealtimeManager(const UpgradeRealtimeManager &) = delete;
        UpgradeRealtimeManager &operator=(const UpgradeRealtimeManager &) = delete;
        UpgradeRealtimeManager(UpgradeRealtimeManager &&) = delete;
        UpgradeRealtimeManager &operator=(UpgradeRealtimeManager &&) = delete;

        // 启动 OTA 探测(清理残留 + SN 白名单校验 + 按开关创建探测器 + 启动定时器)
        void Start();

        // 停止 OTA 探测(优雅退出, 进行中的下载会在重新获锁时检测到并退出)
        void Stop();

        // 用户确认 OTA 升级(由 Controller 调用), 复用 UpgradeService::Upgrade
        Status ApplyPendingOta(uint64_t accountId, UpgradeResponse* resp);

        // 用户取消/拒绝 OTA 升级(由 Controller 调用)
        // PendingConfirm 状态取消: 保留已准备包供下次探测复用; Downloading 状态取消: 清理不完整文件
        Status CancelPendingOta();

        // 管理员 WS 连接建立回调(由 SystemMsgWsController 调用)
        // 若本次通知窗口未通知过(mNotifiedVersion 空)且已有已准备包, 通知该管理员并记录版本
        // 非管理员连接直接忽略, 调用方无需预判角色
        void OnAdminConnected(const std::string &clientId, uint64_t groupId);

        // WS 连接断开回调(由 SystemMsgWsController 调用)
        // 若当前已无管理员在线, 重置 mNotifiedVersion, 开启下一轮通知窗口(下次管理员登录可再通知)
        void OnAdminDisconnected();

    private:
        UpgradeRealtimeManager();

        // 定时器回调: 执行一次探测(在 go_task 线程执行)
        void DoDetect();

        // 调度下一次定时探测
        void ScheduleNext();

        // 探测器回调: 发现新候选时入口
        void OnCandidateFound(const PackageInfo &candidate);

        // SN 白名单校验(读取本机 SN 并校验是否在白名单), 无锁, 可在锁外调用
        // outSophonSn 非空时, 校验通过后回填 sophon_sn 供云端接口鉴权使用
        static bool CheckPreconditions(std::string *outSophonSn = nullptr);

        // 下载/拷贝并 SHA256 校验升级包, 成功填充 ready
        // detector 由调用方在持锁时捕获, 避免无锁访问成员
        static Status DownloadAndVerify(UpgradeDetectorBase *detector, const PackageInfo &candidate,
                                        PackageInfo &ready);

        // WS 推送升级通知, dataId 携带版本与来源
        static void NotifyUpgradeNotice(const std::string &dataId);

        // 清理 OTA 临时目录残留(调用方持锁)
        void CleanupPendingLocked();

        // 检查 OTA 临时目录中已准备的升级包(上次取消/超时后保留)
        // 扫描以版本号命名的标记文件, 读取包路径信息, 并校验包文件存在性
        // 返回: package_version 非空表示找到可复用的已准备包
        PackageInfo FindPreparedPackage();

        // 创建已准备包标记文件(以版本号为文件名, 内容为 package_path/sha256_path/source)
        // 下载校验成功后调用, 供下次探测复用跳过下载
        static void CreatePreparedMarker(const PackageInfo &pkg);

        enum class State { Idle, Downloading, PendingConfirm, Upgrading };

        std::mutex mMutex;
        State mState {State::Idle};
        PackageInfo mPendingPackage;  // 已下载待确认的升级包
        int64_t mPendingTimeMs {0};   // 进入 PendingConfirm 的时间戳(ms)

        // 当前"通知窗口"已通知的版本; 空=未通知(下次有管理员在线时允许通知)
        // 一次登录会话只通知一次: 通知后置为版本号, 静默替换新版本时不重置;
        // 直到所有管理员断开(OnAdminDisconnected)或升级触发/包清理时才清空
        std::string mNotifiedVersion;

        bool mStarted {false};          // Start/Stop 生命周期标记(受 mMutex 保护)
        std::atomic<bool> mIsRunning {false};  // 定时器运行标记(原子, 定时器回调中检查)
        int32_t mIntervalSec {3600};    // 探测间隔(秒)

        std::shared_ptr<PkgNetDetector> mNetDetector;
        std::shared_ptr<PkgUsbDetector> mUsbDetector;
    };

}  // namespace qifeng_ca
