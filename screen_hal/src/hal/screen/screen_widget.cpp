/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "hal/screen/screen_widget.h"

#include <algorithm>
#include <array>

#include "common/logger.h"
#include "hal/screen/protocol/addr_map.h"

namespace qifeng::screen {

    // ── 滚动文本写入上限（GBK 编码：1 汉字 = 2 字节）──
    // 取值 = min(协议约定长度, 不越界覆盖邻接控件的安全长度)，超长由 SanitizeText
    // 自动按 GBK 字节截断（不截半个汉字），业务无需自行裁剪。
    // 各字段的越界目标详见 addr_map.h 文件头的"文本长度上限"说明。

    // 任务卡片名称：内容 0x328D 起，区间 0x328D~0x32B4 无邻接控件 → 按协议 40 汉字
    constexpr uint16_t kTaskNameMaxBytes = 80;
    // 会议名称：内容 0x3032 起，0x3044 为录音状态字段 → 限 16 汉字（含 0xFFFF 结束符不越界）
    constexpr uint16_t kMeetingNameMaxBytes = 32;
    // 会议管理卡片名称：内容 base+3 起，卡① 邻 0x3361（会议类型）、卡④ 邻 0x3400（发起人）
    // → 限 24 汉字（对四张卡均安全）
    constexpr uint16_t kMgmtCardNameMaxBytes = 48;

    // 0xB0 触控开关指令的连续下发间隔(毫秒)：产品协议要求"连续下发多条时每条间隔 20ms，
    // 也可先读 0xB0 为 0（上一条已处理完）再发下一条"。列表刷新一次要下发 8 条图标开关，
    // 无间隔会在 ~1ms 内冲出，屏端 OS 核可能漏处理 → 由引擎按 gapMs 在帧间等待
    constexpr uint16_t kIconTouchGapMs = 20;

    // ── Widget 基类：差量与状态 ──

    bool Widget::BuildDiffRequests(const WidgetData& data, std::vector<Request>& out) {
        // 数据变体与控件类型一一对应（screen_types.h 约定），错配直接拒绝
        if (data.index() != static_cast<size_t>(mType)) {
            SLOG_ERROR << "Widget type mismatch: widget=" << static_cast<int>(mType) << " data.index=" << data.index();
            return false;
        }

        PrepareDiff(data);

        std::vector<Request> all;
        if (!ToRequests(data, all)) {
            return false;
        }
        for (auto& req : all) {
            auto it = mFieldCache.find(req.address);
            if (it == mFieldCache.end() || !(it->second == req.value)) {
                out.push_back(std::move(req));
            }
        }
        // 控制类请求（如图标触控开关）不参与差量过滤，每次刷新都发送
        AppendControlRequests(out);
        return true;
    }

    void Widget::Commit(const WidgetData& data) {
        // 屏上最终状态 = 全部字段的新值（差量未发送的字段此前已对齐），缓存记完整值
        std::vector<Request> all;
        if (ToRequests(data, all)) {
            for (const auto& req : all) {
                mFieldCache[req.address] = req.value;
            }
        }
        mCurrentData = data;
    }

    void Widget::Invalidate() {
        mFieldCache.clear();
    }

    void Widget::InvalidateFields(std::initializer_list<uint16_t> addrs) {
        for (uint16_t addr : addrs) {
            mFieldCache.erase(addr);
        }
    }

    const Value* Widget::FindCachedField(uint16_t addr) const {
        auto it = mFieldCache.find(addr);
        return (it == mFieldCache.end()) ? nullptr : &it->second;
    }

