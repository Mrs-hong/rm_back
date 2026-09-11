/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "common/logger.h"
#include "common/scope_exit.h"
#include "hal/screen/screen_page.h"

using namespace qifeng;
// 屏幕 HAL 业务唯一入口：页面显示/重置、控件更新、弹窗、触控注册
using qifeng::screen::ScreenPageManager;
// 会议管理列表组件：注册删除/下载回调、查询分页状态
using qifeng::screen::MeetingMgmtListWidget;
namespace fs = std::filesystem;

// 默认配置常量
constexpr const char* DEFAULT_SERIAL_PORT = "/dev/ttyS2";
constexpr uint32_t DEFAULT_SERIAL_BAUDRATE = 115200;
// 大变换间隔时间，便于实机观察每个状态
constexpr int STEP_INTERVAL_SECONDS = 3;

static void TestIdlePage(ScreenPageManager& hal);
static void TestMeetingPage(ScreenPageManager& hal);
static void TestIdleTaskName(ScreenPageManager& hal);
static void TestFingerprintPage(ScreenPageManager& hal);
static void TestFingerprintAuthPage(ScreenPageManager& hal);
static void TestUpgradePage(ScreenPageManager& hal);
static void TestFirmwareUpgrade(ScreenPageManager& hal);
static void TestMeetingMgmtPage(ScreenPageManager& hal);
static void TestTaskFlowPages(ScreenPageManager& hal);
static void TestPopWidget(ScreenPageManager& hal);
static void TestTouchRegister(ScreenPageManager& hal);
static void RunAllTests(ScreenPageManager& hal);

struct TestCase {
    int option = 0;
    const char* name = nullptr;
    const char* description = nullptr;
    std::function<void(ScreenPageManager&)> run;
};

// 辅助函数：打印结果 + 等待
static void WaitAndCheck(ScreenResult result, const std::string& desc) {
    std::cout << desc;
    if (result == ScreenResult::OK) {
        std::cout << " [OK]\n";
    } else {
        std::cout << " [FAIL] code=" << static_cast<int>(result) << "\n";
    }
    std::this_thread::sleep_for(std::chrono::seconds(STEP_INTERVAL_SECONDS));
}

