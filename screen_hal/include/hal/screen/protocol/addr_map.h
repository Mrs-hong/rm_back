/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_PROTOCOL_ADDR_MAP_H
#define HAL_SCREEN_PROTOCOL_ADDR_MAP_H

#include <array>
#include <cstdint>

/*
 * ── 地址表依据 ─────────────────────────────────────────────────────────────
 * 2026-09 版本页面包（T5L_DGUSII/920）：待机页、会议中页、会议管理页及联动页。
 * VP 地址空间是全局的（切页不清零），不同页面的区间重叠会在写入时互相覆盖。
 *
 * 地址列读法：表中每个控件在"地址(16进制)"列可能给出两个值（上下相邻），
 *   下方 = 变量地址 VP（主机读写用），上方 = 描述指针 SP（改控件属性用，本层不使用）。
 * 例如会议名称 VP=0x302F / SP=0x321B；会议时长 VP=0x3189 / SP=0x327D。
 * 滚动文本控件另有约定：文本内容自 VP+3 起写入（见 Widget::AppendRollingText），
 * 文档对卡片名称给出的"3341+3 → 3344"即此含义。
 *
 * 原文档待定项（已联调确定）：
 *   - TaskName     0x328A（内容 0x328D）；文档"3289+3=328D"的基址取 0x328A 方自洽
 *   - MgmtUserName 0x3536
 *
 * ⚠ 文本长度上限：写入超长会越界覆盖邻接控件，故由控件层截断（见 screen_widget.cpp
 *   AppendRollingText 调用处的常量），此处记录各控件的实际上限与越界目标：
 *   - MeetingName   内容 0x3032 起，上限 16 汉字（32 字节）——越界目标 0x3044 录音状态；
 *   - MgmtCardName  内容 base+3 起，上限 24 汉字（48 字节）——卡① 越界目标 0x3361 会议类型、
 *                   卡④ 越界目标 0x3400 会议发起人（24 汉字对四张卡均安全）；
 *   - TaskName      内容 0x328D 起，上限 40 汉字（80 字节）——区间 0x328D~0x32B4 无邻接；
 *   - MgmtPageIndicator 0x3415 起占 16 字（0x3415~0x3424），与顶部按钮 0x3425 紧邻不重叠。
 * ────────────────────────────────────────────────────────────────────────
 */

namespace qifeng {
    namespace Addr {
        // 系统变量
        inline constexpr uint16_t PageSwitch = 0x0084;        // 页面切换（0x5A01 + 页面 ID）
        inline constexpr uint16_t CurrentPageAddr = 0x0014;   // 当前页面编号（读，校准主机侧记录）
        // 触控指令开关（0xB0 接口）：写 0x00B0 复现协议帧
        // 5A A5 0B 82 00B0 5AA5 0005 [seq 05] [0000 关 / 0001 开]
        // 注意：连续下发多条时每条需间隔 20ms（或先读 0xB0 为 0 再发下一条），
        //       故该地址的请求须置 Request::gapMs = 20，由引擎在帧间等待
        inline constexpr uint16_t IconTouchControl = 0x00B0;

        // ── 待机页（0x0001）──

        inline constexpr uint16_t AvailableTime = 0x3000;        // 可用时长（h，0~3000）
        inline constexpr uint16_t AvailableRatioLevel = 0x3001;  // 可用时长饼图等级（1~12，12 最小）
        inline constexpr uint16_t AvailableRatioText = 0x3050;   // 可用时长百分比（0~100）
        inline constexpr uint16_t SummaryCompletedCount = 0x3002;  // AI 纪要已生成数（长整型）
        inline constexpr uint16_t SummaryPendingCount = 0x3004;    // AI 纪要待生成数（长整型）
        inline constexpr uint16_t SummaryTotalCount = 0x3006;      // 总会议数（长整型）
        inline constexpr uint16_t SummaryRatioLevel = 0x3008;      // 会议饼图等级（1~22，22=均空）
        inline constexpr uint16_t TaskStatus = 0x3009;             // AI 任务卡片图标（1~5）
        inline constexpr uint16_t TaskName = 0x328A;                // 任务卡片会议名称（滚动文本，上限 40 汉字）
        inline constexpr uint16_t TaskRatioLevel = 0x3022;         // 任务进度条等级（1~13，13=不显示）
        inline constexpr uint16_t IdleTipStatus = 0x3023;          // 待机页提示图标（1~5）
        inline constexpr uint16_t DeviceIPAddress = 0x3144;        // WAN 口 IP 文本（最大 11 位）
        inline constexpr uint16_t DeviceWanStatus = 0x3217;        // 是否有 WAN 口 IP（1=无, 2=有）

        // ── 会议中页（0x0002）──

        inline constexpr uint16_t MeetingType = 0x3361;          // 会议类型卡片（1=制空, 2=公共, 3=私密）
        // 名称写入上限 16 汉字（控件层截断）——0x3032 起写，0x3044 为录音状态字段
        inline constexpr uint16_t MeetingName = 0x302F;          // 会议名称（滚动文本，SP=0x321B）
        inline constexpr uint16_t MeetingAudioStatus = 0x3044;   // 录音状态（1=暂停, 2=录制中, 3=无音频输入）
        inline constexpr uint16_t MeetingRatioLevel = 0x3045;    // 会议时长进度条（0~100）
        inline constexpr uint16_t MeetingClock = 0x3189;         // 会议录制时长（ASCII "00:00:00"）
        inline constexpr uint16_t MeetingEnergy = 0x3100;        // 音频能量波形（50 点，1~52，52=暂停）
        inline constexpr uint16_t MeetingOperationTip = 0x3155;  // 会议中操作提示窗（1~4）
        inline constexpr uint16_t MeetingSponsor = 0x3400;       // 会议发起人（最大 16 字）
        inline constexpr uint16_t MeetingStartTime = 0x3251;     // 会议开始时间（固定 ASCII 16 字节）
        inline constexpr uint16_t MeetingPauseIcon = 0x3309;     // 暂停/继续按键图标（1=暂停, 2=继续）