    std::unique_ptr<Widget> Widget::Create(WidgetType type) {
        std::unique_ptr<Widget> widget;
        switch (type) {
            case WidgetType::IdleAvailability:
                widget = std::make_unique<IdleAvailabilityWidget>();
                break;
            case WidgetType::IdleSummary:
                widget = std::make_unique<IdleSummaryWidget>();
                break;
            case WidgetType::IdleTask:
                widget = std::make_unique<IdleTaskWidget>();
                break;
            case WidgetType::IdleStatus:
                widget = std::make_unique<IdleStatusWidget>();
                break;
            case WidgetType::DeviceInfo:
                widget = std::make_unique<DeviceInfoWidget>();
                break;
            case WidgetType::MeetingBasicInfo:
                widget = std::make_unique<MeetingBasicWidget>();
                break;
            case WidgetType::MeetingInfo:
                widget = std::make_unique<MeetingInfoWidget>();
                break;
            case WidgetType::MeetingClock:
                widget = std::make_unique<MeetingClockWidget>();
                break;
            case WidgetType::MeetingEnergy:
                widget = std::make_unique<MeetingEnergyWidget>();
                break;
            case WidgetType::Fingerprint:
                widget = std::make_unique<FingerprintWidget>();
                break;
            case WidgetType::FingerprintAuth:
                widget = std::make_unique<FingerprintAuthWidget>();
                break;
            case WidgetType::Upgrade:
                widget = std::make_unique<UpgradeWidget>();
                break;
            case WidgetType::MeetingMgmtList:
                widget = std::make_unique<MeetingMgmtListWidget>();
                break;
            case WidgetType::MeetingMgmtInfo:
                widget = std::make_unique<MeetingMgmtInfoWidget>();
                break;
            case WidgetType::DeleteLoading:
                widget = std::make_unique<DeleteLoadingWidget>();
                break;
            case WidgetType::DownloadLoading:
                widget = std::make_unique<DownloadLoadingWidget>();
                break;
            case WidgetType::DeleteResult:
                widget = std::make_unique<DeleteResultWidget>();
                break;
            case WidgetType::DownloadResult:
                widget = std::make_unique<DownloadResultWidget>();
                break;
            default:
                SLOG_ERROR << "Unknown widget type: " << static_cast<int>(type);
                return nullptr;
        }
        // 初始当前数据 = 默认空态（屏上电初始状态与协议"空态"基态一致的假定，
        // 首次 BuildDiffRequests 因缓存为空仍会全量发送，不依赖该假定）
        widget->mCurrentData = widget->DefaultData();
        return widget;
    }

    // ── 共享构建工具 ──

    std::string Widget::SanitizeText(const std::string& text, size_t maxGbkBytes) {
        // 屏幕不支持下划线显示，替换为连字符
        std::string result = text;
        std::replace(result.begin(), result.end(), '_', '-');
        if (maxGbkBytes == 0) {
            return result;
        }
        // 按 GBK 字节数截断：ASCII 计 1 字节，UTF-8 多字节字符（中文）计 2 字节，避免截断到半个字符
        std::string truncated;
        truncated.reserve(result.size());
        size_t gbkBytes = 0;
        size_t i = 0;
        while (i < result.size()) {
            auto ch = static_cast<uint8_t>(result[i]);
            size_t charLen = 1;
            size_t charGbk = 1;
            if (ch >= 0xF0) {
                charLen = 4;
                charGbk = 2;
            } else if (ch >= 0xE0) {
                charLen = 3;
                charGbk = 2;
            } else if (ch >= 0xC0) {
                charLen = 2;
                charGbk = 2;
            }
            if (i + charLen > result.size()) {
                break;
            }
            if (gbkBytes + charGbk > maxGbkBytes) {
                break;
            }
            truncated.append(result, i, charLen);
            gbkBytes += charGbk;
            i += charLen;
        }
        return truncated;
    }

    std::string Widget::PadLeftZero(const std::string& s, size_t width) {
        std::string r = s;
        if (r.size() > width) {
            r.resize(width);
        }
        if (r.size() < width) {
            r.insert(0, width - r.size(), '0');
        }
        return r;
    }

    void Widget::AppendText(std::vector<Request>& out, uint16_t addr, const std::string& text, uint16_t maxBytes) {
        // 屏幕固件遇 0xFFFF 即停止渲染且不纳入 VP 地址计数，缩短文本时 FFFF 之后
        // 的残留字节不会被显示，无需固定长度填充覆盖
        if (text.empty()) {
            out.emplace_back(addr, static_cast<uint16_t>(0xFFFF));
        } else {
            out.emplace_back(addr, Text {SanitizeText(text, maxBytes), maxBytes});
        }
    }