static void WaitForMenuInput() {
    std::cout << "\n按回车键返回功能菜单...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

// 辅助函数：打印使用说明
static void PrintUsage(const char* exeName) {
    std::cout << "用法: " << exeName << " [--port <串口路径>] [--baud <波特率>]\n";
    std::cout << "默认串口: " << DEFAULT_SERIAL_PORT << "\n";
    std::cout << "默认波特率: " << DEFAULT_SERIAL_BAUDRATE << "\n";
    std::cout << "启动后通过数字菜单选择要测试的模块或页面功能\n";
}

static std::vector<TestCase> BuildTestCases() {
    return {
        {1, "待机页面", "可用时长/纪要/任务卡片、顶部 IP/提示", TestIdlePage},
        {2, "会议页面", "会议中页面的基础信息、提示、波形和进度", TestMeetingPage},
        {3, "指纹录入页", "指纹录入进度和成功/失败结果", TestFingerprintPage},
        {4, "指纹鉴权页", "指纹鉴权提示（弹窗）与失败自动恢复", TestFingerprintAuthPage},
        {5, "任务名称专项", "滚动文本名称编码（VP+3）与进度条等级切换", TestIdleTaskName},
        {6, "升级页面", "升级页面的进行中、成功、失败状态", TestUpgradePage},
        {7, "固件升级", "传入目录路径，升级该目录下所有固件文件", TestFirmwareUpgrade},
        {8, "会议管理页", "全量数据分页渲染、筛选、U盘/用户名、删除/下载回调注册", TestMeetingMgmtPage},
        {9, "删除/下载流程页", "确认页、Loading 页、结果页的状态切换", TestTaskFlowPages},
        {10, "弹窗自动恢复", "ShowPopWidget 自动恢复、叠加弹窗、手动关闭", TestPopWidget},
        {11, "触控注册", "归属校验、控件接管拒绝、回调内调用 Manager 接口", TestTouchRegister},
        {12, "全部测试", "顺序执行全部模块和页面测试", RunAllTests},
    };
}

static const TestCase* FindTestCase(const std::vector<TestCase>& tests, int option) {
    for (const auto& test : tests) {
        if (test.option == option) {
            return &test;
        }
    }
    return nullptr;
}

static void PrintTestMenu(const std::vector<TestCase>& tests) {
    std::cout << "\n===== 功能测试菜单 =====\n";
    for (const auto& test : tests) {
        std::cout << "  " << test.option << ". " << test.name << " - " << test.description << "\n";
    }
    std::cout << "  0. 退出\n";
}

static void RunInteractiveMenu(ScreenPageManager& hal) {
    const auto tests = BuildTestCases();
    while (true) {
        PrintTestMenu(tests);
        std::cout << "请输入功能编号: ";

        int option = -1;
        if (!(std::cin >> option)) {
            // 输入失败后需要清空状态，否则后续菜单会持续读失败
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "输入无效，请输入数字编号\n";
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        if (option == 0) {
            break;
        }

        const TestCase* test = FindTestCase(tests, option);
        if (test == nullptr) {
            std::cout << "未找到对应功能，请重新选择\n";
            continue;
        }

        std::cout << "\n开始执行: " << test->name << "\n";
        test->run(hal);
        WaitAndCheck(hal.ShowPage(DisplayPage::Idle, std::nullopt, 2000), "测试完成后切换回待机页面");
        WaitForMenuInput();
    }
}

// 待机页面：卡片数据 + 任务状态流 + 设备状态提示恢复 + WIFI/IP切换
static void TestIdlePage(ScreenPageManager& hal) {
    std::cout << "\n===== 待机页面测试 =====\n";
    WaitAndCheck(hal.ShowPage(DisplayPage::Idle, std::nullopt, 2000), "切换到待机页面");

    std::cout << "更新可用时长卡片: 高可用，应显示较高百分比和较大水球\n";
    IdleAvailabilityData avail;
    avail.availableTime = 2400;
    avail.ratioLevel = 2;
    avail.ratioValue = 80;
    WaitAndCheck(hal.UpdateWidget(avail, 2000), "可用时长(高可用) -> 应显示 80% 文本");

    std::cout << "更新可用时长卡片: 低可用，应显示较低百分比和较小水球\n";
    avail.availableTime = 300;
    avail.ratioLevel = 10;
    avail.ratioValue = 30;
    WaitAndCheck(hal.UpdateWidget(avail, 2000), "可用时长(低可用) -> 应显示 30% 文本");

    std::cout << "更新纪要卡片: 全零态，应显示空态比例图\n";
    IdleSummaryData summary;
    summary.totalCount = 0;
    summary.pendingCount = 0;
    summary.completedCount = 0;
    summary.ratioLevel = 22;
    WaitAndCheck(hal.UpdateWidget(summary, 2000), "纪要(全零态) -> 应显示空态比例图");

    std::cout << "更新纪要卡片: 常规态，应显示已生成/待生成统计\n";
    summary.totalCount = 100;
    summary.pendingCount = 20;
    summary.completedCount = 80;
    summary.ratioLevel = 8;
    WaitAndCheck(hal.UpdateWidget(summary, 2000), "纪要(80/20)");

    std::cout << "更新任务卡片: 状态1，应清空名称并隐藏进度条\n";
    IdleTaskData task;
    task.status = 1;
    task.name.clear();
    task.ratioLevel = 13;  // 13 = 不显示进度条（显示背景色）
    WaitAndCheck(hal.UpdateWidget(task, 2000), "任务(状态1) -> 应只显示状态图并清空名称");

    std::cout << "更新任务卡片: 状态2，应仅显示名称与进度条\n";
    task.status = 2;
    task.name = "产品周会";
    task.ratioLevel = 3;
    WaitAndCheck(hal.UpdateWidget(task, 2000), "任务(状态2) -> 应仅显示任务名称");

    std::cout << "更新任务卡片: 状态3，转写中，进度条递增\n";
    task.status = 3;
    task.ratioLevel = 5;
    WaitAndCheck(hal.UpdateWidget(task, 2000), "任务(状态3) -> 应显示转写中状态");

    std::cout << "更新任务卡片: 状态4，待总结\n";
    task.status = 4;
    task.ratioLevel = 8;
    WaitAndCheck(hal.UpdateWidget(task, 2000), "任务(状态4) -> 应显示待总结状态");

    std::cout << "更新任务卡片: 状态5，总结中\n";
    task.status = 5;
    task.ratioLevel = 10;
    WaitAndCheck(hal.UpdateWidget(task, 2000), "任务(状态5) -> 应显示总结中状态");

    std::cout << "更新任务卡片: 同状态清空名称，验证旧名称被清空\n";
    task.name.clear();
    WaitAndCheck(hal.UpdateWidget(task, 2000), "任务(状态5清空名称) -> 应清空名称");

    std::cout << "更新任务卡片: 从高展示态回到状态1，验证名称与进度条一并复位\n";
    task.status = 1;
    task.ratioLevel = 13;
    WaitAndCheck(hal.UpdateWidget(task, 2000), "任务(回到状态1) -> 应清空残留名称与进度条");

    std::cout << "更新待机页提示: 默认无图\n";
    IdleStatusData status;
    status.status = 1;
    WaitAndCheck(hal.UpdateWidget(status, 2000), "提示(1) -> 默认无图");

    std::cout << "更新待机页提示: 可用时长不足提示，随后恢复默认态\n";
    status.status = 2;
    WaitAndCheck(hal.UpdateWidget(status, 2000), "提示(2) -> 应显示可用时长不足提示");
    status.status = 1;
    WaitAndCheck(hal.UpdateWidget(status, 2000), "提示(恢复1) -> 提示后应恢复默认态");

    std::cout << "更新待机页提示: 麦克风异常提示，随后恢复默认态\n";
    status.status = 3;
    WaitAndCheck(hal.UpdateWidget(status, 2000), "提示(3) -> 应显示麦克风异常提示");
    status.status = 1;
    WaitAndCheck(hal.UpdateWidget(status, 2000), "提示(恢复1) -> 提示后应恢复默认态");

    std::cout << "更新待机页提示: 会议结束成功，随后恢复默认态\n";
    status.status = 4;
    WaitAndCheck(hal.UpdateWidget(status, 2000), "提示(4) -> 应显示会议结束成功提示");
    status.status = 1;
    WaitAndCheck(hal.UpdateWidget(status, 2000), "提示(恢复1) -> 提示后应恢复默认态");

    std::cout << "更新待机页提示: 会议时长已满自动结束\n";
    status.status = 5;
    WaitAndCheck(hal.UpdateWidget(status, 2000), "提示(5) -> 应显示会议时长已满提示");
    status.status = 1;
    WaitAndCheck(hal.UpdateWidget(status, 2000), "提示(恢复1) -> 提示后应恢复默认态");

    std::cout << "更新顶部信息: 有 WAN 口，应显示 IP\n";
    DeviceInfoData devInfo;
    devInfo.wanStatus = 2;
    devInfo.ipAddress = "192.168.112.47";
    WaitAndCheck(hal.UpdateWidget(devInfo, 2000), "顶部(有 WAN 口) -> 应显示 IP 文本");

    std::cout << "更新顶部信息: 无 WAN 口，应隐藏 IP 图标并刷新文本\n";
    devInfo.wanStatus = 1;
    devInfo.ipAddress = "192.168.112.48";
    WaitAndCheck(hal.UpdateWidget(devInfo, 2000), "顶部(无 WAN 口) -> 应隐藏 IP 图标");
}

// 会议中页面：基本信息 + 会议类型提示恢复 + 操作提示恢复 + 音频状态 + 时长 + 波形 + 进度条
static void TestMeetingPage(ScreenPageManager& hal) {
    std::cout << "\n===== 会议中页面测试 =====\n";
    WaitAndCheck(hal.ShowPage(DisplayPage::Meeting, std::nullopt, 2000), "切换到会议中页面");

    std::cout << "更新会议基本信息: 应显示发起人与开始时间\n";
    MeetingBasicData basic;
    basic.sponsor = "张三";
    basic.startTime = "2026/07/09/14:30";
    WaitAndCheck(hal.UpdateWidget(basic, 2000), "会议基本信息 -> 应显示发起人与开始时间");

    MeetingInfoData info;
    info.name = "产品周会";
    info.operationTip = 1;
    info.audioStatus = 2;
    info.ratioLevel = 50;

    std::cout << "更新会议类型提示: 公共会议，随后恢复无图\n";
    info.type = 2;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "会议类型(公共) -> 应显示公共会议提示");
    info.type = 1;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "会议类型(恢复1) -> 提示后应恢复无图");

    std::cout << "更新会议类型提示: 私密会议，随后恢复无图\n";
    info.type = 3;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "会议类型(私密) -> 应显示私密会议提示");
    info.type = 1;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "会议类型(恢复1) -> 提示后应恢复无图");

    std::cout << "更新会议信息: 稳定录制中状态\n";
    info.type = 1;
    info.operationTip = 1;
    info.audioStatus = 2;
    info.ratioLevel = 50;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "会议信息(稳定态) -> 应显示录制中且无提示窗");

    std::cout << "更新会议操作提示: 无权限结束会议，随后恢复无提示\n";
    info.operationTip = 2;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "操作提示(2) -> 应显示无权限结束会议");
    info.operationTip = 1;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "操作提示(恢复1) -> 应恢复无提示窗");

    std::cout << "更新会议操作提示: 是否结束当前会议，随后恢复无提示\n";
    info.operationTip = 3;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "操作提示(3) -> 应显示是否结束当前会议");
    info.operationTip = 1;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "操作提示(恢复1) -> 应恢复无提示窗");

    std::cout << "更新会议操作提示: 多次无权限结束会议，随后恢复无提示\n";
    info.operationTip = 4;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "操作提示(4) -> 应显示频繁操作提示");
    info.operationTip = 1;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "操作提示(恢复1) -> 应恢复无提示窗");

    std::cout << "更新音频状态: 暂停 -> 录制中 -> 无音频输入\n";
    info.audioStatus = 1;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "音频状态(1) -> 应显示暂停");
    info.audioStatus = 2;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "音频状态(2) -> 应显示录制中");
    info.audioStatus = 3;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "音频状态(3) -> 应显示无音频输入");
    info.audioStatus = 2;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "音频状态(恢复2) -> 应恢复录制中");

    std::cout << "更新暂停/继续按键图标: 暂停 -> 继续\n";
    info.pauseIcon = 1;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "暂停按键(1) -> 应显示暂停会议按键");
    info.pauseIcon = 2;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "暂停按键(2) -> 应切换为继续会议按键");
    info.pauseIcon = 1;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "暂停按键(恢复1) -> 应恢复暂停会议按键");

    std::cout << "更新会议时长: 应显示 HH:MM:SS 递增\n";
    MeetingClockData clock;
    clock.hour = 0;
    clock.minute = 0;
    clock.second = 0;
    WaitAndCheck(hal.UpdateWidget(clock, 2000), "会议时长(00:00:00) -> 应显示 00:00:00");

    clock.second = 3;
    WaitAndCheck(hal.UpdateWidget(clock, 2000), "会议时长(00:00:03) -> 应显示 00:00:03");

    clock.second = 6;
    WaitAndCheck(hal.UpdateWidget(clock, 2000), "会议时长(00:00:06) -> 应显示 00:00:06");

    std::cout << "更新音频数据: 低幅波形\n";
    MeetingEnergyData energy;
    energy.energyValues = {1, 2, 3,  4, 5, 6, 7, 8, 9, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 2, 3,  4, 5, 6, 7,
                           8, 9, 10, 9, 8, 7, 6, 5, 4, 3,  2, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 9, 8, 7, 6};
    WaitAndCheck(hal.UpdateWidget(energy, 2000), "音频数据(低幅) -> 应显示低幅波形");

    std::cout << "更新音频数据: 高幅波形，包含边界值\n";
    energy.energyValues = {1,  5,  10, 20, 30, 40, 51, 52, 51, 40, 30, 20, 10, 5,  1,  5,  10,
                           20, 30, 40, 51, 52, 51, 40, 30, 20, 10, 5,  1,  5,  10, 20, 30, 40,
                           51, 52, 51, 40, 30, 20, 10, 5,  1,  5,  10, 20, 30, 40, 51, 52};
    WaitAndCheck(hal.UpdateWidget(energy, 2000), "音频数据(高幅) -> 应显示高幅波形与边界值");

    std::cout << "更新会议进度条: 0% -> 30% -> 100%\n";
    info.ratioLevel = 0;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "进度条(0%) -> 应显示起始进度");
    info.ratioLevel = 30;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "进度条(30%) -> 应显示中间进度");
    info.ratioLevel = 100;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "进度条(100%) -> 应显示满进度");
}

