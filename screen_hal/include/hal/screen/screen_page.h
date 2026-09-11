/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_SCREEN_PAGE_H
#define HAL_SCREEN_SCREEN_PAGE_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hal/screen/screen_hal.h"
#include "hal/screen/screen_types.h"
#include "hal/screen/screen_widget.h"

// 面向对象设计：将离散的页、控件、触控抽象为对应的层级对象
// Manager(页面/弹窗/点击) → Page(归属容器) → Widget(控件) → ScreenEngine(收发原语)
namespace qifeng::screen {

    /**
     * @brief 任务执行器：单线程串行消费两类任务的优先级队列
     *
     * 任务分类与调度策略：
     *   - Timer 任务（弹窗恢复/整页回退）：执行短、时效敏感 → 排队优先级高；
     *   - Touch 任务（触控回调/列表翻页）：同按钮（同 keyVp）去重——队列中
     *     已有该按钮未执行任务时抛弃新提交（保留第一个，后续视为重复点击）。
     *
     * 正在执行中的任务不可被抢占（单线程串行的固有代价）；优先级仅指排队次序。
     * 队列有界，满时丢弃并告警（触控为低频事件，正常远达不到上限）。
     */
    class TaskExecutor {
    public:
        using Task = std::function<void()>;

        TaskExecutor() = default;
        ~TaskExecutor();

        TaskExecutor(const TaskExecutor&) = delete;
        TaskExecutor& operator=(const TaskExecutor&) = delete;
        TaskExecutor(TaskExecutor&&) = delete;
        TaskExecutor& operator=(TaskExecutor&&) = delete;

        // 启动执行线程（幂等）
        void Start();
        // 停止线程：处理完当前任务后丢弃剩余任务（回调可能引用已析构对象，不可 drain）
        void Stop();

        // 提交计时任务（高优先级：弹窗恢复/页面回退，短平快）
        bool SubmitTimer(Task task);
        // 提交触控任务（普通优先级 + 同按钮去重，保留第一个）
        bool SubmitTouch(uint16_t touchKey, Task task);

    private:
        void Run();

        struct TaskItem {
            uint16_t touchKey = 0;  // 触控任务的去重键（计时任务无效）
            Task task {};
        };

        std::mutex mMutex;
        std::condition_variable mCv;
        std::deque<TaskItem> mTimerQueue {};           // 计时任务队列（优先消费）
        std::deque<TaskItem> mTouchQueue {};           // 触控任务队列
        std::unordered_set<uint16_t> mPendingKeys {};  // 触控队列中未执行的去重键
        std::thread mThread {};
        bool mRunning = false;
    };

    /**
     * @brief 一次性定时器控制器：弹窗超时/页面回退计时
     *
     * 独立线程只做到期检测，到期回调经注入的派发函数上抛（由 Manager 投递到
     * TaskExecutor 的高优先级队列），不在本线程直接执行——避免长回调阻塞
     * 后续到期项的检测，且统一在执行器线程串行消费。
     */
    class TimerController {
    public:
        using Callback = std::function<void()>;
        // 到期派发函数（默认直执行，Manager 注入 executor 提交）
        using Dispatcher = std::function<void(Callback)>;

        TimerController() = default;
        ~TimerController();

        TimerController(const TimerController&) = delete;
        TimerController& operator=(const TimerController&) = delete;
        TimerController(TimerController&&) = delete;
        TimerController& operator=(TimerController&&) = delete;

        // 启动内部定时线程（幂等）
        void Start();
        // 停止线程并清空所有未到期定时项
        void Stop();
        // 注入到期派发函数（须在 Start 前调用）
        void SetDispatcher(Dispatcher dispatcher);

        /**
         * @brief 注册一次性定时回调
         * @param delayMs 延迟毫秒数
         * @return 定时器 ID（0 表示未启动）
         */
        uint64_t Schedule(uint32_t delayMs, Callback callback);
        // 取消未到期的定时项
        void Cancel(uint64_t timerId);

    private:
        void Run();

        struct TimerItem {
            uint64_t id = 0;
            std::chrono::steady_clock::time_point deadline {};
            Callback callback {};
        };

        std::mutex mMutex;
        std::condition_variable mCv;
        std::vector<TimerItem> mItems {};
        std::thread mThread {};
        bool mRunning = false;
        uint64_t mNextId = 1;
        Dispatcher mDispatcher {};  // 到期派发（空则直接执行）
    };

