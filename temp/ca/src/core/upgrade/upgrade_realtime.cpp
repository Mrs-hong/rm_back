//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <WFTask.h>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/atomic_write_file.h"
#include "common/common.h"
#include "common/config/upgrade_config.h"
#include "common/device/sn_check.h"
#include "common/timer_manager.h"
#include "common/ws/realtime_dispatch_manager.h"
#include "common/ws/system_message_notifier.h"
#include "core/upgrade/upgrade_realtime.h"
#include "core/upgrade/upgrade_service.h"
#include "internal/recording_manager.h"

namespace qifeng_ca {

    UpgradeRealtimeManager::UpgradeRealtimeManager() = default;

    // ===================== 定时器逻辑 (遵循 DeviceCollectTask 范式) =====================

    void UpgradeRealtimeManager::Start() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mStarted) {
            SLOG_WARN << "UpgradeRealtimeManager: already started";
            return;
        }

        auto &cfg = UpgradeConfig::GetInstance();
        // 清理上次中断的 OTA 残留
        CleanupPendingLocked();

        // OTA 总开关关闭则不启动探测
        if (!cfg.IsOtaEnable()) {
            SLOG_INFO << "UpgradeRealtimeManager: OTA disabled by config, skip start";
            mStarted = true;
            return;
        }

        // 前期开放 OTA/USB/Web 全部升级方式, 跳过 SN 白名单校验, 任意设备均可触发升级
        // OTA 查询时上传的 sn 固定为 "000000"(供网络探测器拼接 getNewVersion/download 接口的 sn 参数)
        // TODO: 后期接入正式 SN 校验后恢复 CheckPreconditions 流程
        const std::string sophonSn = "000000";

        std::string currentVersion = cfg.GetCurrentVersion();
        std::string savePath = cfg.GetOtaTmpDir();
        mIntervalSec = cfg.GetDetectIntervalSec();
        SLOG_INFO << "UpgradeRealtimeManager: start, currentVersion=" << currentVersion << ", savePath=" << savePath
                  << ", interval=" << mIntervalSec << "s, sophon_sn=" << sophonSn;

        // 按开关创建探测器
        if (cfg.IsNetEnable()) {
            mNetDetector = std::make_shared<PkgNetDetector>(currentVersion, savePath, cfg.GetCloudBaseUrl(), sophonSn);
            SLOG_INFO << "UpgradeRealtimeManager: net detector enabled";
        } else {
            mNetDetector.reset();
            SLOG_INFO << "UpgradeRealtimeManager: net detect disabled";
        }

        if (cfg.IsUsbEnable()) {
            mUsbDetector = std::make_shared<PkgUsbDetector>(currentVersion, savePath);
            SLOG_INFO << "UpgradeRealtimeManager: usb detector enabled";
        } else {
            mUsbDetector.reset();
            SLOG_INFO << "UpgradeRealtimeManager: usb detect disabled";
        }

        mStarted = true;
        mIsRunning.store(true);

        // 立即执行一次探测, 之后按间隔调度
        auto* firstTask =
            WFTaskFactory::create_go_task(std::string(WorkflowTakeName::OtaDetectTask), [this]() { this->DoDetect(); });
        auto* series = Workflow::create_series_work(firstTask, [this](const SeriesWork*) { this->ScheduleNext(); });
        series->start();
    }

    void UpgradeRealtimeManager::Stop() {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (!mStarted) {
                return;
            }
            mStarted = false;
        }
        // 原子标记关闭, 让定时器回调自行退出(不再调度下一次)
        mIsRunning.store(false);
        SLOG_INFO << "UpgradeRealtimeManager: stopped";
    }

    void UpgradeRealtimeManager::ScheduleNext() {
        if (!mIsRunning.load()) {
            return;
        }

        // 命名定时器(支持优雅退出 cancel_by_name)
        auto* timerTask = WFTaskFactory::create_timer_task(
            std::string(TimerName::OtaDetectTimer), static_cast<time_t>(mIntervalSec), 0, [this](WFTimerTask* task) {
                // timer 被中止(state=2=WFT_STATE_ABORTED): 服务关闭/CA升级杀进程时的正常行为, 不视为错误
                if (task->get_state() == WFT_STATE_ABORTED) {
                    SLOG_INFO << "UpgradeRealtimeManager: timer aborted (service shutting down)";
                    return;
                }
                // 其他非零状态: 真正的异常
                if (task->get_state()) {
                    SLOG_ERROR << "UpgradeRealtimeManager: timer callback state=" << task->get_state()
                               << ", error=" << task->get_error();
                    return;
                }
                if (!mIsRunning.load()) {
                    return;
                }
                // 在 go_task 中执行探测(避免阻塞定时器线程)
                auto* detectTask = WFTaskFactory::create_go_task(std::string(WorkflowTakeName::OtaDetectTask),
                                                                 [this]() { this->DoDetect(); });
                auto* series =
                    Workflow::create_series_work(detectTask, [this](const SeriesWork*) { this->ScheduleNext(); });
                series->start();
            });
        timerTask->start();
    }

    void UpgradeRealtimeManager::DoDetect() {
        if (!mIsRunning.load()) {
            return;
        }

        // 查询所有启用的探测器, 合并候选列表(不下载, 仅元数据)
        std::vector<PackageInfo> candidates;
        if (mNetDetector) {
            auto netPkgs = mNetDetector->FindUpgradePackage();
            candidates.insert(candidates.end(), netPkgs.begin(), netPkgs.end());
        }
        if (mUsbDetector) {
            auto usbPkgs = mUsbDetector->FindUpgradePackage();
            candidates.insert(candidates.end(), usbPkgs.begin(), usbPkgs.end());
        }

        if (candidates.empty()) {
            return;
        }

        // 跨来源选最新版本(需求#5: 网络和USB同时开启时使用版本最新的)
        std::string currentVersion = UpgradeConfig::GetInstance().GetCurrentVersion();
        const PackageInfo* latest = SelectLatestCandidate(candidates, currentVersion);
        if (latest == nullptr) {
            return;
        }

        SLOG_INFO << "UpgradeRealtimeManager: found newer package, version=" << latest->package_version
                  << ", source=" << latest->source;
        // 拷贝一份候选(候选在本次调用栈内), 交给状态机处理
        PackageInfo candidate = *latest;
        OnCandidateFound(candidate);
    }

    // ===================== 状态机 =====================

    bool UpgradeRealtimeManager::CheckPreconditions(std::string* outSophonSn) {
        // SN 白名单校验: 使用 sophon_sn(bm1684x 核心板 SN)
        SnCheck::SnInfo snInfo;
        Status snReadStatus = SnCheck::GetInstance().GetSelfSnInfo(snInfo);
        if (!snReadStatus.IsSuccess()) {
            SLOG_WARN << "UpgradeRealtimeManager: read SN failed, " << snReadStatus.ToString();
            return false;
        }
        Status snValidStatus = SnCheck::GetInstance().IsSnValid(snInfo.sophon_sn);
        if (!snValidStatus.IsSuccess()) {
            SLOG_WARN << "UpgradeRealtimeManager: SN not valid, " << snValidStatus.ToString()
                      << ", sophon_sn=" << snInfo.sophon_sn;
            return false;
        }
        // 校验通过后输出 sophon_sn 供调用方用于云端接口鉴权
        if (outSophonSn != nullptr) {
            *outSophonSn = snInfo.sophon_sn;
        }
        return true;
    }

    Status UpgradeRealtimeManager::DownloadAndVerify(UpgradeDetectorBase* detector, const PackageInfo &candidate,
                                                     PackageInfo &ready) {
        if (detector == nullptr) {
            return Status {-1, "对应探测器未启用"};
        }

        // 下载/拷贝到 OTA 临时目录
        Status prepStatus = detector->PrepareUpgradePackage(candidate, ready);
        if (!prepStatus.IsSuccess()) {
            SLOG_ERROR << "UpgradeRealtimeManager: prepare package failed, " << prepStatus.ToString();
            return prepStatus;
        }

        // SHA256 完整性校验
        Status verifyStatus = UpgradeDetectorBase::VerifyUpgradePackage(ready.package_path, ready.sha256_path);
        if (!verifyStatus.IsSuccess()) {
            SLOG_ERROR << "UpgradeRealtimeManager: verify package failed, " << verifyStatus.ToString();
            return verifyStatus;
        }

        SLOG_INFO << "UpgradeRealtimeManager: download+verify ok, version=" << ready.package_version
                  << ", source=" << ready.source;
        return {};
    }

    void UpgradeRealtimeManager::NotifyUpgradeNotice(const std::string &dataId) {
        SystemMessageNotifier::GetInstance().SendUpgradeNotice(dataId);
        SLOG_INFO << "UpgradeRealtimeManager: notify page, dataId=" << dataId;
    }

    void UpgradeRealtimeManager::CleanupPendingLocked() {
        std::string tmpDir = UpgradeConfig::GetInstance().GetOtaTmpDir();
        std::error_code ec;
        if (std::filesystem::exists(tmpDir, ec)) {
            std::filesystem::remove_all(tmpDir, ec);
            if (ec) {
                SLOG_WARN << "UpgradeRealtimeManager: cleanup remove_all failed, dir=" << tmpDir
                          << ", err=" << ec.message();
            }
        }
        std::filesystem::create_directories(tmpDir, ec);
        if (ec) {
            SLOG_WARN << "UpgradeRealtimeManager: recreate tmp dir failed, dir=" << tmpDir << ", err=" << ec.message();
        }
        // 注意: 不在此重置 mNotifiedVersion。通知窗口的生命周期与包清理解耦:
        //   - 静默替换场景(已准备包 < 候选版本)会调用本函数清理旧包, 但不应重置通知态,
        //     否则下载新包后会重新通知, 违反"一次会话只通知一次"
        //   - mNotifiedVersion 仅在 OnAdminDisconnected(所有管理员断开) /
        //     ApplyPendingOta(升级触发) 时重置
    }

    // 判断字符串是否为版本号格式(纯数字+点, 如 "1.0.7")
    static bool IsVersionString(const std::string &s) {
        if (s.empty()) {
            return false;
        }
        for (char c : s) {
            if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') {
                return false;
            }
        }
        return true;
    }

    PackageInfo UpgradeRealtimeManager::FindPreparedPackage() {
        PackageInfo result;
        std::string dir = UpgradeConfig::GetInstance().GetOtaTmpDir();
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) {
            return result;
        }
        // 扫描目录, 查找以版本号命名的标记文件
        for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec || !entry.is_regular_file(ec)) {
                continue;
            }
            std::string name = entry.path().filename().string();
            if (!IsVersionString(name)) {
                continue;  // 跳过非版本号文件(如 .tar.gz 包文件、.sha256 文件)
            }
            // 读取标记文件内容: package_path\nsha256_path\nsource
            std::string content;
            if (!AtomicFileWriter::ReadFile(entry.path().string(), content).IsSuccess()) {
                continue;
            }
            std::istringstream iss(content);
            std::getline(iss, result.package_path);
            std::getline(iss, result.sha256_path);
            std::getline(iss, result.source);
            // 校验包文件和 sha256 文件是否仍存在(防止外部删除导致复用失败)
            if (result.package_path.empty() || !std::filesystem::exists(result.package_path, ec) ||
                result.sha256_path.empty() || !std::filesystem::exists(result.sha256_path, ec)) {
                SLOG_WARN << "UpgradeRealtimeManager: prepared marker found but package files missing, version="
                          << name;
                result = {};
                continue;
            }
            result.package_version = name;
            SLOG_INFO << "UpgradeRealtimeManager: found prepared package, version=" << name;
            break;
        }
        return result;
    }

    void UpgradeRealtimeManager::CreatePreparedMarker(const PackageInfo &pkg) {
        std::string dir = UpgradeConfig::GetInstance().GetOtaTmpDir();
        std::string markerPath = dir + "/" + pkg.package_version;
        // 标记文件内容: package_path\nsha256_path\nsource
        std::string content = pkg.package_path + "\n" + pkg.sha256_path + "\n" + pkg.source;
        if (!AtomicFileWriter::WriteAtomic(markerPath, content, 0644)) {
            SLOG_WARN << "UpgradeRealtimeManager: create prepared marker failed, path=" << markerPath;
        } else {
            SLOG_INFO << "UpgradeRealtimeManager: prepared marker created, version=" << pkg.package_version;
        }
    }

    void UpgradeRealtimeManager::OnCandidateFound(const PackageInfo &candidate) {
        // 持锁: 检查状态并捕获探测器指针
        UpgradeDetectorBase* detector = nullptr;
        bool skipDownload = false;  // true=复用已准备包, 跳过下载
        bool shouldNotify = false;  // true=本次需通知页面(锁外发送), 受通知窗口(mNotifiedVersion)控制
        PackageInfo preparedPkg;    // 复用的已准备包(skipDownload=true 时有效)
        {
            std::unique_lock<std::mutex> lock(mMutex);

            // 超时自动取消: 若处于 PendingConfirm 且超时, 回到 Idle
            // 注意: 不清理已准备的包文件, 保留供下次探测复用(避免重复下载)
            if (mState == State::PendingConfirm) {
                int64_t elapsed = static_cast<int64_t>(GetTimeMs()) - mPendingTimeMs;
                int64_t timeoutMs = static_cast<int64_t>(UpgradeConfig::GetInstance().GetConfirmTimeoutSec()) * 1000;
                if (elapsed > timeoutMs) {
                    SLOG_INFO << "UpgradeRealtimeManager: pending confirm timeout(" << elapsed << "ms), cancel";
                    mState = State::Idle;
                    mPendingPackage = {};
                    // 超时自动取消, 不推送 WS 通知, 不清理已准备包
                }
            }

            // 仅 Idle 时接受新候选, 避免下载/确认/升级期间重复处理
            if (mState != State::Idle) {
                return;
            }
            if (!mStarted) {
                return;
            }

            // 根据来源选择探测器(持锁捕获, 避免无锁访问成员)
            if (candidate.source == "net") {
                detector = mNetDetector.get();
            } else if (candidate.source == "usb") {
                detector = mUsbDetector.get();
            } else {
                SLOG_WARN << "UpgradeRealtimeManager: unknown source: " << candidate.source;
                return;
            }

            // 检查是否已有准备好的包(上次取消/超时后保留)
            PackageInfo prepared = FindPreparedPackage();
            if (!prepared.package_version.empty()) {
                int cmp = CompareVersion(prepared.package_version, candidate.package_version);
                if (cmp >= 0) {
                    // 已准备包版本 >= 候选版本: 复用, 跳过下载
                    SLOG_INFO << "UpgradeRealtimeManager: reuse prepared package, prepared=" << prepared.package_version
                              << ", candidate=" << candidate.package_version;
                    preparedPkg = prepared;
                    mPendingPackage = prepared;
                    mState = State::PendingConfirm;
                    mPendingTimeMs = static_cast<int64_t>(GetTimeMs());
                    skipDownload = true;
                    // 通知窗口未通知过 → 标记待通知(锁外发送); 已通知过则静默(一次会话只通知一次)
                    if (mNotifiedVersion.empty()) {
                        mNotifiedVersion = prepared.package_version;
                        shouldNotify = true;
                    }
                } else {
                    // 已准备包版本 < 候选版本: 清理旧包, 下载新包
                    SLOG_INFO << "UpgradeRealtimeManager: prepared package outdated(" << prepared.package_version
                              << " < " << candidate.package_version << "), cleanup and re-download";
                    CleanupPendingLocked();
                    mState = State::Downloading;
                }
            } else {
                // 无已准备包: 直接下载
                mState = State::Downloading;
            }
        }

        // 复用已准备包: 按通知窗口决定是否通知(一次会话只通知一次), 跳过下载
        if (skipDownload) {
            if (shouldNotify) {
                NotifyUpgradeNotice(preparedPkg.package_version);
            }
            return;
        }

        // 释放锁执行耗时下载(使用捕获的探测器指针, 不访问成员)
        PackageInfo ready;
        Status status = DownloadAndVerify(detector, candidate, ready);

        // 重新获锁: 验证状态未被取消/停止
        {
            std::unique_lock<std::mutex> lock(mMutex);

            // 若下载期间被取消或停止, 清理已下载文件并返回
            if (mState != State::Downloading || !mStarted) {
                SLOG_INFO << "UpgradeRealtimeManager: state changed during download, discard result";
                CleanupPendingLocked();
                mState = State::Idle;
                mPendingPackage = {};
                return;
            }

            if (!status.IsSuccess()) {
                // 下载/校验失败: 清理残留并回到 Idle
                CleanupPendingLocked();
                mState = State::Idle;
                SLOG_WARN << "UpgradeRealtimeManager: download+verify failed, back to idle, " << status.ToString();
                return;
            }

            // 下载校验成功: 创建标记文件(供下次探测复用), 进入待确认状态
            CreatePreparedMarker(ready);
            mPendingPackage = ready;
            mState = State::PendingConfirm;
            mPendingTimeMs = static_cast<int64_t>(GetTimeMs());
            // 通知窗口未通知过 + 当前有管理员在线 → 标记待通知(锁外发送)
            // 无管理员在线则延迟, 等管理员登录时由 OnAdminConnected 通知
            if (mNotifiedVersion.empty()) {
                size_t adminCount = RealtimeDispatchManager::GetInstance().CountByGroup(EClientType::kMessage,
                                                                                        Authority::ADMINISTRATOR);
                if (adminCount > 0) {
                    mNotifiedVersion = ready.package_version;
                    shouldNotify = true;
                    SLOG_INFO << "UpgradeRealtimeManager: notify on new package, version=" << ready.package_version;
                } else {
                    SLOG_INFO << "UpgradeRealtimeManager: new package ready but no admin online, defer notify, version="
                              << ready.package_version;
                }
            } else {
                SLOG_INFO << "UpgradeRealtimeManager: already notified this session, skip notify, version="
                          << ready.package_version;
            }
        }

        // 锁外通知页面(避免锁内执行WS发送)
        // dataId 仅存放版本号, 客户端收到 messageType=9 即知有升级包待确认
        if (shouldNotify) {
            NotifyUpgradeNotice(ready.package_version);
        }
    }

    Status UpgradeRealtimeManager::ApplyPendingOta(uint64_t accountId, UpgradeResponse* resp) {
        std::unique_lock<std::mutex> lock(mMutex);

        if (mState != State::PendingConfirm) {
            SLOG_WARN << "UpgradeRealtimeManager: apply ota but no pending package, state=" << static_cast<int>(mState);
            return Status {-1, "无待确认的OTA升级"};
        }

        // 实时会议检查: 会议中拒绝升级
        if (RecordingManager::GetInstance().IsRecording()) {
            SLOG_WARN << "UpgradeRealtimeManager: recording in progress, reject ota";
            return Status {-1, "会议进行中, 请先停止会议"};
        }

        PackageInfo pkg = mPendingPackage;
        mState = State::Upgrading;
        lock.unlock();

        SLOG_INFO << "UpgradeRealtimeManager: apply ota, account=" << accountId << ", version=" << pkg.package_version;

        // 两阶段升级: PrepareUpgrade(验证+部署+识别) 同步返回, ExecuteUpgrade 异步执行
        // 版本号更新由 ExecuteUpgrade 内部按组件类型处理:
        //   - 仅屏幕升级: 进程不被杀, 直接更新 data/.version
        //   - CA/SCM 升级: 写入 pending_version, 下次启动 CheckUpgradeResultAndSetComplete 检测成功后更新
        UpgradeRequest req;
        req.set_account_id(accountId);
        req.set_package_path(pkg.package_path);
        req.set_sha256_path(pkg.sha256_path);

        UpgradeService service;
        Status upgradeStatus = service.PrepareUpgrade(req, resp);

        // PrepareUpgrade 已同步返回(验证+部署完成), 升级执行在后台异步进行
        // 清理 OTA 状态机(若进程仍存活则执行, CA 升级进程会被杀)
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mState = State::Idle;
            mPendingPackage = {};
            // 升级已触发, 重置通知窗口, 下次有新包时允许通知
            mNotifiedVersion.clear();
            // 注意: OTA 临时目录由 PrepareUpgrade 部署后清理(remove_all tmpUpgradeDir)
        }

        if (!upgradeStatus.IsSuccess()) {
            SLOG_ERROR << "UpgradeRealtimeManager: prepare upgrade failed, " << upgradeStatus.ToString();
        } else {
            SLOG_INFO << "UpgradeRealtimeManager: prepare ok, execute upgrade running async";
        }
        return upgradeStatus;
    }

    Status UpgradeRealtimeManager::CancelPendingOta() {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mState != State::PendingConfirm && mState != State::Downloading) {
                SLOG_DEBUG << "UpgradeRealtimeManager: cancel but no pending/download, state="
                           << static_cast<int>(mState);
                return {};
            }
            SLOG_INFO << "UpgradeRealtimeManager: cancel pending ota, state=" << static_cast<int>(mState);
            // 不清理已准备的包文件: 保留供下次探测复用(版本一致时跳过下载)
            // 仅在 Downloading 状态取消时清理(下载未完成, 部分文件无意义)
            if (mState == State::Downloading) {
                CleanupPendingLocked();
            }
            mState = State::Idle;
            mPendingPackage = {};
        }

        // 不推送 WS 取消通知: 调用方通过 API 响应已知取消结果, 客户端自行处理 UI
        return {};
    }

    void UpgradeRealtimeManager::OnAdminConnected(const std::string &clientId, uint64_t groupId) {
        // 非管理员直接忽略(普通用户/访客无法触发升级, 不需通知)
        if (groupId != Authority::ADMINISTRATOR) {
            return;
        }
        std::string notifyVersion;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (!mStarted) {
                return;
            }
            // 通知窗口已通知过 → 不再通知(系统级一次)
            if (!mNotifiedVersion.empty()) {
                return;
            }
            // 有已准备包且本次未通知过 → 记录版本, 锁外发送
            // FindPreparedPackage 持锁调用(与 OnCandidateFound 复用判断一致)
            PackageInfo prepared = FindPreparedPackage();
            if (prepared.package_version.empty()) {
                return;  // 无已准备包, 等下次探测后再由 OnCandidateFound 触发
            }
            mNotifiedVersion = prepared.package_version;
            notifyVersion = prepared.package_version;
            SLOG_INFO << "OnAdminConnected: notify admin on login, client=" << clientId
                      << ", version=" << prepared.package_version;
        }
        // 锁外发送, 避免持锁执行 WS send
        NotifyUpgradeNotice(notifyVersion);
    }

    void UpgradeRealtimeManager::OnAdminDisconnected() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mStarted) {
            return;
        }
        // 当前已无管理员在线 → 重置通知窗口, 开启下一轮(下次管理员登录可再通知)
        // CountByGroup 获取 RealtimeDispatchManager 共享锁, 不反向获取 mMutex, 无死锁风险
        size_t adminCount =
            RealtimeDispatchManager::GetInstance().CountByGroup(EClientType::kMessage, Authority::ADMINISTRATOR);
        if (adminCount == 0 && !mNotifiedVersion.empty()) {
            SLOG_INFO << "OnAdminDisconnected: no admin online, reset notified version";
            mNotifiedVersion.clear();
        }
    }

}  // namespace qifeng_ca