// 任务名称编码专项测试（滚动文本）
// 验证：滚动文本内容写入 VP+3（协议约定，前 3 字为控件控制头）；
//       GBK 文本追加 0xFFFF 结束符（屏幕遇 FFFF 停止渲染、不纳入地址计数），
//       缩短或清空时无残留乱码；进度条等级 1~13（13 = 不显示进度条）
static void TestIdleTaskName(ScreenPageManager& hal) {
    std::cout << "\n===== 任务名称（滚动文本）编码测试 =====\n";
    WaitAndCheck(hal.ShowPage(DisplayPage::Idle, std::nullopt, 2000), "切换到待机页面");

    IdleTaskData task;
    task.status = 3;
    task.ratioLevel = 5;

    // 长名称：接近 40 汉字上限，验证按 GBK 字节数截断且不截半个字符
    task.name = "测试会议";
    WaitAndCheck(hal.UpdateWidget(task, 2000), "名称(短) -> 应显示 测试会议");

    task.name = "二〇二六年一月一日产品周会会议记录";
    WaitAndCheck(hal.UpdateWidget(task, 2000), "名称(长) -> 应完整显示，不截半个汉字");

    task.name = "产品评审会";
    WaitAndCheck(hal.UpdateWidget(task, 2000), "名称(缩短) -> 应无上次长名称的残留乱码");

    task.name = "Test_Meeting_2026";
    WaitAndCheck(hal.UpdateWidget(task, 2000), "名称(含下划线) -> 下划线应替换为连字符");

    task.name.clear();
    WaitAndCheck(hal.UpdateWidget(task, 2000), "名称(清空) -> 应写 0xFFFF 清空槽位、无残留");

    // 进度条等级：1~12 递增，13 = 显示背景色（不显示进度条）
    task.name = "产品周会";
    task.ratioLevel = 1;
    WaitAndCheck(hal.UpdateWidget(task, 2000), "进度条(1) -> 应为最小进度");
    task.ratioLevel = 12;
    WaitAndCheck(hal.UpdateWidget(task, 2000), "进度条(12) -> 应为最大进度");
    task.ratioLevel = 13;
    WaitAndCheck(hal.UpdateWidget(task, 2000), "进度条(13) -> 应隐藏进度条（显示背景色）");

    // 状态切换时强制重发名称与进度条：回到状态1应清空名称、进度条复位
    task.status = 1;
    task.name.clear();
    task.ratioLevel = 13;
    WaitAndCheck(hal.UpdateWidget(task, 2000), "状态(回到1) -> 名称与进度条应一并复位");
}