    void Widget::AppendRollingText(std::vector<Request>& out, uint16_t baseAddr, const std::string& text,
                                   uint16_t maxBytes) {
        // 内容自 VP+3 起（前 3 字为滚动控件控制头，属性由屏工程预置，主机不写）
        AppendText(out, static_cast<uint16_t>(baseAddr + 3), text, maxBytes);
    }

    // ── 待机页控件 ──

    WidgetData IdleAvailabilityWidget::DefaultData() const {
        // 默认空态：时长 0 / 等级 1（最大可用）/ 百分比 0
        return IdleAvailabilityData {0, 1, 0};
    }

    bool IdleAvailabilityWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        const auto& avail = std::get<IdleAvailabilityData>(data);
        out.emplace_back(Addr::AvailableTime, static_cast<uint16_t>(avail.availableTime));
        out.emplace_back(Addr::AvailableRatioLevel, avail.ratioLevel);
        out.emplace_back(Addr::AvailableRatioText, avail.ratioValue);
        return true;
    }

    WidgetData IdleSummaryWidget::DefaultData() const {
        // 22 = 已生成/未生成均为 0 的空态等级
        return IdleSummaryData {0, 0, 0, 22};
    }

    bool IdleSummaryWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        const auto& summary = std::get<IdleSummaryData>(data);
        // 计数为长整型（2 字），屏端按 0x82 写变量接收高低字
        out.emplace_back(Addr::SummaryCompletedCount, summary.completedCount);
        out.emplace_back(Addr::SummaryPendingCount, summary.pendingCount);
        out.emplace_back(Addr::SummaryTotalCount, summary.totalCount);
        out.emplace_back(Addr::SummaryRatioLevel, summary.ratioLevel);
        return true;
    }

    WidgetData IdleTaskWidget::DefaultData() const {
        // 1 = 暂无会议数据，名称空（滚动文本写 0xFFFF 清空），13 = 不显示进度条
        return IdleTaskData {1, "", 13};
    }

    bool IdleTaskWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        const auto& task = std::get<IdleTaskData>(data);
        out.emplace_back(Addr::TaskStatus, task.status);
        // 名称上限 40 汉字（80 字节），超长自动截断
        AppendRollingText(out, Addr::TaskName, task.name, kTaskNameMaxBytes);
        out.emplace_back(Addr::TaskRatioLevel, task.ratioLevel);
        return true;
    }

    void IdleTaskWidget::PrepareDiff(const WidgetData& data) {
        // 状态切换时清除名称与进度条缓存：状态从"有待办"变为"暂无"时，
        // 名称需写回 0xFFFF 清空、进度条需回到 13（背景色），差量不得吞掉这两次写入
        const auto& task = std::get<IdleTaskData>(data);
        const Value* cached = FindCachedField(Addr::TaskStatus);
        if (cached == nullptr || *cached != Value(task.status)) {
            InvalidateFields({static_cast<uint16_t>(Addr::TaskName + 3), Addr::TaskRatioLevel});
        }
    }

    WidgetData IdleStatusWidget::DefaultData() const {
        // 1 = 待机状态（无图）
        return IdleStatusData {1};
    }

    bool IdleStatusWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        out.emplace_back(Addr::IdleTipStatus, std::get<IdleStatusData>(data).status);
        return true;
    }

    WidgetData DeviceInfoWidget::DefaultData() const {
        // wanStatus=1 无 WAN 口（不显示图标），IP 文本为空（写 FFFF 清空槽位）
        return DeviceInfoData {1, ""};
    }

    bool DeviceInfoWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        const auto& info = std::get<DeviceInfoData>(data);
        out.emplace_back(Addr::DeviceWanStatus, info.wanStatus);
        AppendText(out, Addr::DeviceIPAddress, info.ipAddress, 16);  // 最大 11 位 + 结束符余量
        return true;
    }

    // ── 会议中页控件 ──

    WidgetData MeetingBasicWidget::DefaultData() const {
        return MeetingBasicData {};  // 发起人空、开始时间空
    }

    bool MeetingBasicWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        const auto& basic = std::get<MeetingBasicData>(data);
        AppendText(out, Addr::MeetingSponsor, basic.sponsor, 32);  // 最大 16 字
        // 文档要求文本显示"2026/01/01/00:00"，固定 ASCII 16 字节
        AppendText(out, Addr::MeetingStartTime, basic.startTime, 16);
        return true;
    }

    WidgetData MeetingInfoWidget::DefaultData() const {
        // type=1 无卡片 / operationTip=1 无提示窗 / 录音状态=2 录制中 / 暂停图标=1 暂停
        return MeetingInfoData {1, "", 1, 2, 0, 1};
    }

    bool MeetingInfoWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        const auto& meeting = std::get<MeetingInfoData>(data);
        out.emplace_back(Addr::MeetingType, meeting.type);
        // 名称上限 16 汉字（32 字节），超长自动截断——再长会写及录音状态字段 0x3044
        AppendRollingText(out, Addr::MeetingName, meeting.name, kMeetingNameMaxBytes);
        out.emplace_back(Addr::MeetingOperationTip, meeting.operationTip);
        out.emplace_back(Addr::MeetingAudioStatus, meeting.audioStatus);
        out.emplace_back(Addr::MeetingRatioLevel, meeting.ratioLevel);
        out.emplace_back(Addr::MeetingPauseIcon, meeting.pauseIcon);
        return true;
    }

    WidgetData MeetingClockWidget::DefaultData() const {
        return MeetingClockData {};  // 00:00:00
    }

    bool MeetingClockWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        // 文档要求 0x3189 写入 ASCII 文本 "HH:MM:SS"，每秒更新
        const auto& clock = std::get<MeetingClockData>(data);
        uint16_t h = std::min(clock.hour, static_cast<uint16_t>(99));
        uint16_t m = std::min(clock.minute, static_cast<uint16_t>(59));
        uint16_t s = std::min(clock.second, static_cast<uint16_t>(59));
        auto pad2 = [](uint16_t v) -> std::string { return v < 10 ? "0" + std::to_string(v) : std::to_string(v); };
        out.emplace_back(Addr::MeetingClock, Text {pad2(h) + ":" + pad2(m) + ":" + pad2(s), 8});
        return true;
    }

    WidgetData MeetingEnergyWidget::DefaultData() const {
        // 52 = 协议定义的暂停值，50 点全部置 52
        return MeetingEnergyData {std::vector<uint16_t>(50, 52)};
    }

    bool MeetingEnergyWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        // 文档要求范围 1~52，52 为暂停值
        const auto& energy = std::get<MeetingEnergyData>(data);
        std::vector<uint16_t> clamped(energy.energyValues.size());
        for (size_t i = 0; i < energy.energyValues.size(); ++i) {
            clamped[i] = std::clamp(energy.energyValues[i], static_cast<uint16_t>(1), static_cast<uint16_t>(52));
        }
        out.emplace_back(Addr::MeetingEnergy, std::move(clamped));
        return true;
    }

    // ── 指纹页控件 ──

    WidgetData FingerprintWidget::DefaultData() const {
        // 进度 1 = 录入为零，结果 1 = 无图
        return FingerprintData {1, "", 1};
    }

    bool FingerprintWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        const auto& fp = std::get<FingerprintData>(data);
        out.emplace_back(Addr::FingerprintRatioLevel, fp.ratioLevel);
        AppendText(out, Addr::FingerprintTip, fp.tip, 48);
        out.emplace_back(Addr::FingerprintResult, fp.result);
        return true;
    }

    WidgetData FingerprintAuthWidget::DefaultData() const {
        // 1 = 无提示
        return FingerprintAuthData {1};
    }

    bool FingerprintAuthWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        out.emplace_back(Addr::FingerprintAuthTip, std::get<FingerprintAuthData>(data).tip);
        return true;
    }

    // ── 升级页控件 ──

    WidgetData UpgradeWidget::DefaultData() const {
        // 1/1/0 = 升级中基态
        return UpgradeData {1, 1, 0};
    }

    bool UpgradeWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        const auto& up = std::get<UpgradeData>(data);
        out.emplace_back(Addr::UpdateSystemTip, up.title);
        out.emplace_back(Addr::UpdateSystemTypes, up.types);
        out.emplace_back(Addr::UpgradeLoading, up.loading);
        return true;
    }

    // ── 会议管理列表控件 ──

    WidgetData MeetingMgmtListWidget::DefaultData() const {
        // 空列表：筛选=总结完成会议、0 张卡片；页码指示渲染为 "0/0"
        return MeetingMgmtListData {1, {}};
    }

    void MeetingMgmtListWidget::PrepareDiff(const WidgetData& data) {
        // 同步数据快照与筛选；页码按下述规则调整（见类文档"页码规则"）
        const auto& list = std::get<MeetingMgmtListData>(data);
        const bool filterChanged = (list.filter != mFilter);
        mItems = list.items;  // 先同步，PageCount() 依赖它
        mFilter = list.filter;

        if (filterChanged) {
            mPageIndex = 0;  // 换数据源：从第 1 页重新开始
            return;
        }
        // 增删会议 / 条目内容刷新：保留当前页；当前页已不存在时落到最后一页（夹紧）
        const size_t total = PageCount();
        if (total == 0) {
            mPageIndex = 0;
        } else if (mPageIndex >= total) {
            mPageIndex = total - 1;
        }
        // 页数足够时页码不变（含纯内容刷新，如会议状态异步变化；翻页重渲染亦走此路径）
    }

    size_t MeetingMgmtListWidget::PageCount() const {
        return (mItems.size() + PageSize - 1) / PageSize;
    }

    void MeetingMgmtListWidget::SetDeleteHandler(ItemHandler handler) {
        std::lock_guard<std::mutex> lock(mHandlerMutex);
        mDeleteHandler = std::move(handler);
    }

    void MeetingMgmtListWidget::SetDownloadHandler(ItemHandler handler) {
        std::lock_guard<std::mutex> lock(mHandlerMutex);
        mDownloadHandler = std::move(handler);
    }

    std::vector<ScreenButton> MeetingMgmtListWidget::OwnedButtons() const {
        // 组件联动按钮全集：删除×4 + 下载×4 + 翻页×4
        // （键值语义依赖列表内部状态：槽位/页码/全量数据）
        return {
            ScreenButton::MgmtDelete1,   ScreenButton::MgmtDelete2,   ScreenButton::MgmtDelete3,
            ScreenButton::MgmtDelete4,   ScreenButton::MgmtDownload1, ScreenButton::MgmtDownload2,
            ScreenButton::MgmtDownload3, ScreenButton::MgmtDownload4, ScreenButton::MgmtFirstPage,
            ScreenButton::MgmtPrevPage,  ScreenButton::MgmtNextPage,  ScreenButton::MgmtLastPage,
        };
    }

    int MeetingMgmtListWidget::FindSlot(ScreenButton button) {
        // 按键值查表得卡片槽位（0~3），未命中返回 -1
        static constexpr std::array<std::pair<ScreenButton, size_t>, 8> SlotTable = {{
            {ScreenButton::MgmtDelete1, 0},
            {ScreenButton::MgmtDelete2, 1},
            {ScreenButton::MgmtDelete3, 2},
            {ScreenButton::MgmtDelete4, 3},
            {ScreenButton::MgmtDownload1, 0},
            {ScreenButton::MgmtDownload2, 1},
            {ScreenButton::MgmtDownload3, 2},
            {ScreenButton::MgmtDownload4, 3},
        }};
        for (const auto& [btn, slot] : SlotTable) {
            if (btn == button) {
                return static_cast<int>(slot);
            }
        }
        return -1;
    }

    bool MeetingMgmtListWidget::OnPageButton(ScreenButton button) {
        // 翻页目标换算（越界/页码未变返回 false——按钮触控已由图标开关关闭）
        const size_t totalPages = PageCount();
        size_t target = mPageIndex;
        if (button == ScreenButton::MgmtFirstPage) {
            target = 0;
        } else if (button == ScreenButton::MgmtPrevPage) {
            target = (mPageIndex > 0) ? mPageIndex - 1 : 0;
        } else if (button == ScreenButton::MgmtNextPage) {
            target = mPageIndex + 1;
        } else if (button == ScreenButton::MgmtLastPage) {
            target = (totalPages > 0) ? totalPages - 1 : 0;
        } else {
            return false;  // 非翻页按钮不处理
        }
        if (totalPages == 0 || target >= totalPages || target == mPageIndex) {
            return false;  // 越界翻页或页码未变化（如首页按"首页"）
        }
        mPageIndex = target;
        return true;
    }

    ButtonResult MeetingMgmtListWidget::HandleButton(ScreenButton button) {
        // 调用方（Manager HandleWidgetButton）持有 mOpMutex：mItems/mPageIndex 访问
        // 与 PrepareDiff（UpdateWidget 路径）天然互斥，无需额外同步

        // 1) 翻页按钮：页码变化 → 要求调用方重渲染本控件
        if (OnPageButton(button)) {
            return {true, {}};
        }

        // 2) 删除/下载指定卡片：返回锁外执行的业务回调
        // （锁内执行会与 Manager 接口二次加锁死锁，故以闭包形式返回）
        return {false, BuildItemAction(button)};
    }

    std::function<void()> MeetingMgmtListWidget::BuildItemAction(ScreenButton button) const {
        int slot = FindSlot(button);
        if (slot < 0) {
            return {};  // 非本控件按钮（翻页已在上文处理）
        }
        size_t index = mPageIndex * PageSize + static_cast<size_t>(slot);
        if (index >= mItems.size()) {
            return {};  // 槽位无数据（图标触控已关闭，防御 0xB0 未生效窗口）
        }

        const bool isDelete = (button == ScreenButton::MgmtDelete1 || button == ScreenButton::MgmtDelete2 ||
                               button == ScreenButton::MgmtDelete3 || button == ScreenButton::MgmtDelete4);
        ItemHandler handler;
        {
            std::lock_guard<std::mutex> lock(mHandlerMutex);
            handler = isDelete ? mDeleteHandler : mDownloadHandler;
        }
        if (!handler) {
            return {};
        }
        // 捕获下标与条目副本：闭包在释放 mOpMutex 后执行，执行期不得再触碰组件成员
        MeetingMgmtItem item = mItems[index];
        return [handler, index, item]() { handler(index, item); };
    }

    bool MeetingMgmtListWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        // 卡片渲染使用 mItems 快照（PrepareDiff 已同步），页码指示由分页状态决定
        const auto& list = std::get<MeetingMgmtListData>(data);

        // 筛选选中图标：1=选中, 2=未选中
        out.emplace_back(Addr::MgmtFilterSummary, static_cast<uint16_t>(list.filter == 1 ? 1 : 2));
        out.emplace_back(Addr::MgmtFilterOther, static_cast<uint16_t>(list.filter == 2 ? 1 : 2));

        // 4 张会议卡片：本页第 i 项 → 卡片 i
        for (size_t i = 0; i < PageSize; ++i) {
            auto idx = mPageIndex * PageSize + i;
            if (idx < mItems.size()) {
                // 有会议：状态 0~3、有无=1、名称（滚动文本，上限 24 汉字，超长自动截断）
                out.emplace_back(Addr::MgmtCardStatus[i], mItems[idx].status);
                out.emplace_back(Addr::MgmtCardPresence[i], static_cast<uint16_t>(1));
                AppendRollingText(out, Addr::MgmtCardName[i], mItems[idx].name, kMgmtCardNameMaxBytes);
            } else {
                // 无会议：状态=0（置空）、有无=2、名称写 0xFFFF 清空槽位
                out.emplace_back(Addr::MgmtCardStatus[i], static_cast<uint16_t>(0));
                out.emplace_back(Addr::MgmtCardPresence[i], static_cast<uint16_t>(2));
                out.emplace_back(static_cast<uint16_t>(Addr::MgmtCardName[i] + 3), static_cast<uint16_t>(0xFFFF));
            }
        }

        // 页码指示 "cur/total"（如 1/2），空列表显示 "0/0"
        size_t total = PageCount();
        out.emplace_back(Addr::MgmtPageIndicator,
                         Text {std::to_string(total == 0 ? 0 : mPageIndex + 1) + "/" + std::to_string(total), 16});
        return true;
    }

    void MeetingMgmtListWidget::AppendControlRequests(std::vector<Request>& out) const {
        // 协议要求：每次列表数据刷新都需重新下发图标触控开关状态
        // ③⑤⑦⑨ 删除 / ④⑥⑧⑩ 下载：当前页该卡片位置有会议才开启（无数据 = 置灰）
        for (size_t i = 0; i < PageSize; ++i) {
            bool has = (mPageIndex * PageSize + i) < mItems.size();
            AppendIconTouch(out, ListIcon::CardDelete[i], has);
            AppendIconTouch(out, ListIcon::CardDownload[i], has);
        }
    }

    void MeetingMgmtListWidget::AppendIconTouch(std::vector<Request>& out, uint16_t iconSeq, bool enabled) {
        // 复现协议部分触控指令：5A A5 0B 82 00B0 5AA5 0005 [seq 05] [0000 关 / 0001 开]
        Request req;
        req.address = Addr::IconTouchControl;
        req.value = std::vector<uint16_t> {0x5AA5, 0x0005, static_cast<uint16_t>((iconSeq << 8) | 0x0005),
                                           static_cast<uint16_t>(enabled ? 0x0001 : 0x0000)};
        req.gapMs = kIconTouchGapMs;  // 连续下发需间隔 20ms，由引擎在帧间执行
        out.push_back(std::move(req));
    }

    // ── 会议管理页页头 / 结果页控件 ──

    WidgetData MeetingMgmtInfoWidget::DefaultData() const {
        // 无 U 盘、用户名空
        return MeetingMgmtInfoData {2, ""};
    }

    bool MeetingMgmtInfoWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        const auto& info = std::get<MeetingMgmtInfoData>(data);
        out.emplace_back(Addr::MgmtUdiskStatus, info.udiskStatus);
        AppendText(out, Addr::MgmtUserName, info.userName, 32);  // 最大 16 字
        return true;
    }

    WidgetData DeleteLoadingWidget::DefaultData() const {
        return DeleteLoadingData {0};  // 0 = 进行中
    }

    bool DeleteLoadingWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        out.emplace_back(Addr::DeleteLoading, std::get<DeleteLoadingData>(data).loading);
        return true;
    }

    WidgetData DownloadLoadingWidget::DefaultData() const {
        return DownloadLoadingData {0};  // 0 = 进行中
    }

    bool DownloadLoadingWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        out.emplace_back(Addr::DownloadLoading, std::get<DownloadLoadingData>(data).loading);
        return true;
    }

    WidgetData DeleteResultWidget::DefaultData() const {
        return DeleteResultData {1, ""};  // 1 = 无结果图
    }

    bool DeleteResultWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        const auto& result = std::get<DeleteResultData>(data);
        out.emplace_back(Addr::DeleteResultImage, result.result);
        AppendText(out, Addr::DeleteResultTip, result.tip, 72);  // 最大 36 字
        return true;
    }

    WidgetData DownloadResultWidget::DefaultData() const {
        return DownloadResultData {1, ""};  // 1 = 无结果图
    }

    bool DownloadResultWidget::ToRequests(const WidgetData& data, std::vector<Request>& out) const {
        const auto& result = std::get<DownloadResultData>(data);
        out.emplace_back(Addr::DownloadResultImage, result.result);
        AppendText(out, Addr::DownloadResultTip, result.tip, 72);  // 最大 36 字
        return true;
    }

}  // namespace qifeng::screen