        // ── 指纹鉴权页（0x0005）──

        inline constexpr uint16_t FingerprintAuthTip = 0x3306;  // 指纹提示（1=无提示, 2=操作频繁）

        // ── 指纹录入页（0x0003，沿用）──

        inline constexpr uint16_t FingerprintRatioLevel = 0x316E;  // 指纹录入进度（1~7）
        inline constexpr uint16_t FingerprintTip = 0x3170;         // 指纹录入提示文本
        inline constexpr uint16_t FingerprintResult = 0x3215;      // 指纹录入结果（1=无图, 2=成功, 3=失败）

        // ── 升级页（0x0004，沿用）──

        inline constexpr uint16_t UpdateSystemTip = 0x320C;    // 显示屏升级标题
        inline constexpr uint16_t UpdateSystemTypes = 0x3216;  // 显示屏升级类型
        inline constexpr uint16_t UpgradeLoading = 0x3185;     // 升级 loading 动画（0=升级中, 1=完成）

        // ── 会议管理页（0x000B）──

        inline constexpr uint16_t MgmtFilterSummary = 0x3335;  // 会议类型选中：总结完成会议（1=选中, 2=未选中）
        inline constexpr uint16_t MgmtFilterOther = 0x3337;    // 会议类型选中：其他会议（1=选中, 2=未选中）
        inline constexpr uint16_t MgmtUdiskStatus = 0x3339;    // U 盘连接状态（1=有, 2=无）
        inline constexpr uint16_t MgmtUserName = 0x3536;       // 用户名称（上限 16 字）

        // 4 张会议卡片：每卡 状态(0~3) + 数据有无(1~2) + 名称(滚动文本，内容自 base+3 起)
        inline constexpr std::array<uint16_t, 4> MgmtCardStatus {0x333D, 0x3373, 0x33A9, 0x33DF};
        inline constexpr std::array<uint16_t, 4> MgmtCardPresence {0x333F, 0x3375, 0x33AB, 0x33E1};
        // ⚠ 名称写入上限 24 汉字（控件层截断）：卡① 邻 0x3361(会议类型)、卡④ 邻 0x3400(发起人)
        inline constexpr std::array<uint16_t, 4> MgmtCardName {0x3341, 0x3377, 0x33AD, 0x33E3};

        inline constexpr uint16_t MgmtPageIndicator = 0x3415;  // 列表页数文本（如 "1/2"、"0/0"，占 16 字）

        // ── 删除 / 下载 Loading 与结果页 ──

        inline constexpr uint16_t DeleteLoading = 0x34D3;         // 删除 Loading（0=开始, 1=停止）
        inline constexpr uint16_t DeleteResultImage = 0x34D5;     // 删除结果图（1=无, 2=成功, 3=失败）
        inline constexpr uint16_t DeleteResultTip = 0x34D7;       // 删除结果文字提示（36 字）
        inline constexpr uint16_t DownloadLoading = 0x34FF;       // 下载 Loading（0=开始, 1=停止）
        inline constexpr uint16_t DownloadResultImage = 0x3501;   // 下载结果图（1=无, 2=成功, 3=失败）
        inline constexpr uint16_t DownloadResultTip = 0x3503;     // 下载结果文字提示（36 字）

        // ── 固件升级地址 ──

        inline constexpr uint16_t FlashCacheBase = 0x8000;    // 升级数据缓存起始地址（每包 +0x78）
        inline constexpr uint16_t FlashTriggerAddr = 0x00AA;  // Flash 写入触发与完成状态查询
        inline constexpr uint16_t RebootAddr = 0x0004;        // 重启命令写入地址
        // DGUS 刷新/OS 核与触控总开关（【开发指南】5.1 节 0x00FC）：
        // 写 0x55AA 0x5A5A 停止 DGUS 刷新与触控处理，写 0x0000 0x0000 全部恢复。
        // 升级前必须停止（暂存区 0x8000~0xFFFF 与 SP 属性区重叠），
        // 业务侧"会议中临时锁屏"也复用该接口。
        inline constexpr uint16_t DgusStopEnable = 0x00FC;
    }  // namespace Addr

    /**
     * @brief 会议管理页触控图标序号（0xB0 指令载荷中的 seq 字节）
     *
     * 会议管理页共 8 个卡片图标：①③ 为卡片①的删除/下载，②⑤ 为卡片②的
     * 删除/下载，依此类推（左列为卡片编号，圈码为 DGUS 控件视图顺序）。
     * 协议约定：每次列表数据刷新都需重新下发各图标的开启/关闭状态
     * （"当数据为零时候功能关闭"，实现按钮置灰）。
     * 注：新协议已取消"全部删除"图标，故不再维护 AllDelete 序号。
     */
    namespace ListIcon {
        inline constexpr std::array<uint16_t, 4> CardDelete {0x03, 0x05, 0x07, 0x09};    // ③⑤⑦⑨ 卡片删除
        inline constexpr std::array<uint16_t, 4> CardDownload {0x04, 0x06, 0x08, 0x0A};  // ④⑥⑧⑩ 卡片下载
    }  // namespace ListIcon
}  // namespace qifeng

#endif  // HAL_SCREEN_PROTOCOL_ADDR_MAP_H