// 指纹鉴权页：鉴权提示（弹窗）与失败后自动恢复
static void TestFingerprintAuthPage(ScreenPageManager& hal) {
    std::cout << "\n===== 指纹鉴权页测试 =====\n";
    WaitAndCheck(hal.ShowPage(DisplayPage::FingerprintAuth, std::nullopt, 2000), "切换到指纹鉴权页");

    std::cout << "鉴权基态: 无提示\n";
    FingerprintAuthData auth;
    auth.tip = 1;
    WaitAndCheck(hal.UpdateWidget(auth, 2000), "鉴权提示(1) -> 应无提示");

    std::cout << "鉴权频繁提示: 使用默认显示时长（3s）后自动恢复\n";
    auth.tip = 2;
    WaitAndCheck(hal.ShowPopWidget(auth), "鉴权提示(2) -> 应按默认 3s 自动恢复（kDefaultPopResetMs）");

    std::cout << "鉴权频繁提示: 指定 2 秒后自动恢复\n";
    WaitAndCheck(hal.ShowPopWidget(auth, 2000, 2000), "鉴权提示(2) -> 应显示约 2 秒后自动恢复");

    std::cout << "鉴权频繁提示: 手动关闭\n";
    WaitAndCheck(hal.ShowPopWidget(auth, 0, 2000), "鉴权提示(手动弹窗) -> 不应自动消失");
    WaitAndCheck(hal.ClosePopWidget(WidgetType::FingerprintAuth, 2000), "ClosePopWidget -> 应恢复到无提示");
}