    /**
     * @brief 页面：一组控件的归属容器 + 该页触控键值表
     *
     * DGUS 协议不提供"查询某 VP 绑定哪个页"的接口，页面归属由屏幕工程(14.BIN)
     * 静态定义。本对象按产品协议合同在主机侧镜像同一份归属关系，
     * 是切页重置（默认数据）、控件更新归属校验与触控注册校验的权威依据。
     */
    class Page {
    public:
        explicit Page(DisplayPage id) : mId(id) {
        }

        DisplayPage Id() const {
            return mId;
        }

        // 注册控件（同时注入控件对该页的归属关系），空指针忽略
        void AddWidget(std::unique_ptr<Widget> widget);
        // 按类型查找控件，未命中返回 nullptr
        Widget* FindWidget(WidgetType type) const;
        const std::map<WidgetType, std::unique_ptr<Widget>>& Widgets() const {
            return mWidgets;
        }

        // 注册该页的触控键值（权威键值表），重复注册忽略
        void AddTouchKey(ScreenButton button);
        // 键值是否归属该页
        bool HasTouchKey(ScreenButton button) const;

    private:
        DisplayPage mId;
        std::map<WidgetType, std::unique_ptr<Widget>> mWidgets {};  // ko: 控件类型，vo: 控件对象
        std::vector<ScreenButton> mTouchKeys {};                    // 该页按钮键值表（类型化抽象）
    };

    /**
     * @brief 弹窗默认显示时长（毫秒）
     *
     * 产品约定各类提示窗（录音失败、会议操作提示、指纹鉴权失败等）统一显示 3 秒后
     * 自动消失。作为 ShowPopWidget 的 autoResetMs 默认值；传 0 表示转为手动关闭模式。
     */
    inline constexpr uint32_t kDefaultPopResetMs = 3000;

    /**
     * @brief 屏幕页面管理器：业务层操作屏幕的唯一入口
     *
     * 组合页面注册表、控件差量缓存调度与引擎原语，提供页面显示/重置、
     * 控件更新、弹窗（超时自动恢复）、触控注册/分发、状态校准能力。
     * 页面与控件的归属关系在 Init 时构建（BuildRegistry），运行期不变。
     *
     * 线程模型（4 线程 + 业务调用线程）：
     *   - 引擎接收线程：帧解析与读会话配对，触控事件仅入队（不执行业务代码）；
     *   - 引擎发送线程：串口出队写；
     *   - 定时器线程：到期检测，回调投递到执行器高优先级队列；
     *   - 任务执行器线程：串行消费任务——计时任务（弹窗恢复/页面回退）排队
     *     优先，触控任务同按钮去重（保留第一个）；
     *   - 业务线程：调用本类同步接口。
     * 执行器线程可安全调用本类同步接口（不在引擎接收通路上）；
     * 触控回调在执行器线程上下文执行，**请勿阻塞**（见 TouchEventHandler）。
     *
     * 升级期间的行为：UpgradeFirmware 执行期间，所有页面/控件/读页/触控开关请求
     * 一律直接拒绝并返回 UpgradeInProgress（不排队、不缓存）。调用方应在
     * UpgradeFirmware 返回后重新发起请求——升级结束（无论成败）后控件差量缓存
     * 已全部失效，重发会自动全量下发，无需业务侧额外处理。
     */
    class ScreenPageManager {
    public:
        ScreenPageManager() = default;
        ~ScreenPageManager();

        ScreenPageManager(const ScreenPageManager&) = delete;
        ScreenPageManager& operator=(const ScreenPageManager&) = delete;
        ScreenPageManager(ScreenPageManager&&) = delete;
        ScreenPageManager& operator=(ScreenPageManager&&) = delete;

        /**
         * @brief 初始化：构建页面注册表、启动引擎与弹窗定时器
         */
        ScreenResult Init(const DisplayConfig& config);
        void Release();

        // ── 页面 ──

        /**
         * @brief 显示页面：切页帧始终发送（不依赖缓存差量，屏端可能已自行换页）
         *
         * 未指定的控件应用默认重置数据（消除上一会话残留值在新页上的闪现），
         * 指定的控件用给定数据，差量过滤生效。
         * 升级期间返回 UpgradeInProgress（见类说明）。
         * @param page 目标页面
         * @param data 该页部分控件的初始数据（控件类型由数据变体自动推导），空时全默认
         */
        ScreenResult ShowPage(DisplayPage page, const std::optional<std::vector<WidgetData>>& data = std::nullopt,
                              uint32_t timeoutMs = 3000);
        // 将指定页面全部控件重置为默认状态（不发切页帧）；升级期间返回 UpgradeInProgress
        ScreenResult ResetPage(DisplayPage page, uint32_t timeoutMs = 3000);

        // ── 控件 ──

