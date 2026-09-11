/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_SCREEN_WIDGET_H
#define HAL_SCREEN_SCREEN_WIDGET_H

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hal/screen/protocol/screen_protocol.h"
#include "hal/screen/screen_types.h"

// 面向对象设计：将离散的控件抽象为对象，持有自身 VP 字段的编解码与差量缓存
namespace qifeng::screen {

    /**
     * @brief 组件联动按钮的处理结果
     *
     * HandleButton 由 ScreenPageManager 在 mOpMutex 保护下调用，因此组件内
     * 不得直接执行会回调业务的逻辑：业务回调若同步再调本管理器接口，会对同一
     * 非递归 mutex 二次加锁而死锁。约定拆成两部分返回——
     *   - needsRender：组件自身状态已变（如翻页），需调用方立即重渲染本控件；
     *   - action：捕获好全部上下文的业务回调闭包，由调用方在释放锁之后执行。
     */
    struct ButtonResult {
        bool needsRender = false;         // 组件状态变化，需调用方重渲染
        std::function<void()> action {};  // 需在锁外执行的业务回调（可为空）
    };

    /**
     * @brief 控件抽象：页面内一组 VP 字段的集合 + 数据编解码 + 差量缓存
     *
     * 一个控件可占多个 VP 地址（如会议卡片占 3 个字段），Widget 持有这些字段的
     * 差量缓存与当前生效数据，页面重置、弹窗恢复均以控件为单位。
     * 数据保持 variant 值语义（无堆分配、无生命周期问题），
     * 控件类型由数据变体自动推导（WidgetTypeOf），接口无需显式传 type。
     */
    class Widget {
    public:
        explicit Widget(WidgetType type) : mType(type) {
        }
        virtual ~Widget() = default;
        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;
        Widget(Widget&&) = delete;
        Widget& operator=(Widget&&) = delete;

        WidgetType Type() const {
            return mType;
        }
        DisplayPage OwnerPage() const {
            return mOwnerPage;
        }

        // 控件默认（空态）数据：页面重置与弹窗恢复的基准
        virtual WidgetData DefaultData() const = 0;
        // 用户数据 → 协议请求列表（含控件语义：空文本 0xFFFF 清空、数值钳制等）
        virtual bool ToRequests(const WidgetData& data, std::vector<Request>& out) const = 0;

        // ── 差量与状态（由 ScreenPageManager 调用，业务层不直接接触）──

        /**
         * @brief 构建差量请求：仅保留与缓存不同的字段，首次发送为全量
         * @return 数据变体与控件类型不匹配时返回 false
         */
        bool BuildDiffRequests(const WidgetData& data, std::vector<Request>& out);

        /**
         * @brief 发送成功后提交：按本控件完整字段值更新缓存，记录当前数据
         *
         * 屏上最终状态 = 全部字段的新值（未变化的字段此前已对齐），故缓存
         * 需记录完整值而非仅差量子集。
         */
        void Commit(const WidgetData& data);

        // 失效缓存（发送失败/升级重启后），下次强制全量发送
        void Invalidate();

        // 当前生效数据（上次成功提交的值），弹窗恢复快照的来源
        const WidgetData& CurrentData() const {
            return mCurrentData;
        }

        /**
         * @brief 工厂：按控件类型创建（内部完成默认数据初始化）
         * @return 未知的控件类型返回 nullptr
         */
        static std::unique_ptr<Widget> Create(WidgetType type);

        /**
         * @brief 声明本控件接管的联动按钮（Manager 据此构建触控路由）
         *
         * 按钮语义依赖控件内部状态（如列表槽位/页码）时归控件管理（本接口），
         * 纯流程按钮（二次确认页取消等）与页面级按钮仍走 RegisterClick。
         */
        virtual std::vector<ScreenButton> OwnedButtons() const {
            return {};
        }

        /**
         * @brief 接管按钮的处理入口（Manager 在执行器线程、持 mOpMutex 调用）
         * @return needsRender 表示组件状态已变需重渲染；action 由调用方在锁外执行
         */
        virtual ButtonResult HandleButton(ScreenButton button) {
            (void)button;
            return {};
        }

    protected:
        // 差量过滤前的缓存预处理钩子（如任务状态切换时强制失效文本字段缓存）
        virtual void PrepareDiff(const WidgetData& data) {
            (void)data;
        }

        // 清除指定地址的字段缓存（差量预处理钩子用）
        void InvalidateFields(std::initializer_list<uint16_t> addrs);
        // 查询字段缓存值（差量预处理钩子用），未缓存返回 nullptr
        const Value* FindCachedField(uint16_t addr) const;

        // 控制类请求钩子（不参与差量过滤、每次刷新都发送），如列表图标触控开关
        virtual void AppendControlRequests(std::vector<Request>& out) const {
            (void)out;
        }