// 指纹页面：录入进度 + 结果提示恢复正常态
static void TestFingerprintPage(ScreenPageManager& hal) {
    std::cout << "\n===== 指纹录入页面测试 =====\n";
    WaitAndCheck(hal.ShowPage(DisplayPage::Fingerprint, std::nullopt, 2000), "切换到指纹录入页面");

    FingerprintData fp;
    fp.result = 1;

    std::cout << "更新指纹进度(1/7)\n";
    fp.ratioLevel = 1;
    fp.tip = "请用同一手指按压指纹识别区";
    WaitAndCheck(hal.UpdateWidget(fp, 2000), "指纹进度1 -> 应显示第一步提示");

    std::cout << "更新指纹进度(2/7)\n";
    fp.ratioLevel = 2;
    fp.tip = "请抬起手指";
    WaitAndCheck(hal.UpdateWidget(fp, 2000), "指纹进度2 -> 应显示抬起手指");

    std::cout << "更新指纹进度(3/7)\n";
    fp.ratioLevel = 3;
    fp.tip = "请再次按压";
    WaitAndCheck(hal.UpdateWidget(fp, 2000), "指纹进度3 -> 应显示再次按压");

    std::cout << "更新指纹进度(4/7)\n";
    fp.ratioLevel = 4;
    fp.tip = "请抬起手指";
    WaitAndCheck(hal.UpdateWidget(fp, 2000), "指纹进度4 -> 应显示抬起手指");

    std::cout << "更新指纹进度(5/7)\n";
    fp.ratioLevel = 5;
    fp.tip = "请继续按压";
    WaitAndCheck(hal.UpdateWidget(fp, 2000), "指纹进度5 -> 应显示继续按压");

    std::cout << "更新指纹进度(6/7)\n";
    fp.ratioLevel = 6;
    fp.tip = "请抬起手指";
    WaitAndCheck(hal.UpdateWidget(fp, 2000), "指纹进度6 -> 应显示抬起手指");

    std::cout << "更新指纹进度(7/7)\n";
    fp.ratioLevel = 7;
    fp.tip = "采集完成";
    WaitAndCheck(hal.UpdateWidget(fp, 2000), "指纹进度7 -> 应显示采集完成");

    std::cout << "更新指纹结果: 成功提示，随后恢复正常态\n";
    fp.result = 2;
    WaitAndCheck(hal.UpdateWidget(fp, 2000), "指纹结果(成功) -> 应显示成功提示");
    fp.result = 1;
    WaitAndCheck(hal.UpdateWidget(fp, 2000), "指纹结果(恢复1) -> 成功提示后应恢复正常态");

    std::cout << "更新指纹结果: 失败提示，随后恢复正常态\n";
    fp.result = 3;
    fp.tip = "录入失败，请重试";
    WaitAndCheck(hal.UpdateWidget(fp, 2000), "指纹结果(失败) -> 应显示失败提示");
    fp.result = 1;
    fp.tip = "请用同一手指按压指纹识别区";
    WaitAndCheck(hal.UpdateWidget(fp, 2000), "指纹结果(恢复1) -> 失败提示后应恢复正常态");
}

static void TestUpgradePage(ScreenPageManager& hal) {
    std::cout << "\n===== 升级页面测试 =====\n";
    WaitAndCheck(hal.ShowPage(DisplayPage::Upgrade, std::nullopt, 2000), "切换到升级页面");

    UpgradeData upgrade;

    std::cout << "更新升级状态: 升级进行中\n";
    upgrade.title = 1;
    upgrade.types = 1;
    upgrade.loading = 0;
    WaitAndCheck(hal.UpdateWidget(upgrade, 2000),
                 "升级状态(进行中) -> 应显示升级中和 loading 动画");

    std::cout << "更新升级状态: 升级成功\n";
    upgrade.title = 2;
    upgrade.types = 2;
    upgrade.loading = 1;
    WaitAndCheck(hal.UpdateWidget(upgrade, 2000), "升级状态(成功) -> 应显示成功结果");

    std::cout << "更新升级状态: 升级失败\n";
    upgrade.title = 3;
    upgrade.types = 3;
    upgrade.loading = 1;
    WaitAndCheck(hal.UpdateWidget(upgrade, 2000), "升级状态(失败) -> 应显示失败结果");
}

static void TestFirmwareUpgrade(ScreenPageManager& hal) {
    std::cout << "\n===== 固件升级测试 =====\n";

    std::cout << "请输入固件所在目录路径: ";
    std::string dirPath;
    if (!std::getline(std::cin, dirPath) || dirPath.empty()) {
        std::cout << "未输入目录路径，跳过升级\n";
        return;
    }

    std::error_code ec;
    if (!fs::is_directory(dirPath, ec)) {
        std::cout << "目录不存在或不是目录: " << dirPath << "\n";
        return;
    }

    // 收集目录下的固件文件，排除子目录
    std::vector<std::string> filePaths;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (entry.is_regular_file()) {
            filePaths.push_back(entry.path().string());
        }
    }
    if (filePaths.empty()) {
        std::cout << "目录下无固件文件: " << dirPath << "\n";
        return;
    }

    std::cout << "发现 " << filePaths.size() << " 个文件:\n";
    for (const auto& p : filePaths) {
        std::cout << "  " << p << "\n";
    }

    WaitAndCheck(hal.ShowPage(DisplayPage::Upgrade, std::nullopt, 2000), "切换到升级页面");

    UpgradeData upgrade;
    upgrade.title = 1;
    upgrade.types = 1;
    upgrade.loading = 0;
    WaitAndCheck(hal.UpdateWidget(upgrade, 2000), "显示升级进行中");

    std::cout << "开始升级，请勿断电...\n";
    auto result = hal.UpgradeFirmware(filePaths, 3000);
    if (result == ScreenResult::OK) {
        std::cout << "固件升级 [OK]\n";
        upgrade.title = 2;
        upgrade.types = 2;
        upgrade.loading = 1;
        WaitAndCheck(hal.UpdateWidget(upgrade, 2000), "显示升级成功");
    } else {
        std::cout << "固件升级 [FAIL] code=" << static_cast<int>(result) << "\n";
        upgrade.title = 3;
        upgrade.types = 3;
        upgrade.loading = 1;
        WaitAndCheck(hal.UpdateWidget(upgrade, 2000), "显示升级失败");
    }
}