        /**
         * @brief 更新当前页控件数据（差量：仅发送与缓存不同的字段）
         * 升级期间返回 UpgradeInProgress（见类说明）。
         * @param data 控件数据，控件类型由数据变体自动推导
         */
        ScreenResult UpdateWidget(const WidgetData& data, uint32_t timeoutMs = 3000);

        /**
         * @brief 弹窗式更新：立即显示控件数据，autoResetMs 后自动恢复到弹窗前数据
         *
         * 适用于录音失败提示、会议操作提示窗等"显示 N 秒后消失"的控件。
         * 恢复语义为快照回放：超时后写回弹窗前的控件数据；若弹窗期间业务
         * 已更新该控件，恢复操作会被丢弃（以最新数据为准）。
         * 同控件已有活动弹窗时，新弹窗沿用**最初**的弹窗快照作为恢复基准
         * （避免把上一次弹窗数据当作基准导致弹窗内容无法消除）。
         * 升级期间返回 UpgradeInProgress（见类说明）。
         * @param data 弹窗数据
         * @param autoResetMs 自动恢复延时(毫秒)，默认 kDefaultPopResetMs(3s)；
         *                    0 表示不自动恢复，转为手动关闭模式（由业务调用 ClosePopWidget）
         */
        ScreenResult ShowPopWidget(const WidgetData& data, uint32_t autoResetMs = kDefaultPopResetMs,
                                   uint32_t timeoutMs = 3000);

        /**
         * @brief 手动关闭弹窗：快照回放恢复到弹窗前数据
         *
         * 关闭 ShowPopWidget(autoResetMs=0) 的手动弹窗；对自动弹窗同样有效
         * （未到期时提前关闭）。幂等：无活动弹窗时直接返回 OK 不发帧。
         * 升级期间返回 UpgradeInProgress（见类说明）。
         * @param widgetType 弹窗控件类型（与 ShowPopWidget 数据变体推导的类型一致）
         */
        ScreenResult ClosePopWidget(WidgetType widgetType, uint32_t timeoutMs = 3000);

        /**
         * @brief 读取屏当前页面 ID（VP 0x0014，屏端自行切页后的状态校准依据）
         * 升级期间返回 UpgradeInProgress（屏端应答流被升级占用，不得插入读请求）。
         * @param pageId 屏上报的页面编号
         */
        ScreenResult ReadCurrentPage(uint16_t& pageId, uint32_t timeoutMs = 2000);

        // ── 触控 ──

        /**
         * @brief 注册页面按钮点击事件（按钮必须归属该页，见页面键值表）
         * @param page 按钮归属页
         * @param button 屏幕按钮（类型化抽象，对上屏蔽按钮 VP 地址）
         * @return 注册句柄（用于注销），失败返回 0；同按钮重复注册覆盖旧回调
         *
         * 已被控件接管的联动按钮（翻页/删除/下载等，见 Widget::OwnedButtons）
         * 会被拒绝（返回 0）——这些按钮由控件内部处理，注册也不会被触发。
         *
         * 回调契约：在执行器线程串行执行，可安全调用本类同步接口；
         * **建议回调不要阻塞**（长耗时业务转投业务线程后立即返回），
         * 否则会推迟后续触控与弹窗恢复任务；详见 TouchEventHandler 说明。
         */
        uint64_t RegisterClick(DisplayPage page, ScreenButton button, TouchEventHandler handler);

        /**
         * @brief 注册未纳入页面管理的流程按钮（二次确认页的删除/下载确认、取消等）
         * @return 注册句柄，失败返回 0；同样拒绝已被控件接管的按钮
         * @note 回调约束同上（执行器线程、请勿阻塞）
         */
        uint64_t RegisterClick(ScreenButton button, TouchEventHandler handler);
        bool UnregisterClick(uint64_t handle);

        // 触控总开关（会议进行中临时锁屏等场景）；升级期间返回 UpgradeInProgress
        ScreenResult SetTouchEnabled(bool enabled, uint32_t timeoutMs = 2000);

        // ── 组件访问（受控 getter：Init 后有效，具体类型由工厂保证）──

        /**
         * @brief 会议管理列表组件（注册删除/下载业务回调、查询分页状态）
         * @return 未初始化或会议管理页未注册返回 nullptr
         */
        MeetingMgmtListWidget* MeetingMgmtWidget() const;

        // ── 升级 ──

        /**
         * @brief 串口固件升级（升级期间页面/控件操作返回 UpgradeInProgress）
         *
         * 升级标记在 mOpMutex 内置位，与页面/控件操作串行化判定，
         * 避免"操作刚通过 IsUpgrading 检查、升级随即开始"导致的帧交织。
         * 升级结束（无论成败）后控件差量缓存全部失效，强制下次全量重发。
         */
        ScreenResult UpgradeFirmware(const std::vector<std::string>& filePaths, uint32_t timeoutMsPerOp = 3000);