        // ── 控件构建共享工具 ──
        // UTF-8 → 显示安全文本：下划线替换为连字符、按 GBK 字节数截断（不截半个字符）
        static std::string SanitizeText(const std::string& text, size_t maxGbkBytes);
        // 固定宽度数字字段：左补 '0' 右对齐，交给文本编码链（GBK+0xFFFF 结束符）
        static std::string PadLeftZero(const std::string& s, size_t width);
        // 普通文本显示：空文本写 0xFFFF 清空槽位，非空走文本编码链
        static void AppendText(std::vector<Request>& out, uint16_t addr, const std::string& text, uint16_t maxBytes);
        /**
         * @brief 滚动文本显示：内容自 baseAddr+3 起写入（协议约定）
         *
         * DGUS 滚动文本控件 VP 指向 3 字控制头（滚动属性由屏工程/22.BIN 预置），
         * 文本数据从 VP+3 开始，协议文档对每个滚动文本控件均显式给出 VP+3 地址。
         * 空文本同样在第 1 个字符位置写 0xFFFF，避免残留旧名称。
         */
        static void AppendRollingText(std::vector<Request>& out, uint16_t baseAddr, const std::string& text,
                                      uint16_t maxBytes);

    private:
        friend class Page;  // 页面注入控件归属

        WidgetType mType;
        DisplayPage mOwnerPage = DisplayPage::Idle;
        WidgetData mCurrentData {};                // 当前生效数据（发送成功后提交）
        std::map<uint16_t, Value> mFieldCache {};  // 字段级差量缓存：addr → 上次成功值
    };

    // ── 具体控件：仅实现数据转换（列表控件另含分页状态），无传输依赖 ──

    // 待机页-可用时长卡片
    class IdleAvailabilityWidget final : public Widget {
    public:
        IdleAvailabilityWidget() : Widget(WidgetType::IdleAvailability) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 待机页-AI 纪要卡片
    class IdleSummaryWidget final : public Widget {
    public:
        IdleSummaryWidget() : Widget(WidgetType::IdleSummary) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 待机页-任务卡片（状态切换时强制重发名称与进度条）
    class IdleTaskWidget final : public Widget {
    public:
        IdleTaskWidget() : Widget(WidgetType::IdleTask) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;

    protected:
        void PrepareDiff(const WidgetData& data) override;
    };

    // 待机页-提示卡片（弹窗类：提示 3s 后由管理器恢复）
    class IdleStatusWidget final : public Widget {
    public:
        IdleStatusWidget() : Widget(WidgetType::IdleStatus) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 待机页-顶部设备信息（WAN 口 IP 显隐与文本）
    class DeviceInfoWidget final : public Widget {
    public:
        DeviceInfoWidget() : Widget(WidgetType::DeviceInfo) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 会议中页-基本信息（发起人、开始时间）
    class MeetingBasicWidget final : public Widget {
    public:
        MeetingBasicWidget() : Widget(WidgetType::MeetingBasicInfo) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 会议中页-会议信息（操作提示窗为弹窗字段）
    class MeetingInfoWidget final : public Widget {
    public:
        MeetingInfoWidget() : Widget(WidgetType::MeetingInfo) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 会议中页-录制时长（每秒更新）
    class MeetingClockWidget final : public Widget {
    public:
        MeetingClockWidget() : Widget(WidgetType::MeetingClock) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 会议中页-音频能量波形
    class MeetingEnergyWidget final : public Widget {
    public:
        MeetingEnergyWidget() : Widget(WidgetType::MeetingEnergy) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 指纹录入页-录入控件
    class FingerprintWidget final : public Widget {
    public:
        FingerprintWidget() : Widget(WidgetType::Fingerprint) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 指纹鉴权页-指纹提示（鉴权失败弹窗：提示后由管理器恢复）
    class FingerprintAuthWidget final : public Widget {
    public:
        FingerprintAuthWidget() : Widget(WidgetType::FingerprintAuth) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 升级页-升级状态控件
    class UpgradeWidget final : public Widget {
    public:
        UpgradeWidget() : Widget(WidgetType::Upgrade) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    /**
     * @brief 会议管理列表组件：全量数据分页显示 + 联动按钮内部接管 + 图标触控自动开关
     *
     * 协议（920 会议管理页）：4 张会议卡片（会议状态 0~3、数据有无 1~2、会议名称
     * 滚动文本）、筛选选中图标、列表页数文本；卡片删除③⑤⑦⑨ / 下载④⑥⑧⑩ 的
     * 触控由 0xB0 指令开关，每次列表数据刷新都需重新下发（"当数据为零时候功能关闭"）。
     *
     * 职责边界：
     *   - 全量会议数据经 UpdateWidget(MeetingMgmtListData) 值语义传入，组件内部分页
     *     渲染；页码规则见下；
     *   - 组件联动按钮由 HandleButton 内部接管：翻页越界直接忽略（按钮触控已由图标
     *     开关关闭，"置灰"效果）；删除/下载完成槽位→全量下标换算与有效性校验后上抛；
     *   - 筛选按钮（总结完成/其他）语义为"业务换数据源"，不走组件内部，由业务
     *     RegisterClick 注册后自行换数据并 UpdateWidget；
     *   - 二次确认页【取消】等纯流程按钮不属于本控件，走全局 RegisterClick。
     *
     * 页码规则（数据集变化时）：
     *   - 增删会议 / 条目内容刷新（如会议状态异步变化）：**保留当前页**；数据减少
     *     导致当前页已不存在时落到**最后一页**（夹紧，不回第 1 页）；
     *   - 切换筛选：换数据源，回到第 1 页；
     *   - 数据为空：页码指示显示 "0/0"。
     *
     * 线程契约：handler 在执行器线程执行（可同步调用 Manager 接口）；
     * Set*Handler 可运行期随时调用（内部互斥，覆盖式注册）。
     */
    class MeetingMgmtListWidget final : public Widget {
    public:
        // 删除/下载回调：组件已完成槽位→全量下标换算与有效性校验
        using ItemHandler = std::function<void(size_t index, const MeetingMgmtItem& item)>;

