/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "hal/screen/screen_page.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "common/logger.h"
#include "hal/screen/protocol/addr_map.h"

namespace qifeng::screen {

    namespace {
        // 任务队列总容量（计时 + 触控之和）：触控低频，正常远达不到上限
        constexpr size_t kMaxQueueSize = 32;
    }  // namespace

    // ── TaskExecutor ──

    TaskExecutor::~TaskExecutor() {
        Stop();
    }

    void TaskExecutor::Start() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mRunning) {
            return;
        }
        mRunning = true;
        mThread = std::thread([this]() { Run(); });
    }

    void TaskExecutor::Stop() {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (!mRunning) {
                return;
            }
            mRunning = false;
            // 丢弃剩余任务：回调可能引用已析构的业务对象，不可继续执行
            mTimerQueue.clear();
            mTouchQueue.clear();
            mPendingKeys.clear();
        }
        mCv.notify_all();
        if (mThread.joinable()) {
            mThread.join();
        }
    }

    bool TaskExecutor::SubmitTimer(Task task) {
        if (!task) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mRunning) {
            return false;
        }
        if (mTimerQueue.size() + mTouchQueue.size() >= kMaxQueueSize) {
            SLOG_WARN << "TaskExecutor: queue full (" << kMaxQueueSize << "), timer task dropped";
            return false;
        }
        mTimerQueue.push_back(TaskItem {0, std::move(task)});
        mCv.notify_all();
        return true;
    }

    bool TaskExecutor::SubmitTouch(uint16_t touchKey, Task task) {
        if (!task) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mRunning) {
            return false;
        }
        // 同按钮去重：队列中已有该按钮未执行任务时抛弃新提交（保留第一个）
        if (mPendingKeys.count(touchKey) > 0) {
            return true;  // 视为已受理（语义：首个任务将执行，重复点击被吸收）
        }
        if (mTimerQueue.size() + mTouchQueue.size() >= kMaxQueueSize) {
            SLOG_WARN << "TaskExecutor: queue full (" << kMaxQueueSize << "), touch task dropped (key=0x" << std::hex
                      << touchKey << ")";
            return false;
        }
        mPendingKeys.insert(touchKey);
        mTouchQueue.push_back(TaskItem {touchKey, std::move(task)});
        mCv.notify_all();
        return true;
    }

    void TaskExecutor::Run() {
        std::unique_lock<std::mutex> lock(mMutex);
        while (mRunning) {
            // 优先消费计时任务（弹窗恢复/页面回退，短平快），空则消费触控任务
            TaskItem item;
            if (!mTimerQueue.empty()) {
                item = std::move(mTimerQueue.front());
                mTimerQueue.pop_front();
            } else if (!mTouchQueue.empty()) {
                item = std::move(mTouchQueue.front());
                mTouchQueue.pop_front();
                mPendingKeys.erase(item.touchKey);  // 出队即解除去重锁，后续同键可再入队
            } else {
                mCv.wait(lock);
                continue;
            }
            lock.unlock();
            item.task();  // 锁外执行（不可被抢占，同类任务串行）
            lock.lock();
        }
    }

    // ── TimerController ──

    TimerController::~TimerController() {
        Stop();
    }

    void TimerController::Start() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mRunning) {
            return;
        }
        mRunning = true;
        // 定时线程 detach 不需要：由 Stop 精确 join
        mThread = std::thread([this]() { Run(); });
    }

    void TimerController::Stop() {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (!mRunning) {
                return;
            }
            mRunning = false;
            mItems.clear();
        }
        mCv.notify_all();
        if (mThread.joinable()) {
            mThread.join();
        }
    }

    uint64_t TimerController::Schedule(uint32_t delayMs, Callback callback) {
        if (!callback) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mRunning) {
            return 0;
        }
        auto id = mNextId++;
        mItems.push_back(
            {id, std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs), std::move(callback)});
        mCv.notify_all();
        return id;
    }

    void TimerController::SetDispatcher(Dispatcher dispatcher) {
        std::lock_guard<std::mutex> lock(mMutex);
        mDispatcher = std::move(dispatcher);
    }

    void TimerController::Cancel(uint64_t timerId) {
        if (timerId == 0) {
            return;
        }
        std::lock_guard<std::mutex> lock(mMutex);
        std::erase_if(mItems, [timerId](const TimerItem& item) { return item.id == timerId; });
    }

    void TimerController::Run() {
        std::unique_lock<std::mutex> lock(mMutex);
        while (mRunning) {
            if (mItems.empty()) {
                mCv.wait(lock);
                continue;
            }
            // 取最早到期项
            auto next = std::min_element(mItems.begin(), mItems.end(), [](const TimerItem& a, const TimerItem& b) {
                return a.deadline < b.deadline;
            });
            if (std::chrono::steady_clock::now() >= next->deadline) {
                auto callback = std::move(next->callback);
                mItems.erase(next);
                // 到期回调经派发函数上抛（Manager 注入：投执行器高优先级队列），
                // 避免长回调阻塞本线程对后续到期项的检测；未注入则直接执行。
                // 派发函数在锁内拷贝（约定 Start 后不再变更）
                auto dispatcher = mDispatcher;
                lock.unlock();
                if (dispatcher) {
                    dispatcher(callback);
                } else {
                    callback();
                }
                lock.lock();
            } else {
                mCv.wait_until(lock, next->deadline);
            }
        }
    }

    // ── Page ──

    void Page::AddWidget(std::unique_ptr<Widget> widget) {
        if (!widget) {
            return;
        }
        widget->mOwnerPage = mId;  // 注入控件对该页的归属关系
        mWidgets[widget->Type()] = std::move(widget);
    }

    Widget* Page::FindWidget(WidgetType type) const {
        auto it = mWidgets.find(type);
        return (it == mWidgets.end()) ? nullptr : it->second.get();
    }

    void Page::AddTouchKey(ScreenButton button) {
        if (!HasTouchKey(button)) {
            mTouchKeys.push_back(button);
        }
    }

    bool Page::HasTouchKey(ScreenButton button) const {
        return std::find(mTouchKeys.begin(), mTouchKeys.end(), button) != mTouchKeys.end();
    }

    // ── ScreenPageManager：生命周期 ──

    ScreenPageManager::~ScreenPageManager() {
        Release();
    }

    ScreenResult ScreenPageManager::Init(const DisplayConfig& config) {
        auto ret = mEngine.Init(config);
        if (ret != ScreenResult::OK) {
            return ret;
        }
        BuildRegistry();
        // 执行器先启动（定时器派发与触控投递的目标）
        mExecutor.Start();
        // 定时器到期回调投执行器高优先级队列（弹窗恢复/页面回退优先消费）
        mTimers.SetDispatcher([this](TimerController::Callback cb) { mExecutor.SubmitTimer(std::move(cb)); });
        mTimers.Start();
        // 触控上抛：引擎只负责帧解析，页面语义（归属校验/事件分发）在本层完成
        mEngine.SetTouchCallback([this](const TouchEvent& event) { OnTouch(event); });
        SLOG_INFO << "ScreenPageManager initialized";
        return ScreenResult::OK;
    }

    void ScreenPageManager::Release() {
        // 停止顺序：定时器（不再产生新派发）→ 执行器（join 等待当前任务完成，
        // 此时引擎仍可用）→ 引擎（此时无线程会再调用它）
        mTimers.Stop();
        mExecutor.Stop();
        mEngine.Release();

        std::lock_guard<std::mutex> touchLock(mTouchMutex);
        mTouchHandlers.clear();
        mTouchHandles.clear();

        std::lock_guard<std::mutex> opLock(mOpMutex);
        mActivePops.clear();
        mWidgetIndex.clear();
        mWidgetButtons.clear();
        mPages.clear();
        mCurrentPage.store(DisplayPage::Idle, std::memory_order_release);
        mUpgradeActive.store(false, std::memory_order_release);
    }

    bool ScreenPageManager::IsUpgrading() const {
        // Manager 侧标记在 mOpMutex 内置位（与各操作的检查串行化），
        // 引擎侧标记覆盖绕过 Manager 直接调用引擎的场景
        return mUpgradeActive.load(std::memory_order_acquire) || mEngine.IsUpgrading();
    }

    /**
     * 页面注册表：页面-控件-键值的权威归属。
     * 归属关系与产品协议合同（屏幕工程 14.BIN 静态定义）一致，
     * 新增页面/控件/按钮时在此处注册。
     */
    void ScreenPageManager::BuildRegistry() {
        // 待机页（0x0001）：可用时长/纪要/任务卡片、顶部设备信息、提示卡片
        auto idle = std::make_unique<Page>(DisplayPage::Idle);
        idle->AddWidget(Widget::Create(WidgetType::IdleAvailability));
        idle->AddWidget(Widget::Create(WidgetType::IdleSummary));
        idle->AddWidget(Widget::Create(WidgetType::IdleTask));
        idle->AddWidget(Widget::Create(WidgetType::IdleStatus));
        idle->AddWidget(Widget::Create(WidgetType::DeviceInfo));
        idle->AddTouchKey(ScreenButton::PublicMeeting);   // 【公共会议】→ 会议录制页
        idle->AddTouchKey(ScreenButton::PrivateMeeting);  // 【私密会议】→ 指纹鉴权页
        idle->AddTouchKey(ScreenButton::MeetingManage);   // 【会议管理】→ 数据类型选择页
        mPages[DisplayPage::Idle] = std::move(idle);

        // 会议中页（0x0002）：基本信息、会议信息、录制时长、能量波形
        auto meeting = std::make_unique<Page>(DisplayPage::Meeting);
        meeting->AddWidget(Widget::Create(WidgetType::MeetingBasicInfo));
        meeting->AddWidget(Widget::Create(WidgetType::MeetingInfo));
        meeting->AddWidget(Widget::Create(WidgetType::MeetingClock));
        meeting->AddWidget(Widget::Create(WidgetType::MeetingEnergy));
        meeting->AddTouchKey(ScreenButton::TogglePauseResume);  // 【暂停/继续】
        meeting->AddTouchKey(ScreenButton::EndMeeting);         // 【结束会议】→ 二次确认页
        mPages[DisplayPage::Meeting] = std::move(meeting);

        // 指纹录入页（0x0003，沿用）：录入进度、提示文本、录入结果（按钮走无归属注册）
        auto fingerprint = std::make_unique<Page>(DisplayPage::Fingerprint);
        fingerprint->AddWidget(Widget::Create(WidgetType::Fingerprint));
        mPages[DisplayPage::Fingerprint] = std::move(fingerprint);

        // 升级页（0x0004，沿用）：升级标题/类型、loading 动画
        auto upgrade = std::make_unique<Page>(DisplayPage::Upgrade);
        upgrade->AddWidget(Widget::Create(WidgetType::Upgrade));
        mPages[DisplayPage::Upgrade] = std::move(upgrade);

        // 指纹鉴权页（0x0005）：指纹提示（鉴权失败弹窗）
        auto fpAuth = std::make_unique<Page>(DisplayPage::FingerprintAuth);
        fpAuth->AddWidget(Widget::Create(WidgetType::FingerprintAuth));
        fpAuth->AddTouchKey(ScreenButton::FingerprintAuthCancel);  // 【取消】→ 待机页
        mPages[DisplayPage::FingerprintAuth] = std::move(fpAuth);

        // 结束会议二次确认页（0x0006）：纯触控页，无显示变量
        auto endConfirm = std::make_unique<Page>(DisplayPage::MeetingEndConfirm);
        endConfirm->AddTouchKey(ScreenButton::EndMeetingConfirm);  // 【确认】→ 待机页
        endConfirm->AddTouchKey(ScreenButton::EndMeetingCancel);   // 【取消】→ 会议中页
        mPages[DisplayPage::MeetingEndConfirm] = std::move(endConfirm);

        // 会议数据类型选择页（0x0009）：纯触控页，无显示变量
        auto typeSelect = std::make_unique<Page>(DisplayPage::MeetingTypeSelect);
        typeSelect->AddTouchKey(ScreenButton::SelectVisitorData);  // 【访客会议数据】→ 会议管理页
        typeSelect->AddTouchKey(ScreenButton::SelectPrivateData);  // 【私密会议数据】→ 指纹鉴权页
        mPages[DisplayPage::MeetingTypeSelect] = std::move(typeSelect);

        // 会议管理页（0x000B）：列表主体（卡片/页码/筛选，联动按钮内部接管）+ 页头信息
        auto mgmt = std::make_unique<Page>(DisplayPage::MeetingMgmt);
        mgmt->AddWidget(Widget::Create(WidgetType::MeetingMgmtList));
        mgmt->AddWidget(Widget::Create(WidgetType::MeetingMgmtInfo));
        mgmt->AddTouchKey(ScreenButton::MgmtFilterSummary);  // 【总结完成会议】筛选
        mgmt->AddTouchKey(ScreenButton::MgmtFilterOther);    // 【其他会议】筛选
        mgmt->AddTouchKey(ScreenButton::MgmtReturnHome);     // 【返回】→ 待机页
        mPages[DisplayPage::MeetingMgmt] = std::move(mgmt);

        // 下载二次确认页（其他会议 0x000D / 总结完成会议 0x000F）：共用同一组键值
        const std::array<DisplayPage, 2> downloadConfirmPages {DisplayPage::DownloadConfirmOther,
                                                               DisplayPage::DownloadConfirmSummary};
        for (DisplayPage page : downloadConfirmPages) {
            auto confirm = std::make_unique<Page>(page);
            confirm->AddTouchKey(ScreenButton::ConfirmCancel);       // 【取消】→ 会议管理页
            confirm->AddTouchKey(ScreenButton::DownloadAudioOnly);   // 【仅音频】
            confirm->AddTouchKey(ScreenButton::DownloadSummary);     // 【纪要】
            confirm->AddTouchKey(ScreenButton::DownloadAudio);       // 【音频】
            confirm->AddTouchKey(ScreenButton::DownloadFull);        // 【纪要+音频+转写原文】
            mPages[page] = std::move(confirm);
        }

        // 删除二次确认页（0x0011）
        auto delConfirm = std::make_unique<Page>(DisplayPage::DeleteConfirm);
        delConfirm->AddTouchKey(ScreenButton::ConfirmCancel);    // 【取消】→ 会议管理页
        delConfirm->AddTouchKey(ScreenButton::DeleteConfirmOk);  // 【确认删除】→ 删除 Loading 页
        mPages[DisplayPage::DeleteConfirm] = std::move(delConfirm);

        // 删除 Loading 页（0x0013）
        auto delLoading = std::make_unique<Page>(DisplayPage::DeleteLoading);
        delLoading->AddWidget(Widget::Create(WidgetType::DeleteLoading));
        mPages[DisplayPage::DeleteLoading] = std::move(delLoading);

        // 下载 Loading 页（0x0014）
        auto dlLoading = std::make_unique<Page>(DisplayPage::DownloadLoading);
        dlLoading->AddWidget(Widget::Create(WidgetType::DownloadLoading));
        mPages[DisplayPage::DownloadLoading] = std::move(dlLoading);

        // 下载结果页（0x0015）
        auto dlResult = std::make_unique<Page>(DisplayPage::DownloadResult);
        dlResult->AddWidget(Widget::Create(WidgetType::DownloadResult));
        mPages[DisplayPage::DownloadResult] = std::move(dlResult);

        // 删除结果页（0x0016）
        auto delResult = std::make_unique<Page>(DisplayPage::DeleteResult);
        delResult->AddWidget(Widget::Create(WidgetType::DeleteResult));
        mPages[DisplayPage::DeleteResult] = std::move(delResult);

        // 跨页控件索引（运行期只读）
        for (auto& [page, pageObj] : mPages) {
            for (auto& [type, widget] : pageObj->Widgets()) {
                mWidgetIndex[type] = widget.get();
            }
        }

        // 组件联动按钮路由：各控件声明式上报（OwnedButtons），Manager 只做路由，
        // 新组件带联动按钮时本函数与 OnTouch 均零改动
        for (auto& [type, widget] : mWidgetIndex) {
            for (ScreenButton button : widget->OwnedButtons()) {
                mWidgetButtons[static_cast<uint16_t>(button)] = widget;
            }
        }
    }

    Page* ScreenPageManager::FindPage(DisplayPage page) const {
        auto it = mPages.find(page);
        return (it == mPages.end()) ? nullptr : it->second.get();
    }

    Widget* ScreenPageManager::FindWidget(WidgetType type) const {
        auto it = mWidgetIndex.find(type);
        return (it == mWidgetIndex.end()) ? nullptr : it->second;
    }

    // ── 页面：显示与重置 ──

    ScreenResult ScreenPageManager::ShowPage(DisplayPage page, const std::optional<std::vector<WidgetData>>& data,
                                             uint32_t timeoutMs) {
        std::lock_guard<std::mutex> lock(mOpMutex);
        if (!mEngine.IsInitialized()) {
            return ScreenResult::NotInitialized;
        }
        if (IsUpgrading()) {
            return ScreenResult::UpgradeInProgress;
        }
        return ApplyPage(page, data, true, timeoutMs);
    }

    ScreenResult ScreenPageManager::ResetPage(DisplayPage page, uint32_t timeoutMs) {
        std::lock_guard<std::mutex> lock(mOpMutex);
        if (!mEngine.IsInitialized()) {
            return ScreenResult::NotInitialized;
        }
        if (IsUpgrading()) {
            return ScreenResult::UpgradeInProgress;
        }
        // 重置语义：只清数据不切页（页面通常已在显示中）
        return ApplyPage(page, std::nullopt, false, timeoutMs);
    }

    ScreenResult ScreenPageManager::ApplyPage(DisplayPage page, const std::optional<std::vector<WidgetData>>& data,
                                              bool switchFrame, uint32_t timeoutMs) {
        auto* pageObj = FindPage(page);
        if (pageObj == nullptr) {
            SLOG_ERROR << "Unknown page: 0x" << std::hex << static_cast<uint16_t>(page);
            return ScreenResult::InvalidParameter;
        }

        // 控件数据 = 指定数据（须归属该页）+ 未指定的默认重置数据
        std::map<WidgetType, const WidgetData*> effective;
        if (data.has_value()) {
            for (const auto& d : *data) {
                auto type = WidgetTypeOf(d);
                if (pageObj->FindWidget(type) == nullptr) {
                    SLOG_ERROR << "Widget not owned by page: type=" << static_cast<int>(type);
                    return ScreenResult::InvalidParameter;
                }
                effective[type] = &d;
            }
        }
        std::vector<WidgetData> defaults;
        defaults.reserve(pageObj->Widgets().size());  // 防指针失效
        for (const auto& [type, widget] : pageObj->Widgets()) {
            if (effective.find(type) == effective.end()) {
                defaults.push_back(widget->DefaultData());
                effective[type] = &defaults.back();
            }
        }

        // 帧序列：切页帧在前、控件帧在后（串口有序，屏先换页再收数据，
        // 新页控件按最新 VP 值渲染，无残留闪现）
        std::vector<Request> requests;
        if (switchFrame) {
            Request request;
            request.address = Addr::PageSwitch;
            // 迪文页面切换需要先发 0x5A01 前缀再发页面编号
            request.value = std::vector<uint16_t> {0x5A01, static_cast<uint16_t>(page)};
            requests.push_back(std::move(request));
        }
        std::vector<std::pair<Widget*, const WidgetData*>> applied;
        for (const auto& [type, widget] : pageObj->Widgets()) {
            const auto* d = effective[type];
            std::vector<Request> diff;
            if (!widget->BuildDiffRequests(*d, diff)) {
                return ScreenResult::InvalidParameter;
            }
            requests.insert(requests.end(), diff.begin(), diff.end());
            applied.emplace_back(widget.get(), d);
        }

        auto result = mEngine.SendRequests(requests, timeoutMs);
        if (result == ScreenResult::OK) {
            // 整页应用使活动弹窗的恢复语义失效（页面已被默认数据覆盖），取消之，
            // 避免恢复回调稍后把弹窗快照写回、覆盖整页重置结果
            for (auto& [type, pop] : mActivePops) {
                mTimers.Cancel(pop.timerId);
            }
            mActivePops.clear();
            for (auto& [widget, d] : applied) {
                widget->Commit(*d);
            }
            // 仅真正发送切页帧时才更新主机侧当前页：ResetPage 只重置数据不改屏端页面，
            // 若一并更新会让主机侧页面记录与屏端实际页不一致
            if (switchFrame) {
                mCurrentPage.store(page, std::memory_order_release);
            }
        } else {
            // 切页帧可能已生效而控件帧未确认，屏内实际状态不可信：
            // 全量失效缓存，下次调用强制全量重发（切页帧也会重发，幂等安全）
            InvalidateAllWidgets();
        }
        return result;
    }

    // ── 控件：更新与弹窗 ──

    ScreenResult ScreenPageManager::UpdateWidget(const WidgetData& data, uint32_t timeoutMs) {
        std::lock_guard<std::mutex> lock(mOpMutex);
        if (!mEngine.IsInitialized()) {
            return ScreenResult::NotInitialized;
        }
        if (IsUpgrading()) {
            return ScreenResult::UpgradeInProgress;
        }
        auto* widget = FindWidget(WidgetTypeOf(data));
        if (widget == nullptr) {
            return ScreenResult::InvalidParameter;
        }
        // 归属校验（软）：非当前页仅告警不拒绝——DGUS 变量空间全局，写入仍生效，
        // 但屏上不可见且切页时会被整页默认重置覆盖
        if (widget->OwnerPage() != mCurrentPage.load(std::memory_order_acquire)) {
            SLOG_WARN << "UpdateWidget: widget page != current page, display may not take effect";
        }
        // 业务更新使该控件的活动弹窗作废（以最新数据为准，取消未到期恢复）
        CancelPop(WidgetTypeOf(data));
        return DoUpdateWidget(widget, data, timeoutMs);
    }

    ScreenResult ScreenPageManager::DoUpdateWidget(Widget* widget, const WidgetData& data, uint32_t timeoutMs) {
        std::vector<Request> diff;
        if (!widget->BuildDiffRequests(data, diff)) {
            return ScreenResult::InvalidParameter;
        }
        if (diff.empty()) {
            return ScreenResult::OK;  // 屏与缓存已一致
        }
        auto result = mEngine.SendRequests(diff, timeoutMs);
        if (result == ScreenResult::OK) {
            widget->Commit(data);
        }
        return result;
    }

    ScreenResult ScreenPageManager::ShowPopWidget(const WidgetData& data, uint32_t autoResetMs, uint32_t timeoutMs) {
        std::lock_guard<std::mutex> lock(mOpMutex);
        if (!mEngine.IsInitialized()) {
            return ScreenResult::NotInitialized;
        }
        if (IsUpgrading()) {
            return ScreenResult::UpgradeInProgress;
        }
        auto type = WidgetTypeOf(data);
        auto* widget = FindWidget(type);
        if (widget == nullptr) {
            return ScreenResult::InvalidParameter;
        }

        // 恢复快照 = 弹窗前的数据；同控件已有活动弹窗时沿用其快照——
        // 此时 CurrentData() 已是上一次弹窗的数据，直接取用会导致恢复时把
        // 旧弹窗内容写回、弹窗永远不消失（叠加弹窗必须沿用最初基准）
        auto active = mActivePops.find(type);
        WidgetData snapshot = (active != mActivePops.end()) ? active->second.snapshot : widget->CurrentData();

        auto result = DoUpdateWidget(widget, data, timeoutMs);
        if (result != ScreenResult::OK) {
            // 发送失败：不改变弹窗登记（旧弹窗及其恢复定时器保持有效）
            return result;
        }

        // 新弹窗顶掉旧弹窗：发送成功后再取消旧的恢复定时（旧的恢复语义由新弹窗接管）
        if (active != mActivePops.end()) {
            mTimers.Cancel(active->second.timerId);
        }

        // 登记活动弹窗：autoResetMs > 0 自动恢复；0 转手动关闭模式（ClosePopWidget）
        PopState state;
        state.snapshot = std::move(snapshot);
        state.seq = ++mPopSeq;
        if (autoResetMs > 0) {
            // 恢复任务捕获弹窗代次：Cancel 迟于派发时旧任务仍会入队执行，
            // 靠代次比对丢弃——已派发的旧恢复任务不得误杀后续新弹窗
            state.timerId = mTimers.Schedule(autoResetMs, [this, type, seq = state.seq]() {
                std::lock_guard<std::mutex> timerLock(mOpMutex);
                auto it = mActivePops.find(type);
                // 代次不符 = 旧弹窗已被顶掉/关闭，放弃本次恢复
                if (it == mActivePops.end() || it->second.seq != seq) {
                    return;
                }
                WidgetData restore = it->second.snapshot;
                mActivePops.erase(it);
                auto* w = FindWidget(type);
                if (w != nullptr && !IsUpgrading()) {
                    DoUpdateWidget(w, restore, 3000);
                }
            });
            if (state.timerId == 0) {
                // 定时器未启动（异常路径）：退化为手动关闭模式，快照仍可恢复
                SLOG_WARN << "ShowPopWidget: timer not started, fallback to manual close";
            }
        }
        mActivePops[type] = std::move(state);
        return result;
    }

    ScreenResult ScreenPageManager::ClosePopWidget(WidgetType widgetType, uint32_t timeoutMs) {
        std::lock_guard<std::mutex> lock(mOpMutex);
        if (!mEngine.IsInitialized()) {
            return ScreenResult::NotInitialized;
        }
        if (IsUpgrading()) {
            return ScreenResult::UpgradeInProgress;
        }
        auto it = mActivePops.find(widgetType);
        if (it == mActivePops.end()) {
            return ScreenResult::OK;  // 幂等：无活动弹窗（已恢复/已关闭/从未弹出）
        }
        auto* widget = FindWidget(widgetType);
        if (widget == nullptr) {
            // 登记存在但控件缺失（异常路径）：仅清理登记，避免弹窗表泄漏
            mTimers.Cancel(it->second.timerId);
            mActivePops.erase(it);
            return ScreenResult::InvalidParameter;
        }
        // 先注销弹窗再回放：回放内部更新不再影响弹窗表；
        // 已派发未执行的旧恢复任务会因代次/表项缺失被丢弃
        WidgetData snapshot = it->second.snapshot;
        mTimers.Cancel(it->second.timerId);
        mActivePops.erase(it);
        return DoUpdateWidget(widget, snapshot, timeoutMs);
    }

    void ScreenPageManager::CancelPop(WidgetType type) {
        auto it = mActivePops.find(type);
        if (it != mActivePops.end()) {
            mTimers.Cancel(it->second.timerId);
            mActivePops.erase(it);
        }
    }

    // ── 状态校准 ──

    ScreenResult ScreenPageManager::ReadCurrentPage(uint16_t& pageId, uint32_t timeoutMs) {
        // 与页面/控件操作串行化：读应答在引擎内按 expectedAddr 匹配，并发读可能错配；
        // 同时拒绝升级期间的读——升级流程自身在轮询 0x00AA，插入读请求会干扰配对
        std::lock_guard<std::mutex> lock(mOpMutex);
        if (!mEngine.IsInitialized()) {
            return ScreenResult::NotInitialized;
        }
        if (IsUpgrading()) {
            return ScreenResult::UpgradeInProgress;
        }
        std::vector<uint8_t> resp;
        auto result = mEngine.ReadRegister(Addr::CurrentPageAddr, 0x01, resp, timeoutMs);
        if (result != ScreenResult::OK) {
            return result;
        }
        // 读应答格式：readLen 回显(1B) + 页面编号(2B)
        if (resp.size() < 3) {
            SLOG_ERROR << "ReadCurrentPage: short response (" << resp.size() << " bytes)";
            return ScreenResult::InvalidParameter;  // 应答格式非法（非链路错误）
        }
        pageId = static_cast<uint16_t>((resp[1] << 8) | resp[2]);
        return ScreenResult::OK;
    }

    // ── 触控 ──

    uint64_t ScreenPageManager::RegisterClick(DisplayPage page, ScreenButton button, TouchEventHandler handler) {
        auto* pageObj = FindPage(page);
        if (pageObj == nullptr) {
            SLOG_ERROR << "RegisterClick: unknown page 0x" << std::hex << static_cast<uint16_t>(page);
            return 0;
        }
        // 权威键值表校验：按钮必须归属该页（防止误注册与页面错配）
        if (!pageObj->HasTouchKey(button)) {
            SLOG_ERROR << "RegisterClick: button 0x" << std::hex << static_cast<uint16_t>(button)
                       << " not owned by page 0x" << static_cast<uint16_t>(page);
            return 0;
        }
        return DoRegisterClick(page, button, std::move(handler));
    }

    uint64_t ScreenPageManager::RegisterClick(ScreenButton button, TouchEventHandler handler) {
        // 无页面归属版本：二次确认页（删除/下载确认、取消）等未纳入 DisplayPage 管理的流程按钮
        // （页面元数据取默认值，业务层按 button 识别按钮）
        return DoRegisterClick(DisplayPage::Idle, button, std::move(handler));
    }

    uint64_t ScreenPageManager::DoRegisterClick(DisplayPage page, ScreenButton button, TouchEventHandler handler) {
        if (!handler) {
            SLOG_ERROR << "RegisterClick: handler is null";
            return 0;
        }
        auto keyVp = static_cast<uint16_t>(button);
        // 组件联动按钮由控件接管（OnTouch 的通路一优先级更高），此处拒绝注册：
        // 否则业务注册的回调永远不会被触发，属于静默失效
        // （mWidgetButtons 在 BuildRegistry 构建后运行期只读，无锁读取安全）
        if (mWidgetButtons.find(keyVp) != mWidgetButtons.end()) {
            SLOG_ERROR << "RegisterClick: button 0x" << std::hex << keyVp
                       << " is managed by a widget, register rejected";
            return 0;
        }

        std::lock_guard<std::mutex> lock(mTouchMutex);
        auto handle = mNextTouchHandleId++;  // 句柄分配在锁内，避免并发注册竞态
        // 同按钮重复注册：覆盖旧回调并清理旧句柄映射
        auto it = mTouchHandlers.find(keyVp);
        if (it != mTouchHandlers.end()) {
            mTouchHandles.erase(it->second.handle);
        }
        TouchEntry entry;
        entry.handle = handle;
        entry.page = page;  // 归属页元数据（事件分发时回传，便于业务层分发）
        entry.handler = std::move(handler);
        mTouchHandlers[keyVp] = std::move(entry);
        mTouchHandles[handle] = keyVp;
        SLOG_INFO << "Click registered: page=0x" << std::hex << static_cast<uint16_t>(page) << " button=0x" << keyVp
                  << " handle=" << handle;
        return handle;
    }

    bool ScreenPageManager::UnregisterClick(uint64_t handle) {
        if (handle == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mTouchMutex);
        auto it = mTouchHandles.find(handle);
        if (it == mTouchHandles.end()) {
            return false;
        }
        uint16_t keyVp = it->second;
        mTouchHandles.erase(it);
        // 仅当当前 VP 持有者仍是该句柄时删除（同 VP 被覆盖注册后旧句柄不得误删新回调）
        auto itHandler = mTouchHandlers.find(keyVp);
        if (itHandler != mTouchHandlers.end() && itHandler->second.handle == handle) {
            mTouchHandlers.erase(itHandler);
        }
        SLOG_INFO << "Click unregistered: handle=" << handle;
        return true;
    }

    void ScreenPageManager::HandleWidgetButton(Widget* widget, ScreenButton button) {
        // 执行器线程上下文：可安全加锁并同步发送（不在引擎接收通路上）
        std::function<void()> action;
        {
            std::lock_guard<std::mutex> lock(mOpMutex);
            if (!mEngine.IsInitialized() || IsUpgrading()) {
                return;
            }
            auto result = widget->HandleButton(button);
            if (result.needsRender) {
                // 状态变化（如翻页）：重渲染控件（差量发送变化的卡片字段 + 页码指示 + 图标开关）
                DoUpdateWidget(widget, widget->CurrentData(), 3000);
            }
            action = std::move(result.action);
        }
        // 业务动作在释放 mOpMutex 之后执行：回调内可安全调用本类同步接口
        //（若在锁内执行，回调再调 UpdateWidget/ShowPage 会二次加锁同一非递归 mutex → 死锁）
        if (action) {
            action();
        }
    }

    void ScreenPageManager::OnTouch(const TouchEvent& raw) {
        // 接收线程上下文：仅做按钮识别与任务投递，不执行任何业务代码
        TouchEvent event = raw;  // 引擎侧事件不含页面元数据，本层补全
        auto button = static_cast<ScreenButton>(event.keyVp);

        // 通路一：组件联动按钮（翻页/删除/下载等，语义依赖控件内部状态）→ 控件接管
        auto owned = mWidgetButtons.find(event.keyVp);
        if (owned != mWidgetButtons.end()) {
            Widget* widget = owned->second;
            mExecutor.SubmitTouch(event.keyVp, [this, widget, button]() { HandleWidgetButton(widget, button); });
            return;
        }

        // 通路二：页面/流程按钮 → 业务回调
        TouchEventHandler handler;
        {
            std::lock_guard<std::mutex> lock(mTouchMutex);
            auto it = mTouchHandlers.find(event.keyVp);
            if (it == mTouchHandlers.end()) {
                SLOG_WARN << "Unexpected touch frame keyVp=0x" << std::hex << event.keyVp << " (not registered)";
                return;
            }
            event.page = it->second.page;  // 注册时声明的归属页
            handler = it->second.handler;
        }
        // 业务回调投执行器（同按钮去重：重复点击被吸收，只执行首个）；
        // 回调内可安全调用本管理器同步接口（执行器不在引擎接收通路上）
        mExecutor.SubmitTouch(event.keyVp, [handler, event]() { handler(event); });
    }

    // ── 其他透传 ──

    MeetingMgmtListWidget* ScreenPageManager::MeetingMgmtWidget() const {
        // 受控 getter：动态类型校验，避免工厂映射变化时 static_cast 静默产生 UB
        auto* widget = FindWidget(WidgetType::MeetingMgmtList);
        return dynamic_cast<MeetingMgmtListWidget*>(widget);
    }

    ScreenResult ScreenPageManager::SetTouchEnabled(bool enabled, uint32_t timeoutMs) {
        // 升级期间升级流程自身在管理 0x00FC（已停触控以保证 ACK 配对），业务不得介入
        {
            std::lock_guard<std::mutex> lock(mOpMutex);
            if (IsUpgrading()) {
                return ScreenResult::UpgradeInProgress;
            }
        }
        return mEngine.SetTouchEnabled(enabled, timeoutMs);
    }

    ScreenResult ScreenPageManager::UpgradeFirmware(const std::vector<std::string>& filePaths,
                                                    uint32_t timeoutMsPerOp) {
        {
            // 在 mOpMutex 内置位（短临界区，不覆盖漫长的传输过程）：
            // 页面/控件操作要么在置位前完成，要么看到标记后拒绝，
            // 消除"通过 IsUpgrading 检查后升级才置位"造成的帧交织窗口
            std::lock_guard<std::mutex> lock(mOpMutex);
            if (mUpgradeActive.load(std::memory_order_acquire)) {
                return ScreenResult::UpgradeInProgress;
            }
            mUpgradeActive.store(true, std::memory_order_release);
        }

        auto result = mEngine.UpgradeFirmware(filePaths, timeoutMsPerOp);

        {
            std::lock_guard<std::mutex> lock(mOpMutex);
            mUpgradeActive.store(false, std::memory_order_release);
            // 屏复位（成功）或升级中断后（失败恢复/半写入）变量空间都可能已改变，
            // 主机缓存不再可信，统一失效以强制下次全量重发
            InvalidateAllWidgets();
        }
        return result;
    }

    void ScreenPageManager::InvalidateAllWidgets() {
        for (auto& [type, widget] : mWidgetIndex) {
            widget->Invalidate();
        }
    }

}  // namespace qifeng::screen