        // 主机侧记录的当前页（ShowPage 成功后更新；屏端自行切页后以 ReadCurrentPage 为准）
        DisplayPage CurrentPage() const {
            return mCurrentPage.load(std::memory_order_acquire);
        }

    private:
        // 构建页面注册表：页面-控件-键值的权威归属（产品协议合同的主机侧镜像）
        void BuildRegistry();
        Page* FindPage(DisplayPage page) const;
        // 全页面控件类型 → 控件对象索引（运行期不变，无锁读取）
        Widget* FindWidget(WidgetType type) const;

        // 页面应用公共实现：switchFrame 控制是否发送切页帧
        ScreenResult ApplyPage(DisplayPage page, const std::optional<std::vector<WidgetData>>& data, bool switchFrame,
                               uint32_t timeoutMs);
        // 控件更新公共实现（调用方需持有 mOpMutex 或位于单线程上下文）
        ScreenResult DoUpdateWidget(Widget* widget, const WidgetData& data, uint32_t timeoutMs);

        // 失效全部控件差量缓存（发送失败/升级重启后，下次强制全量重发）
        void InvalidateAllWidgets();

        // 取消控件的活动弹窗（含未到期恢复定时器），无活动弹窗时空操作
        void CancelPop(WidgetType type);

        // 触控分发：接收线程上下文，仅做按钮识别与任务投递（不执行业务回调）
        void OnTouch(const TouchEvent& raw);
        uint64_t DoRegisterClick(DisplayPage page, ScreenButton button, TouchEventHandler handler);

        // 升级进行中判定（Manager 侧标记 + 引擎侧标记）
        bool IsUpgrading() const;

        /**
         * @brief 组件联动按钮处理（执行器线程 wrapper）
         *
         * 在 mOpMutex 内完成控件状态变更并取出待执行动作，动作在锁外执行：
         * 业务回调内可安全调用本类同步接口，不会因二次加锁自死锁。
         */
        void HandleWidgetButton(Widget* widget, ScreenButton button);

        ScreenEngine mEngine;
        TimerController mTimers;
        TaskExecutor mExecutor;

        std::map<DisplayPage, std::unique_ptr<Page>> mPages {};  // ko: 页面 ID，vo: 页面对象
        std::map<WidgetType, Widget*> mWidgetIndex {};           // 控件类型 → 控件（跨页索引）
        // 组件联动按钮路由表：按钮 VP → 接管控件（BuildRegistry 时由各控件 OwnedButtons 上报）
        std::unordered_map<uint16_t, Widget*> mWidgetButtons {};
        std::atomic<DisplayPage> mCurrentPage {DisplayPage::Idle};

        // 页面/控件/弹窗操作互斥（与触控分发线程、引擎接收线程互不嵌套）
        std::mutex mOpMutex;

        // Manager 侧升级标记：在 mOpMutex 内置位/清位，为页面与控件操作提供
        // 无竞态窗口的判定依据（引擎侧另有 mUpgradeInProgress 覆盖直接调用引擎的场景）
        std::atomic<bool> mUpgradeActive {false};

        // 活动弹窗表：控件类型 → 弹窗状态（快照 + 恢复定时器，手动模式无定时器）
        struct PopState {
            WidgetData snapshot {};  // 弹窗前数据（关闭/超时恢复的回放基准）
            uint64_t timerId = 0;    // 自动恢复定时器（0 = 手动关闭模式）
            uint64_t seq = 0;        // 弹窗代次（丢弃已派发的过期恢复任务）
        };
        std::map<WidgetType, PopState> mActivePops {};
        uint64_t mPopSeq = 0;  // 弹窗代次发生器（每次 ShowPopWidget 递增）

        // 触控注册表（VP→回调 + 句柄→VP 双索引）
        struct TouchEntry {
            uint64_t handle = 0;  // 注册句柄（注销凭证）
            DisplayPage page = DisplayPage::Idle;
            TouchEventHandler handler {};
        };
        std::mutex mTouchMutex;
        std::unordered_map<uint16_t, TouchEntry> mTouchHandlers {};  // keyVp → 回调
        std::unordered_map<uint64_t, uint16_t> mTouchHandles {};     // handle → keyVp
        uint64_t mNextTouchHandleId = 1;
    };

}  // namespace qifeng::screen

#endif  // HAL_SCREEN_SCREEN_PAGE_H