// 会议管理页：全量数据分页渲染 + 筛选 + U盘/用户名 + 删除/下载业务回调注册 + 分页状态查询
// 说明：删除③⑤⑦⑨/下载④⑥⑧⑩/翻页按钮由 MeetingMgmtListWidget 内部接管（不走 RegisterClick）；
//       筛选（总结完成/其他）语义为"业务换数据源"，由业务 RegisterClick 注册后换数据；
//       程序侧验证数据渲染与回调注册，实际点按请在屏幕上进行
static void TestMeetingMgmtPage(ScreenPageManager& hal) {
    std::cout << "\n===== 会议管理页测试 =====\n";
    WaitAndCheck(hal.ShowPage(DisplayPage::MeetingMgmt, std::nullopt, 2000), "切换到会议管理页");

    // 组件访问：注册删除/下载业务回调（真机点按图标时触发，回调内可调 Manager 接口）
    auto* mgmt = hal.MeetingMgmtWidget();
    if (mgmt == nullptr) {
        std::cout << "[FAIL] 会议管理列表组件不可用\n";
        return;
    }
    mgmt->SetDeleteHandler([](size_t index, const MeetingMgmtItem& item) {
        std::cout << "  [回调] 删除会议 index=" << index << " status=" << item.status << " name=" << item.name
                  << "（屏端已跳转删除二次确认页）\n";
    });
    mgmt->SetDownloadHandler([](size_t index, const MeetingMgmtItem& item) {
        std::cout << "  [回调] 下载会议 index=" << index << " status=" << item.status << " name=" << item.name
                  << "（屏端已跳转下载二次确认页）\n";
    });

    // 0) 页头信息：U 盘状态 + 用户名
    MeetingMgmtInfoData info;
    info.udiskStatus = 1;
    info.userName = "旗一峰";
    WaitAndCheck(hal.UpdateWidget(info, 2000), "页头 -> 应显示有 U 盘与用户名");

    // 1) 空列表：页码指示应为 0/0，4 张卡片空态（状态=0、有无=2）、图标触控全部关闭
    MeetingMgmtListData empty;
    empty.filter = 1;  // 总结完成会议
    WaitAndCheck(hal.UpdateWidget(empty, 2000), "空列表 -> 页码指示应显示 0/0");

    // 2) 9 条会议 = 3 页：应渲染第 1 页 4 张卡片、页码 1/3，图标按卡片有无开启
    MeetingMgmtListData list;
    list.filter = 1;
    for (int i = 1; i <= 9; ++i) {
        MeetingMgmtItem item;
        item.status = static_cast<uint16_t>(i % 4);  // 0=置空 1=待处理 2=处理中 3=异常
        item.name = "测试会议" + std::to_string(i);
        list.items.push_back(item);
    }
    WaitAndCheck(hal.UpdateWidget(list, 2000), "9 条会议 -> 应显示第 1/3 页（4 张卡片 + 页码 1/3）");
    std::cout << "  分页状态: 当前页=" << mgmt->CurrentPage() << " 总页数=" << mgmt->PageCount() << "\n";

    // 3) 数据减少（5 条 = 2 页）：保持当前页（当前在第 1 页 → 仍 1/2）；
    //    若当前页已超出新页数则夹紧到最后一页（需在屏上先翻到末页再删除才能观察到）
    list.items.resize(5);
    WaitAndCheck(hal.UpdateWidget(list, 2000), "缩为 5 条 -> 保持当前页（页码 1/2）");
    std::cout << "  分页状态: 当前页=" << mgmt->CurrentPage() << " 总页数=" << mgmt->PageCount() << "\n";

    // 3b) 数据减少到 1~3 条：空槽卡片应随之显示为"无会议"，对应图标触控关闭
    list.items.resize(3);
    WaitAndCheck(hal.UpdateWidget(list, 2000), "缩为 3 条 -> 第 4 张卡片应为无会议、其图标置灰");

    // 3c) 清空数据：4 张卡片全为无会议，页码指示应显示 0/0
    list.items.clear();
    WaitAndCheck(hal.UpdateWidget(list, 2000), "空数据 -> 4 张卡片均无会议 + 页码 0/0");

    // 恢复 5 条继续后续用例
    for (int i = 1; i <= 5; ++i) {
        MeetingMgmtItem item;
        item.status = 1;
        item.name = "测试会议" + std::to_string(i);
        list.items.push_back(item);
    }
    WaitAndCheck(hal.UpdateWidget(list, 2000), "恢复 5 条 -> 页码 1/2");

    // 4) 会议状态遍历：置空/待处理/处理中/异常
    for (uint16_t st = 0; st <= 3; ++st) {
        list.items[0].status = st;
        WaitAndCheck(hal.UpdateWidget(list, 2000),
                     "卡片①状态(" + std::to_string(st) + ") -> 应切换会议状态图标");
    }

    // 5) 筛选切换：选中图标随之切换（数据源由业务侧更换）
    list.filter = 2;
    WaitAndCheck(hal.UpdateWidget(list, 2000), "筛选=其他会议 -> 选中图标应切换");
    list.filter = 1;
    WaitAndCheck(hal.UpdateWidget(list, 2000), "筛选=总结完成 -> 选中图标应切回");

    // 6) U 盘状态：无 U 盘提示
    info.udiskStatus = 2;
    WaitAndCheck(hal.UpdateWidget(info, 2000), "无 U 盘 -> 应显示无 U 盘状态");

    std::cout << "提示: 请点按卡片③⑤⑦⑨/④⑥⑧⑩（删除/下载）与底部翻页，验证上方 [回调] 输出\n";
}