        MeetingMgmtListWidget() : Widget(WidgetType::MeetingMgmtList) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
        std::vector<ScreenButton> OwnedButtons() const override;
        ButtonResult HandleButton(ScreenButton button) override;

        // ── 业务回调注册（对外）──
        // 回调在执行器线程串行执行（与触控回调同一线程），可安全调用 Manager 同步接口；
        // **请勿阻塞**——长耗时业务请转投业务线程后立即返回（详见 TouchEventHandler 说明）。

        // 点击卡片删除图标（index 为全量数据下标，屏端已跳转删除二次确认页）
        void SetDeleteHandler(ItemHandler handler);
        // 点击卡片下载图标（index 为全量数据下标，屏端已跳转下载二次确认页）
        void SetDownloadHandler(ItemHandler handler);

        // 当前页码（0 起）与总页数（诊断/测试用）
        size_t CurrentPage() const {
            return mPageIndex;
        }
        size_t PageCount() const;

    protected:
        void PrepareDiff(const WidgetData& data) override;
        void AppendControlRequests(std::vector<Request>& out) const override;

    private:
        static constexpr size_t PageSize = 4;  // 协议固定：每页 4 张会议卡片

        // 构建 0xB0 图标触控开关请求（协议帧：5AA5 0005 [seq 05] [0000 关/0001 开]）
        static void AppendIconTouch(std::vector<Request>& out, uint16_t iconSeq, bool enabled);
        // 按键值查表得卡片槽位（0~3），未命中返回 -1（键值非连续，不可算术换算）
        static int FindSlot(ScreenButton button);
        // 翻页目标换算（HandleButton 内部用）：页码变化返回 true
        bool OnPageButton(ScreenButton button);
        // 删除/下载指定卡片：槽位→全量下标换算 + 有效性校验，返回待锁外执行的业务回调
        // （闭包内已捕获回调与条目副本，执行期不触碰组件状态）
        std::function<void()> BuildItemAction(ScreenButton button) const;

        std::vector<MeetingMgmtItem> mItems {};  // 全量会议数据快照（PrepareDiff 时同步）
        uint16_t mFilter = 1;                    // 当前筛选（1 总结完成 / 2 其他），随数据同步
        size_t mPageIndex = 0;                   // 当前页码（0 起）

        // 业务回调（注册/读取互斥；执行在 Manager mOpMutex 串行化上下文内）
        mutable std::mutex mHandlerMutex;
        ItemHandler mDeleteHandler {};
        ItemHandler mDownloadHandler {};
    };

    // 会议管理页-页头信息（U 盘状态、用户名称）
    class MeetingMgmtInfoWidget final : public Widget {
    public:
        MeetingMgmtInfoWidget() : Widget(WidgetType::MeetingMgmtInfo) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 删除 Loading 页-进度指示
    class DeleteLoadingWidget final : public Widget {
    public:
        DeleteLoadingWidget() : Widget(WidgetType::DeleteLoading) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 下载 Loading 页-进度指示
    class DownloadLoadingWidget final : public Widget {
    public:
        DownloadLoadingWidget() : Widget(WidgetType::DownloadLoading) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 删除结果页-结果图与文字提示
    class DeleteResultWidget final : public Widget {
    public:
        DeleteResultWidget() : Widget(WidgetType::DeleteResult) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

    // 下载结果页-结果图与文字提示
    class DownloadResultWidget final : public Widget {
    public:
        DownloadResultWidget() : Widget(WidgetType::DownloadResult) {
        }
        WidgetData DefaultData() const override;
        bool ToRequests(const WidgetData& data, std::vector<Request>& out) const override;
    };

}  // namespace qifeng::screen

#endif  // HAL_SCREEN_SCREEN_WIDGET_H
