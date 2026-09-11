/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_SCREEN_TYPES_H
#define HAL_SCREEN_SCREEN_TYPES_H

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace qifeng {
    /**
     * @brief 显示屏页面（页面 ID 即屏内背景图片序号，切页帧 0x0084 的载荷）
     *
     * 2026-09 页面包：待机页、会议中页、指纹鉴权页、会议管理页及其联动页。
     * 指纹录入页与升级页沿用既有实现（本次页面包未涉及）。
     */
    enum class DisplayPage : uint16_t {
        Idle = 0x0001,                    // 待机页：可用时长/AI 纪要/任务卡片、顶部 IP、提示
        Meeting = 0x0002,                 // 会议中页：会议信息、能量波形、录制时长
        Fingerprint = 0x0003,             // 指纹录入页（沿用）：录入进度、提示、结果
        Upgrade = 0x0004,                 // 系统升级页（沿用）：升级标题/类型、loading
        FingerprintAuth = 0x0005,         // 指纹鉴权页：指纹提示（鉴权/失败弹窗）
        MeetingEndConfirm = 0x0006,       // 结束会议二次确认页
        MeetingTypeSelect = 0x0009,       // 会议数据类型选择页（访客数据/私密数据）
        MeetingMgmt = 0x000B,             // 会议管理页：4 张会议卡片、页码、筛选、U 盘/用户名
        DownloadConfirmOther = 0x000D,    // 下载二次确认页（其他会议）
        DownloadConfirmSummary = 0x000F,  // 下载二次确认页（总结完成会议）
        DeleteConfirm = 0x0011,           // 删除二次确认页
        DeleteLoading = 0x0013,           // 删除 Loading 页
        DownloadLoading = 0x0014,         // 下载 Loading 页
        DownloadResult = 0x0015,          // 下载结果页
        DeleteResult = 0x0016,            // 删除结果页
    };

    /**
     * @brief 屏幕按钮（触控上传 VP 的类型化抽象，对上屏蔽底层地址）
     *
     * 按钮按下后屏主动上传标准读应答格式帧：5A A5 06 83 [按钮VP] 01 00 00，
     * 产品协议约定每个按钮键值恒为 0x0000，主机完全靠 VP 地址区分按钮，
     * 故枚举值即按钮 VP 地址。
     * 注意：触控键值段（0x32D6~0x34C9）必须与主机主动读的 VP 段分开规划，
     * 否则触控上传帧与读应答帧无法区分（两者格式完全相同）。
     */
    enum class ScreenButton : uint16_t {
        // ── 待机页（0x0001）──
        PublicMeeting = 0x32D6,   // 【公共会议】→ 会议录制页（屏内切页，同时上抛后端）
        PrivateMeeting = 0x32DD,  // 【私密会议】→ 指纹鉴权页
        MeetingManage = 0x32E4,   // 【会议管理】→ 会议数据类型选择页

        // ── 指纹鉴权页（0x0005）──
        FingerprintAuthCancel = 0x32EC,  // 【取消】→ 返回待机页

        // ── 会议数据类型选择页（0x0009）──
        SelectVisitorData = 0x32F4,  // 【访客会议数据】→ 会议管理页
        SelectPrivateData = 0x32FC,  // 【私密会议数据】→ 指纹鉴权页

        // ── 会议中页（0x0002）──
        TogglePauseResume = 0x3315,  // 【暂停/继续】切换会议录制状态
        EndMeeting = 0x331D,         // 【结束会议】→ 结束会议二次确认页

        // ── 结束会议二次确认页（0x0006）──
        EndMeetingConfirm = 0x3325,  // 【确认】→ 待机页
        EndMeetingCancel = 0x332D,   // 【取消】→ 返回会议中页

        // ── 会议管理页（0x000B）──
        MgmtFilterSummary = 0x3425,  // 【总结完成会议】筛选（业务侧换数据）
        MgmtFilterOther = 0x342D,    // 【其他会议】筛选（业务侧换数据）
        MgmtReturnHome = 0x3435,     // 【返回】→ 待机页

        // 卡片删除/下载（组件内部接管：槽位→全量下标换算后上抛业务）
        MgmtDelete1 = 0x343D,
        MgmtDelete2 = 0x3445,
        MgmtDelete3 = 0x344D,
        MgmtDelete4 = 0x3455,
        MgmtDownload1 = 0x345D,
        MgmtDownload2 = 0x3465,
        MgmtDownload3 = 0x346D,
        MgmtDownload4 = 0x3475,

        // 翻页（组件内部接管）
        MgmtFirstPage = 0x349D,
        MgmtPrevPage = 0x34A5,
        MgmtNextPage = 0x34AD,
        MgmtLastPage = 0x34B5,

        // ── 二次确认页共用（下载确认 0x000D/0x000F、删除确认 0x0011）──
        ConfirmCancel = 0x34BF,    // 【取消】→ 返回会议管理页
        DeleteConfirmOk = 0x34C9,  // 【确认删除】→ 删除 Loading 页

        // 下载二次确认页内容类型（0x000D/0x000F 共用键值）
        DownloadAudioOnly = 0x347D,  // 【仅音频】
        DownloadSummary = 0x3485,    // 【纪要】
        DownloadAudio = 0x348D,      // 【音频】
        DownloadFull = 0x3495,       // 【纪要+音频+转写原文】
    };

    /**
     * @brief 控件类型，与 WidgetData 变体备选项一一对应
     * @note 备选序 == 枚举序，新增控件须在枚举与变体两端同步追加（编译期断言兜底）
     */
    enum class WidgetType : uint8_t {
        IdleAvailability,  // 待机页-可用时长卡片（时长、饼图等级 1~12、百分比 0~100）
        IdleSummary,       // 待机页-AI 纪要卡片（已生成/待生成/总数、饼图等级 1~22）
        IdleTask,          // 待机页-任务卡片（图标 1~5、会议名称、进度条等级 1~13）
        IdleStatus,        // 待机页-提示卡片（1 待机无图 / 2~5 各类提示）
        DeviceInfo,        // 待机页-顶部（WAN 口 IP 显隐、IP 文本）
        MeetingBasicInfo,  // 会议中页-基本信息（发起人、开始时间）
        MeetingInfo,  // 会议中页-会议信息（类型、名称、操作提示、录音状态、进度、暂停图标）
        MeetingClock,     // 会议中页-录制时长（"HH:MM:SS" 文本，每秒更新）
        MeetingEnergy,    // 会议中页-能量波形（50 点，1~52，52 为暂停）
        Fingerprint,      // 指纹录入页-录入控件（进度 1~7、提示文本、结果 1/2/3）
        FingerprintAuth,  // 指纹鉴权页-指纹提示（1 无提示 / 2 操作频繁）
        Upgrade,          // 升级页-升级状态（标题/类型、loading 动画）
        MeetingMgmtList,  // 会议管理页-列表主体（筛选选中、4 张卡片、页码）
        MeetingMgmtInfo,  // 会议管理页-页头信息（U 盘状态、用户名称）
        DeleteLoading,    // 删除 Loading 页-进度指示（0 开始 / 1 停止）
        DownloadLoading,  // 下载 Loading 页-进度指示（0 开始 / 1 停止）
        DeleteResult,     // 删除结果页-结果图（1/2/3）与文字提示
        DownloadResult,   // 下载结果页-结果图（1/2/3）与文字提示
    };

    // ── 待机页 ──

    struct IdleAvailabilityData {
        uint32_t availableTime = 0;  // 可用时长(h)，0~3000
        uint16_t ratioLevel = 0;     // 可用时长饼图等级（1~12，1 可用空间最大）
        uint16_t ratioValue = 0;     // 可用时长百分比数值（0~100）
    };

    struct IdleSummaryData {
        uint32_t completedCount = 0;  // AI 纪要已生成数（0~288000）
        uint32_t pendingCount = 0;    // AI 纪要待生成数
        uint32_t totalCount = 0;      // 总会议数
        uint16_t ratioLevel = 0;      // 会议饼图等级（1~22，22 = 均已生成与未生成都为 0）
    };

    struct IdleTaskData {
        uint16_t status = 1;  // 任务卡片图标（1 暂无会议数据 / 2 待转写 / 3 转写中 / 4 待总结 / 5 总结中）
        // 会议名称（滚动文本）：**上限 40 汉字**，超长自动按 GBK 截断（不截半个汉字）
        std::string name {};
        uint16_t ratioLevel = 0;  // 任务进度条等级（1~13；13 = 显示背景色，不显示进度条）
    };

    struct IdleStatusData {
        // 1 待机无图 / 2 开启失败·可用时长不足 360 分钟（3s）/ 3 开启失败·麦克风异常（3s）
        // / 4 会议结束成功（3s）/ 5 会议时长已满·自动结束会议
        // 注：文档"范围"列标注 1~4，与枚举说明中的 5 不一致，5 已按枚举实现，待屏端确认
        uint16_t status = 1;
    };

    struct DeviceInfoData {
        uint16_t wanStatus = 1;    // 是否有 WAN 口 IP（1=无 WAN 口, 2=有 WAN 口）
        std::string ipAddress {};  // WAN 口 IP 文本（最大 11 位）# 例如："192.168.1.100"
    };

    // ── 会议中页 ──

    struct MeetingClockData {
        uint16_t hour = 0;    // 录制时长-时
        uint16_t minute = 0;  // 录制时长-分
        uint16_t second = 0;  // 录制时长-秒
    };

    struct MeetingBasicData {
        std::string sponsor {};    // 会议发起人（最大 16 字）# 例如："张三"
        std::string startTime {};  // 会议开始时间（固定 ASCII 16 字节）# 例如："2026/01/01/00:00"
    };

    struct MeetingInfoData {
        uint16_t type = 1;  // 开启会议类型（1=制空（无图）, 2=公共会议, 3=私密会议）
        // 会议名称（滚动文本）：**实际上限 16 汉字**（协议标称 40，但再长会写及录音状态
        // 字段，故控件层截断为 16 汉字），超长自动按 GBK 截断（不截半个汉字）
        std::string name {};
        uint16_t operationTip = 1;  // 会议中操作提示（1 无提示窗 / 2 结束会议无权限 3s /
                                    // 3 提示是否结束当前会议 10s / 4 多次结束会议无权限 10s）
        uint16_t audioStatus = 2;   // 录音状态（1=暂停, 2=录制中, 3=无音频输入）
        uint16_t ratioLevel = 0;    // 会议时长进度条（0~100）
        uint16_t pauseIcon = 1;     // 暂停/继续按键图标（1=暂停, 2=继续）
    };

    struct MeetingEnergyData {
        std::vector<uint16_t> energyValues {};  // 音频能量值（1~52，52 为暂停）# 共 50 个数据点
    };

    // ── 指纹页 ──

    struct FingerprintData {
        uint16_t ratioLevel = 1;  // 指纹录入进度（1~7，1 = 录入为零）
        std::string tip {};       // 指纹录入提示 # 例如："请抬起您的手指"
        uint16_t result = 1;      // 指纹录入结果（1=正常情况(无图), 2=成功, 3=失败）
    };

    struct FingerprintAuthData {
        uint16_t tip = 1;  // 指纹鉴权提示（1=无提示, 2=提示"指纹操作频繁"）
    };

    // ── 升级页（沿用）──

    struct UpgradeData {
        uint16_t title = 1;    // 升级标题（1: 系统升级中, 2: 成功, 3: 失败）
        uint16_t types = 1;    // 升级类型（1: 系统升级中, 2: 成功, 3: 失败）
        uint16_t loading = 0;  // loading 动画（0: 升级中, 1: 完成）
    };

    // ── 会议管理页 ──

    /**
     * @brief 会议管理页单条会议项（卡片显示内容）
     */
    struct MeetingMgmtItem {
        uint16_t status = 0;  // 会议状态（0=置空, 1=待处理, 2=处理中, 3=异常）
        // 会议名称（滚动文本）：**实际上限 24 汉字**（协议标称 40，但再长会写及邻接控件，
        // 故控件层截断为 24 汉字），超长自动按 GBK 截断（不截半个汉字）
        std::string name {};

        // 逐成员相等比较：控件据此判断会议数据集是否变化（新增成员时需同步补充）
        bool operator==(const MeetingMgmtItem& other) const {
            return status == other.status && name == other.name;
        }
    };

    /**
     * @brief 会议管理页列表数据：全量会议项由业务传入，分页显示由列表组件内部管理
     */
    struct MeetingMgmtListData {
        uint16_t filter = 1;  // 会议类型筛选（1=总结完成会议, 2=其他会议），决定选中的筛选图标
        std::vector<MeetingMgmtItem> items;  // 全量会议数据（每页 4 项）

        // 逐成员相等比较：控件据此判断整份列表数据是否变化（新增成员时需同步补充）
        bool operator==(const MeetingMgmtListData& other) const {
            return filter == other.filter && items == other.items;
        }
    };

    struct MeetingMgmtInfoData {
        uint16_t udiskStatus = 2;  // U 盘连接状态（1=有 U 盘, 2=无 U 盘）
        std::string userName {};   // 用户名称（最大 16 字）
    };

    // ── 删除 / 下载 Loading 与结果页 ──

    struct DeleteLoadingData {
        uint16_t loading = 0;  // 删除进度（0=开始值（进行中）, 1=停止值（完成））
    };

    struct DownloadLoadingData {
        uint16_t loading = 0;  // 下载进度（0=开始值（进行中）, 1=停止值（完成））
    };

    struct DeleteResultData {
        uint16_t result = 1;  // 删除结果图（1=无, 2=成功, 3=失败）
        std::string tip {};   // 结果文字提示（最大 36 字）
    };

    struct DownloadResultData {
        uint16_t result = 1;  // 下载结果图（1=无, 2=成功, 3=失败）
        std::string tip {};   // 结果文字提示（最大 36 字）
    };

    /**
     * @brief 控件数据变体，与 WidgetType 一一对应（variant 备选序 == WidgetType 枚举序）
     */
    using WidgetData =
        std::variant<IdleAvailabilityData, IdleSummaryData, IdleTaskData, IdleStatusData, DeviceInfoData,
                     MeetingBasicData, MeetingInfoData, MeetingClockData, MeetingEnergyData, FingerprintData,
                     FingerprintAuthData, UpgradeData, MeetingMgmtListData, MeetingMgmtInfoData, DeleteLoadingData,
                     DownloadLoadingData, DeleteResultData, DownloadResultData>;

    // 变体备选序必须与 WidgetType 枚举序严格一致（WidgetTypeOf 与控件类型校验依赖该约定），
    // 用编译期断言替代注释约定：任一侧新增/重排都会在此处直接报错，而不是运行期错配
    static_assert(std::variant_size_v<WidgetData> == static_cast<size_t>(WidgetType::DownloadResult) + 1,
                  "WidgetType 与 WidgetData 备选序不一致：请同步追加枚举与变体备选");
    static_assert(std::is_same_v<std::variant_alternative_t<0, WidgetData>, IdleAvailabilityData>,
                  "WidgetData 首个备选必须对应 WidgetType::IdleAvailability");

    /**
     * @brief 从控件数据推导控件类型（WidgetData 变体与 WidgetType 一一对应）
     */
    inline WidgetType WidgetTypeOf(const WidgetData& data) {
        return static_cast<WidgetType>(data.index());
    }

    /**
     * @brief 屏幕操作结果（widget/page/engine 各层的公共返回码）
     */
    enum class ScreenResult : uint8_t {
        OK = 0,             // 操作成功（会话内所有帧均已收到屏端应答）
        Timeout,            // 等待屏端应答超时，可据此判断显示链路是否通畅
        NotInitialized,     // 引擎尚未 Init 或已 Release，禁止调用
        InvalidParameter,   // 参数非法（控件与数据不匹配/页面未注册/帧编码失败等）
        TransportError,     // 传输层错误（串口发送失败/打开失败等）
        UpgradeInProgress,  // 固件升级进行中，页面切换与控件更新被拒绝
        UpgradeFailed       // 固件升级失败（文件读取/Flash 写入/轮询超时等）
    };

    /**
     * @brief 触控事件（屏按钮按下后主动上传）
     *
     * 屏上传帧为标准读应答格式 5A A5 06 83 [按钮VP] 01 00 00，
     * 产品协议约定按钮键值恒为 0x0000，主机完全靠 VP 地址识别按钮。
     */
    struct TouchEvent {
        DisplayPage page = DisplayPage::Idle;  // 注册时声明的按钮归属页（元数据，便于业务层分发）
        uint16_t keyVp = 0;                    // 按钮键值 VP 地址（识别按钮的唯一依据）
        uint16_t value = 0;                    // 键值（产品协议恒 0x0000）
    };

    /**
     * @brief 触控事件处理回调
     *
     * 执行上下文：任务执行器线程（独立于引擎接收/发送线程），**串行执行**。
     *   - 可安全调用 ScreenPageManager 的同步接口（ShowPage/UpdateWidget 等）；
     *   - **请勿阻塞**：回调耗时会推迟后续触控与弹窗恢复任务的执行（接收线程不受影响）。
     *     长耗时业务（网络/磁盘/等锁）请转投业务线程后立即返回；
     *   - 队列满或同按钮排队去重时，事件可能被丢弃，不应依赖"每次点击必达"。
     */
    using TouchEventHandler = std::function<void(const TouchEvent&)>;
}  // namespace qifeng

#endif  // HAL_SCREEN_SCREEN_TYPES_H