// 删除/下载流程页：二次确认页（纯触控）+ Loading 页 + 结果页
static void TestTaskFlowPages(ScreenPageManager& hal) {
    std::cout << "\n===== 删除/下载流程页测试 =====\n";
    std::cout << "说明：二次确认页与选择页无显示变量，仅验证切页（按钮由业务 RegisterClick 注册）\n";

    // 纯触控页：切页验证
    WaitAndCheck(hal.ShowPage(DisplayPage::DownloadConfirmOther, std::nullopt, 2000), "切换到下载确认页(其他会议)");
    WaitAndCheck(hal.ShowPage(DisplayPage::DownloadConfirmSummary, std::nullopt, 2000),
                 "切换到下载确认页(总结完成)");
    WaitAndCheck(hal.ShowPage(DisplayPage::DeleteConfirm, std::nullopt, 2000), "切换到删除确认页");
    WaitAndCheck(hal.ShowPage(DisplayPage::MeetingTypeSelect, std::nullopt, 2000), "切换到会议数据类型选择页");
    WaitAndCheck(hal.ShowPage(DisplayPage::MeetingEndConfirm, std::nullopt, 2000), "切换到结束会议二次确认页");

    // 下载 Loading → 完成
    WaitAndCheck(hal.ShowPage(DisplayPage::DownloadLoading, std::nullopt, 2000), "切换到下载 Loading 页");
    DownloadLoadingData dl;
    dl.loading = 0;
    WaitAndCheck(hal.UpdateWidget(dl, 2000), "下载中(0) -> 应显示下载 loading 动画");
    dl.loading = 1;
    WaitAndCheck(hal.UpdateWidget(dl, 2000), "下载完成(1) -> 应停止 loading");

    // 下载结果页：成功 / 失败
    WaitAndCheck(hal.ShowPage(DisplayPage::DownloadResult, std::nullopt, 2000), "切换到下载结果页");
    DownloadResultData dr;
    dr.result = 2;
    dr.tip = "下载完成";
    WaitAndCheck(hal.UpdateWidget(dr, 2000), "下载结果(成功) -> 应显示成功结果图与提示");
    dr.result = 3;
    dr.tip = "下载失败，请检查 U 盘空间";
    WaitAndCheck(hal.UpdateWidget(dr, 2000), "下载结果(失败) -> 应显示失败结果图与提示");

    // 删除 Loading → 完成
    WaitAndCheck(hal.ShowPage(DisplayPage::DeleteLoading, std::nullopt, 2000), "切换到删除 Loading 页");
    DeleteLoadingData del;
    del.loading = 0;
    WaitAndCheck(hal.UpdateWidget(del, 2000), "删除中(0) -> 应显示删除 loading 动画");
    del.loading = 1;
    WaitAndCheck(hal.UpdateWidget(del, 2000), "删除完成(1) -> 应停止 loading");

    // 删除结果页：成功 / 失败
    WaitAndCheck(hal.ShowPage(DisplayPage::DeleteResult, std::nullopt, 2000), "切换到删除结果页");
    DeleteResultData de;
    de.result = 2;
    de.tip = "删除完成";
    WaitAndCheck(hal.UpdateWidget(de, 2000), "删除结果(成功) -> 应显示成功结果图与提示");
    de.result = 3;
    de.tip = "删除失败，请重试";
    WaitAndCheck(hal.UpdateWidget(de, 2000), "删除结果(失败) -> 应显示失败结果图与提示");
}

// 弹窗控件：自动恢复、叠加弹窗恢复基准、手动关闭（幂等）
static void TestPopWidget(ScreenPageManager& hal) {
    std::cout << "\n===== 弹窗（自动恢复）测试 =====\n";
    WaitAndCheck(hal.ShowPage(DisplayPage::Meeting, std::nullopt, 2000), "切换到会议中页面");

    // 基态：operationTip=1 表示无提示窗，作为后续弹窗的恢复基准
    MeetingInfoData base;
    base.name = "产品周会";
    base.type = 1;
    base.operationTip = 1;
    base.audioStatus = 2;
    base.ratioLevel = 50;
    WaitAndCheck(hal.UpdateWidget(base, 2000), "基态 -> operationTip=1（无提示窗）");

    // 1) 自动恢复：显示约 2 秒后自动回到基态
    MeetingInfoData tip = base;
    tip.operationTip = 3;  // 是否结束当前会议
    WaitAndCheck(hal.ShowPopWidget(tip, 2000, 2000), "操作提示(3) -> 应显示约 2 秒后自动消失");

    // 2) 叠加弹窗：第二次弹窗顶掉第一次，超时应回到「最初的弹窗基态」而不是上一次弹窗内容
    tip.operationTip = 2;  // 无权限结束会议
    WaitAndCheck(hal.ShowPopWidget(tip, 6000, 2000), "操作提示(2) -> 显示 6 秒（下一步会被提示(4)顶掉）");
    tip.operationTip = 4;  // 操作频繁
    WaitAndCheck(hal.ShowPopWidget(tip, 2000, 2000), "提示(4) 顶掉提示(2) -> 应显示约 2 秒后回到基态(1)");

    // 3) 手动弹窗：autoResetMs=0 不自动恢复，需 ClosePopWidget；重复关闭应幂等
    tip.operationTip = 3;
    WaitAndCheck(hal.ShowPopWidget(tip, 0, 2000), "手动弹窗(3) -> 不应自动消失，等待手动关闭");
    WaitAndCheck(hal.ClosePopWidget(WidgetType::MeetingInfo, 2000), "ClosePopWidget -> 应恢复到基态(1)");
    WaitAndCheck(hal.ClosePopWidget(WidgetType::MeetingInfo, 2000), "重复关闭 -> 幂等返回 OK（不发帧）");
}

// 触控注册：页面按钮注册/注销、归属校验、控件接管按钮拒绝、回调内调用 Manager 接口
static void TestTouchRegister(ScreenPageManager& hal) {
    std::cout << "\n===== 触控注册测试 =====\n";
    WaitAndCheck(hal.ShowPage(DisplayPage::Idle, std::nullopt, 2000), "切换到待机页面");

    // 1) 页面按钮：待机页【公共会议】归属待机页，注册成功。
    //    回调在执行器线程执行，内部可直接调用 Manager 同步接口（不会与 mOpMutex 自死锁）
    uint64_t openMeeting = hal.RegisterClick(DisplayPage::Idle, ScreenButton::PublicMeeting,
                                             [&hal](const TouchEvent& event) {
                                                 std::cout << "  [触控] 公共会议 keyVp=0x" << std::hex << event.keyVp
                                                           << std::dec << " -> 进入会议中页\n";
                                                 auto ret = hal.ShowPage(DisplayPage::Meeting, std::nullopt, 2000);
                                                 std::cout << "  [触控] 回调内 ShowPage 返回 " << static_cast<int>(ret)
                                                           << "\n";
                                             });
    std::cout << "  注册待机页【公共会议】handle=" << openMeeting << "（应非 0）\n";

    // 2) 会议管理页【返回】注册：回调内回到待机页
    uint64_t home = hal.RegisterClick(DisplayPage::MeetingMgmt, ScreenButton::MgmtReturnHome,
                                      [&hal](const TouchEvent&) {
                                          std::cout << "  [触控] 会议管理页返回 -> 回待机页\n";
                                          hal.ShowPage(DisplayPage::Idle, std::nullopt, 2000);
                                      });
    std::cout << "  注册会议管理页【返回】handle=" << home << "（应非 0）\n";

    // 3) 归属页错配：按钮不归属该页，应被键值表校验拒绝
    uint64_t wrongPage = hal.RegisterClick(DisplayPage::Meeting, ScreenButton::PublicMeeting, [](const TouchEvent&) {});
    std::cout << "  归属页错配（Meeting 页注册【公共会议】）handle=" << wrongPage << "（应为 0）\n";

    // 4) 控件接管按钮：翻页由 MeetingMgmtListWidget 内部接管，不属于任何页面键值表，应被拒绝
    uint64_t ownedButton = hal.RegisterClick(DisplayPage::MeetingMgmt, ScreenButton::MgmtNextPage,
                                             [](const TouchEvent&) {});
    std::cout << "  注册控件接管按钮（会议管理页下一页）handle=" << ownedButton << "（应为 0）\n";

    // 5) 注销：二次确认页按钮注册后立即注销，重复注销应失败
    uint64_t cancel = hal.RegisterClick(DisplayPage::DeleteConfirm, ScreenButton::ConfirmCancel,
                                        [](const TouchEvent&) {});
    bool unregistered = hal.UnregisterClick(cancel);
    std::cout << "  确认页【取消】注册 handle=" << cancel << " 注销=" << (unregistered ? "true" : "false")
              << " 重复注销=" << (hal.UnregisterClick(cancel) ? "true" : "false") << "（应为 false）\n";

    std::cout << "提示: 请点按待机页【公共会议】与会议管理页【返回】，验证回调输出与页面跳转\n";
    std::cout << "提示: 临时锁屏可用 SetTouchEnabled(false/true)；该接口同时停止 DGUS 刷新，\n";
    std::cout << "      恢复后需重新下发页面数据，故本用例不自动执行\n";
}

static void RunAllTests(ScreenPageManager& hal) {
    TestIdlePage(hal);
    TestMeetingPage(hal);
    TestFingerprintPage(hal);
    TestFingerprintAuthPage(hal);
    TestIdleTaskName(hal);
    TestUpgradePage(hal);
    TestMeetingMgmtPage(hal);
    TestTaskFlowPages(hal);
    TestPopWidget(hal);
    TestTouchRegister(hal);
}

int main(int argc, char** argv) {
    std::cout << "屏幕测试程序启动\n";

    DisplayConfig config;
    config.protocolType = ScreenProtocolType::Diwen;
    config.transportType = ScreenTransportType::Serial;
    config.transport.port = DEFAULT_SERIAL_PORT;
    config.transport.baudRate = DEFAULT_SERIAL_BAUDRATE;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "--port" && i + 1 < argc) {
            config.transport.port = argv[++i];
        } else if (arg == "--baud" && i + 1 < argc) {
            try {
                config.transport.baudRate = static_cast<uint32_t>(std::stoul(argv[++i]));
            } catch (const std::exception&) {
                std::cerr << "警告: 无效的波特率值，使用默认值\n";
            }
        }
    }

    std::cout << "配置参数:\n";
    std::cout << "  串口路径: " << config.transport.port << "\n";
    std::cout << "  波特率: " << config.transport.baudRate << "\n";
    std::cout << "  步骤间隔: " << STEP_INTERVAL_SECONDS << "s\n";

    if (!Logger::GetInstance().Initialize("screen_test")) {
        std::cerr << "Logger init failed\n";
        return 1;
    }
    ScopeExit logExit([] { spdlog::shutdown(); });

    ScreenPageManager hal;
    if (hal.Init(config) != ScreenResult::OK) {
        std::cerr << "ScreenPageManager init failed\n";
        return 1;
    }
    ScopeExit halExit([&hal] { hal.Release(); });
    std::cout << "屏幕HAL初始化成功\n";

    RunInteractiveMenu(hal);

    std::cout << "\n===== 测试结束，回到待机页面 =====\n";
    WaitAndCheck(hal.ShowPage(DisplayPage::Idle, std::nullopt, 2000), "切换回待机页面");
    return 0;
}
