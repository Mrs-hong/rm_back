//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <sys/wait.h>
#include <thread>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"
#include "qifeng_framework/common/utils/mutex.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/atomic_write_file.h"
#include "common/config/device_config.h"
#include "common/proto_utils.h"
#include "common/status.h"
#include "core/device/device_service.h"
#include "core/device/ntp.h"
#include "internal/hal/hal_bridge.h"
#include "internal/recording_manager.h"

namespace qifeng_ca {

    DeviceService::DeviceService() {
        mDao = std::make_shared<DeviceDao>();
    }

    // 时间/NTP操作共享互斥锁: 默认超时
    static common::utils::TimedMutex &TimeOpMutex() {
        static common::utils::TimedMutex Mtx(std::chrono::milliseconds(10000));
        return Mtx;
    }

    // 命令执行结果: 退出码 + 输出(2>&1合并stdout/stderr, 去除尾部换行)
    struct CommandResult {
        int rc = -1;         // 退出码; -1表示popen失败或进程非正常退出
        std::string output;  // stdout与stderr合并后的输出
    };

    static CommandResult RunCommand(const std::string &cmd) {
        CommandResult res;
        std::string fullCmd = cmd + " 2>&1";
        FILE* pipe = popen(fullCmd.c_str(), "r");
        if (!pipe) {
            SLOG_WARN << "RunCommand: popen failed, cmd=[" << cmd << "]";
            return res;
        }
        std::array<char, 256> buffer {};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            res.output += buffer.data();
        }
        int status = pclose(pipe);
        res.rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        while (!res.output.empty() && (res.output.back() == '\n' || res.output.back() == '\r')) {
            res.output.pop_back();
        }
        return res;
    }

    static std::string ExecCommand(const std::string &cmd) {
        return RunCommand(cmd).output;
    }

    // ============ chrony基础设施检测与配置读取 ============
    // 服务名探测: RHEL系为chronyd, Debian系为chrony, 返回""表示服务单元未安装
    // 必须返回规范单元名: Debian系chronyd只是chrony.service的Alias, show对别名也返回loaded,
    // 但systemctl enable拒绝操作别名(rc=1: "Refusing to operate on alias name"),
    // start/restart却接受别名, 若用别名会导致enable失败却难以定位; Id属性返回真实单元名
    static std::string DetectChronyServiceName() {
        for (const char* name : {"chronyd", "chrony"}) {
            std::string load = ExecCommand(std::string("systemctl show ") + name + " --property=LoadState --value");
            if (load != "loaded") {
                continue;
            }
            std::string canonical = ExecCommand(std::string("systemctl show ") + name + " --property=Id --value");
            return canonical.empty() ? name : canonical;
        }
        return "";
    }

    // chrony是否可用: chronyc存在 + 服务单元已安装(输出参数返回服务名)
    static bool ChronyInstalled(std::string* service = nullptr) {
        if (ExecCommand("command -v chronyc").empty()) {
            return false;
        }
        std::string svc = DetectChronyServiceName();
        if (svc.empty()) {
            return false;
        }
        if (service != nullptr) {
            *service = svc;
        }
        return true;
    }

    // 配置文件路径探测: Debian系/etc/chrony/chrony.conf, RHEL系/etc/chrony.conf
    static std::string DetectChronyConfPath() {
        for (const char* path : {"/etc/chrony/chrony.conf", "/etc/chrony.conf"}) {
            std::ifstream ifs(path);
            if (ifs.good()) {
                return path;
            }
        }
        return "";
    }

    static std::string TrimSpaces(const std::string &s) {
        size_t start = s.find_first_not_of(" \t");
        if (start == std::string::npos) {
            return "";
        }
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    // 读取chrony.conf中第一个启用的NTP源(server/pool指令), 无则返回""
    static std::string ReadNtpFromChronyConf() {
        const std::string confPath = DetectChronyConfPath();
        if (confPath.empty()) {
            return "";
        }
        std::ifstream ifs(confPath);
        if (!ifs) {
            return "";
        }
        std::string line;
        while (std::getline(ifs, line)) {
            std::string trimmed = TrimSpaces(line);
            if (trimmed.empty() || trimmed[0] == '#') {
                continue;
            }
            for (const char* directive : {"server", "pool"}) {
                size_t len = strlen(directive);
                if (trimmed.rfind(directive, 0) == 0 &&
                    (trimmed.size() == len || trimmed[len] == ' ' || trimmed[len] == '\t')) {
                    std::istringstream ls(trimmed.substr(len));
                    std::string host;
                    if (ls >> host && !host.empty() && host[0] != '#') {
                        return host;
                    }
                }
            }
        }
        return "";
    }

    // 查询chrony当前同步源: chronyc -N sources中"^*"标记行(已选中的最优源)
    // -N表示按配置名输出(不做DNS解析), 与用户配置的域名/IP直接对应
    static std::string QueryChronySyncSource() {
        std::string sources = ExecCommand("chronyc -N sources");
        std::istringstream iss(sources);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.rfind("^*", 0) == 0) {
                std::istringstream ls(line.substr(2));
                std::string host;
                if (ls >> host && !host.empty()) {
                    return host;
                }
            }
        }
        return "";
    }

    // 查询chrony同步状态: tracking输出中"Leap status : Normal"表示已完成同步
    static bool QueryChronySynced() {
        std::string tracking = ExecCommand("chronyc tracking");
        std::istringstream iss(tracking);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.find("Leap status") != std::string::npos && line.find("Normal") != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // 验证chrony服务(带重试轮询, 规避启动初期异步DNS解析/源加载竞态导致的误判):
    // 1) 服务活跃(最多10s); 2) chronyc sources已加载到源(最多10s);
    // 3) requireSync=true时额外等待首次时钟同步(最多20s)。
    // 返回false仅表示验证阶段未完全就绪, 由调用方决定处理方式(当前不回滚已生效配置)。
    static bool VerifyChronyService(const std::string &service, const std::string &expectServer, bool requireSync) {
        // 源行特征: chronyc -N sources输出中源行以"^"+状态字符(* + - ? x)开头
        auto hasSourceLine = [](const std::string &sources) {
            std::istringstream iss(sources);
            std::string line;
            while (std::getline(iss, line)) {
                if (line.size() >= 2 && line[0] == '^' &&
                    (line[1] == '*' || line[1] == '+' || line[1] == '-' || line[1] == '?' || line[1] == 'x')) {
                    return true;
                }
            }
            return false;
        };

        constexpr int kPollTimeoutSec = 10;

        // 1) 服务活跃: 等待chronyd完成启动(enable --now/restart后异步进入active)
        bool active = false;
        for (int i = 0; i < kPollTimeoutSec; ++i) {
            std::string state = ExecCommand("systemctl show " + service + " --property=ActiveState");
            if (state.find("ActiveState=active") != std::string::npos) {
                active = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        if (!active) {
            SLOG_ERROR << "VerifyChronyService: " << service << " not active after " << kPollTimeoutSec << "s, "
                       << ", journal=[" << ExecCommand("journalctl -u " + service + " -n 20 --no-pager") << "]";
            return false;
        }

        // 2) 源已加载: 等待chronyc sources出现源行(首次DNS解析/源初始化可能较慢)
        bool sourceLoaded = false;
        std::string sources;
        for (int i = 0; i < kPollTimeoutSec; ++i) {
            sources = ExecCommand("chronyc -N sources");
            if (hasSourceLine(sources)) {
                sourceLoaded = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        if (!sourceLoaded) {
            SLOG_ERROR << "VerifyChronyService: no NTP source loaded after " << kPollTimeoutSec << "s" << ", expect=["
                       << expectServer << "], sources=[" << sources << "]" << ", journal=["
                       << ExecCommand("journalctl -u " + service + " -n 20 --no-pager") << "]";
            return false;
        }
        if (!expectServer.empty() && sources.find(expectServer) == std::string::npos) {
            SLOG_WARN << "VerifyChronyService: loaded sources may not yet contain expectServer=[" << expectServer
                      << "], sources=[" << sources << "] (chrony仍在解析/选择源)";
        }

        if (!requireSync) {
            SLOG_WARN << "VerifyChronyService: skip sync check (UDP blocked at precheck), server=" << expectServer;
            return true;
        }

        // 3) 等待首次同步(chrony后续会持续同步; 此处超时不再回滚, 仅告警)
        for (int i = 0; i < 20; ++i) {
            // 主动触发步进: 一旦源出现可测偏移即立即拉齐时间, 跳过慢速微调（只用chronyc makestep
            // 来立即同步，不用修改配置中同步次数）
            ExecCommand("chronyc makestep");
            if (QueryChronySynced()) {
                SLOG_INFO << "VerifyChronyService: chrony synchronized after " << i * 1000 << "ms";
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        SLOG_WARN << "VerifyChronyService: not synchronized within 20s (chrony will retry), server=[" << expectServer
                  << "], sources=[" << ExecCommand("chronyc -N sources -v") << "]";
        return false;
    }

    static void FillSubItemFromJson(const std::string &json, google::protobuf::Message &msg) {
        if (json.empty()) {
            return;
        }
        if (!proto_utils::JsonToMessage(json, msg)) {
            SLOG_WARN << "FillSubItemFromJson: JsonToMessage failed for " << msg.GetTypeName();
        }
    }

    static bool CollectAudioInfo(SystemInfoItem &item) {
        bool connected = HalBridge::GetInstance().HasMicrophone();
        auto* audio = item.mutable_audio();
        if (connected) {
            audio->set_connect("1");
            audio->set_status("normal");
            audio->set_msg("");
        } else {
            audio->set_connect("0");
            audio->set_status("normal");
            audio->set_msg("");
        }
        return true;
    }

    static void FillSystemInfoItem(const models::DeviceRecord &record, SystemInfoItem* item) {
        item->set_timestamp(static_cast<uint64_t>(GetTimeMs() / 1000));
        item->set_device_id(record.mDeviceId);
        item->set_arch(DeviceConfig::GetInstance().GetArchName());

        FillSubItemFromJson(record.mCpu, *item->mutable_cpu());
        FillSubItemFromJson(record.mMemory, *item->mutable_memory());
        FillSubItemFromJson(record.mDisk, *item->mutable_disk());
        FillSubItemFromJson(record.mAccelerator, *item->mutable_accelerator());
        FillSubItemFromJson(record.mBattery, *item->mutable_battery());
        FillSubItemFromJson(record.mConnect, *item->mutable_connect());

        VersionInfo versionInfo;
        FillSubItemFromJson(record.mVersion, versionInfo);

        for (auto &version : *versionInfo.mutable_versions()) {
            item->mutable_versions()->Add(std::move(version));
        }

        CollectAudioInfo(*item);
    }

    static NtpTester::Result TestNtpServer(const std::string &server) {
        return NtpTester::NtpTest(server);
    }

    static Status BuildNtpTestFailedStatus(NtpTester::Result result) {
        switch (result) {
            case NtpTester::Result::ResolveFailed:
                return Status {-1, "NTP服务器域名不存在或解析失败，请检查服务器地址或网络后重试"};
            case NtpTester::Result::DnsTemporary:
                return Status {-1, "网络异常导致域名解析失败，请稍后重试"};
            case NtpTester::Result::SocketFailed:
            case NtpTester::Result::SendFailed:
                return Status {-1, "NTP服务器连接失败，无法正常同步时间"};
            case NtpTester::Result::UdpBlocked:
            case NtpTester::Result::Unreachable:
                return Status {-1, "NTP服务器不可达（网络拒绝或无响应），请检查网络或更换服务器"};
            case NtpTester::Result::ReceiveFailed:
            case NtpTester::Result::InvalidResponse:
                return Status {-1, "NTP服务器无响应或拒绝服务，请稍后重试或更换服务器"};
            case NtpTester::Result::Success:
                break;
            default:
                return Status {-1, "NTP服务器测试失败，无法正常同步时间"};
        }
        return Status {};
    }

    // 构建更新后的chrony.conf内容:
    // 1. 首个启用的server/pool行替换为"server <ntp> iburst"(iburst加速首次同步, 解决启动慢);
    // 2. 其余server/pool源注释掉(用户语义为设置唯一NTP服务器);
    // 3. 确保makestep指令存在: 偏差>1s时前3次更新直接步进时钟, 避免大偏差以500ppm缓慢追赶
    static std::string BuildUpdatedChronyConfig(const std::string &original, const std::string &ntpServer) {
        std::istringstream iss(original);
        std::string line;
        std::string result;
        bool primaryWritten = false;
        bool hasMakestep = false;

        while (std::getline(iss, line)) {
            std::string trimmed = TrimSpaces(line);
            bool isComment = trimmed.empty() || trimmed[0] == '#';
            bool isSource = false;
            if (!isComment) {
                for (const char* directive : {"server", "pool"}) {
                    size_t len = strlen(directive);
                    if (trimmed.rfind(directive, 0) == 0 &&
                        (trimmed.size() == len || trimmed[len] == ' ' || trimmed[len] == '\t')) {
                        isSource = true;
                        break;
                    }
                }
            }
            if (isSource) {
                if (!primaryWritten) {
                    result += "server " + ntpServer + " iburst\n";
                    primaryWritten = true;
                } else {
                    result += "# [managed-by-qifeng_ca] " + line + "\n";  // 注释多余源
                }
                continue;
            }
            if (!isComment && trimmed.rfind("makestep", 0) == 0) {
                hasMakestep = true;
            }
            result += line + "\n";
        }
        if (!primaryWritten) {
            result = "server " + ntpServer + " iburst\n" + result;
        }
        // 暂时不改成-1（永远同步），避免无网等环境跳变异常
        if (!hasMakestep) {
            result += "makestep 1.0 3\n";
        }
        return result;
    }

    Status DeviceService::GetSystemInfo(const std::string &deviceId, GetSystemInfoResponse* resp) {
        try {
            std::string id = deviceId.empty() ? "device_1" : deviceId;
            models::DeviceRecord record = mDao->GetLatestRecord(id);

            auto* data = resp->mutable_data();
            auto* item = data->add_device_list();

            if (!record.mDeviceId.empty()) {
                FillSystemInfoItem(record, item);
            }

            float cpuMaxTemp = mDao->QueryMaxTemperature(id, "cpu");
            if (cpuMaxTemp > 0.0F) {
                item->mutable_cpu()->set_max_temperature(cpuMaxTemp);
            }
            float npuMaxTemp = mDao->QueryMaxTemperature(id, "npu");
            if (npuMaxTemp > 0.0F) {
                item->mutable_accelerator()->mutable_npu()->set_max_temperature(npuMaxTemp);
            }

            // NTP源查询链: chrony当前同步源 → chrony.conf配置 → (历史源已废弃)
            std::string ntp = QueryChronySyncSource();
            if (ntp.empty()) {
                ntp = ReadNtpFromChronyConf();
            }
            if (!ntp.empty()) {
                item->set_ntp(ntp);
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetSystemInfo exception: " << e.what();
            return Status {-1, "获取系统信息异常"};
        }
        return Status {};
    }

    Status DeviceService::QueryDeviceHistory(const std::string &deviceId, int64_t startTime, int64_t endTime,
                                             QueryDeviceHistoryResponse* resp) {
        if (startTime <= 0 || endTime <= 0 || startTime > endTime) {
            return Status {-1, "时间范围参数无效"};
        }

        try {
            auto records = mDao->QueryByTimeRange(deviceId, startTime, endTime);

            auto* data = resp->mutable_data();
            for (const auto &record : records) {
                auto* item = data->add_device_list();
                FillSystemInfoItem(record, item);
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "QueryDeviceHistory exception: " << e.what();
            return Status {-1, std::string("查询设备历史异常")};
        }
        return Status {};
    }

    Status DeviceService::UpdateSystemTime(const UpdateSystemTimeRequest &req) {
        if (req.timestamp() <= 0) {
            return Status {-1, "时间戳参数无效"};
        }
        // 共享锁: 与UpdateSystemNTPServer互斥, 防止并发修改时间/NTP配置导致状态不一致
        try {
            std::lock_guard<common::utils::TimedMutex> lock(TimeOpMutex());
            return UpdateSystemTimeImpl(req);
        } catch (const std::exception &e) {
            SLOG_WARN << "UpdateSystemTime: lock timeout, " << e.what();
            return {-1, "系统时间设置繁忙，请稍后再试"};
        }
    }

    Status DeviceService::UpdateSystemTimeImpl(const UpdateSystemTimeRequest &req) {
        try {
            // 先关闭NTP同步: 防止chrony在设置时间窗口内覆盖手动时间;
            // 手动设置时间具有最高优先级, 后续若用户重新设置NTP服务器(UpdateSystemNTPServer), chrony将重新接管
            CommandResult offlineRes = RunCommand("chronyc offline");
            if (offlineRes.rc != 0) {
                SLOG_WARN << "UpdateSystemTime: chronyc offline failed, rc=" << offlineRes.rc
                          << ", out=" << offlineRes.output;
            }
            CommandResult timedateCtlRes = RunCommand("timedatectl set-ntp false");
            if (timedateCtlRes.rc != 0) {
                SLOG_WARN << "UpdateSystemTime: timedatectl set-ntp false failed, rc=" << timedateCtlRes.rc
                          << ", out=" << timedateCtlRes.output << ", manual time may be overwritten";
            } else {
                SLOG_INFO << "UpdateSystemTime: NTP sync disabled before setting time";
            }

            // 记录当前时间用于回滚
            std::string currentTime = ExecCommand("date +%s");
            int64_t timestampSeconds = req.timestamp() / 1000;
            std::string timeCmd = "date -s @" + std::to_string(timestampSeconds);
            CommandResult setTimeRes = RunCommand(timeCmd);
            if (setTimeRes.rc != 0) {
                SLOG_ERROR << "UpdateSystemTime: date -s failed, rc=" << setTimeRes.rc << ", out=" << setTimeRes.output
                           << ", current time unchanged";
                return Status {-1, "设置系统时间失败"};
            }

            CommandResult hwclockRes = RunCommand("hwclock --systohc");
            if (hwclockRes.rc != 0) {
                // 硬件时钟同步失败: 回滚系统时间并返回错误
                SLOG_WARN << "UpdateSystemTime: hwclock --systohc failed, rc=" << hwclockRes.rc
                          << ", out=" << hwclockRes.output << ", restoring previous time";
                CommandResult restoreRes = RunCommand("date -s @" + currentTime);
                if (restoreRes.rc != 0) {
                    SLOG_ERROR << "UpdateSystemTime: restore previous time failed, rc=" << restoreRes.rc
                               << ", out=" << restoreRes.output;
                }
                return Status {-1, "硬件时钟同步失败"};
            }

            SLOG_INFO << "System time updated successfully (NTP sync disabled)";
        } catch (const std::exception &e) {
            SLOG_ERROR << "UpdateSystemTime exception: " << e.what();
            return Status {-1, std::string("设置系统时间异常")};
        }
        return Status {};
    }

    Status DeviceService::GetNTPIP(GetNTPIPResponse* resp) {
        try {
            // 优先返回chrony当前同步源，未同步时回退到chrony.conf中的首个NTP源
            std::string serverName = QueryChronySyncSource();
            if (!serverName.empty()) {
                resp->set_ntp(serverName);
                return Status {};
            }

            std::string confNtp = ReadNtpFromChronyConf();
            if (!confNtp.empty()) {
                resp->set_ntp(confNtp);
                return Status {};
            }

            SLOG_WARN << "未找到活跃的NTP服务器（chrony无同步源且无配置）";
            return {};
        } catch (const std::exception &e) {
            SLOG_ERROR << "GetNTPIP exception: " << e.what();
            return Status {-1, std::string("获取NTP信息异常")};
        }
    }

    Status DeviceService::IsDeviceIdle(IsDeviceIdleResponse* resp) {
        // bool idle = HalBridge::GetInstance().IsRecordingIdle();
        bool idle = !RecordingManager::GetInstance().IsRecording();
        resp->set_idle(idle);
        SLOG_WARN << "IsDeviceIdle: idle=" << resp->idle();
        return Status {};
    }

    Status DeviceService::UpdateSystemNTPServer(const UpdateSystemNTPServerRequest &req) {
        if (req.ntp().empty()) {
            return Status {-1, "NTP服务器地址不能为空"};
        }
        // 前置检查: chrony必须可用(chronyc + 服务单元), 否则给出明确安装指引
        std::string chronyService;
        if (!ChronyInstalled(&chronyService)) {
            SLOG_ERROR << "UpdateSystemNTPServer: chrony not installed (chronyc or service unit missing)";
            return Status {-1, "NTP繁忙，请稍后再试"};
        }
        // 先测试NTP服务器可用性(无需持锁: 网络测试耗时较长, getaddrinfo无超时最多30s + recvfrom超时5s)
        NtpTester::Result ntpTestResult = TestNtpServer(req.ntp());
        // 预检策略: UDP静默超时但ICMP可达(UdpBlocked)说明网络只放行ICMP拦了UDP/123,
        // 此时chrony同样大概率同步不上, 但配置本身有效 —— 放行写入并跳过同步强验证,
        // 交由chrony后台以指数退避持续重试(网络策略放开后自动完成同步), 避免可ping通却配置失败
        bool requireSync = true;
        if (ntpTestResult == NtpTester::Result::UdpBlocked) {
            SLOG_WARN << "UpdateSystemNTPServer: UDP probe blocked but ICMP reachable for [" << req.ntp()
                      << "], allow config (chrony will retry), skip sync verification";
            requireSync = false;
        } else if (ntpTestResult != NtpTester::Result::Success) {
            return BuildNtpTestFailedStatus(ntpTestResult);
        }
        // 共享锁: 保证配置文件修改原子性, 并与UpdateSystemTime互斥
        try {
            std::lock_guard<common::utils::TimedMutex> lock(TimeOpMutex());
            return UpdateSystemNTPServerImpl(req, requireSync);
        } catch (const std::exception &e) {
            SLOG_WARN << "UpdateSystemNTPServer: lock timeout, " << e.what();
            return {-1, "NTP繁忙，请稍后再试"};
        }
    }

    Status DeviceService::UpdateSystemNTPServerImpl(const UpdateSystemNTPServerRequest &req, bool requireSync) {
        try {
            // NTP可用性已在UpdateSystemNTPServer中预先测试通过
            std::string chronyService;
            if (!ChronyInstalled(&chronyService)) {
                return Status {-1, "NTP繁忙，请稍后再试"};
            }

            // 记录timesyncd初始状态: chrony接管NTP前需停用timesyncd(两者都绑UDP/123会互相干扰),
            // 回滚时若其原本在运行则恢复, 避免破坏设备原状
            bool timesyncdWasActive = ExecCommand("systemctl is-active systemd-timesyncd") == "active";

            // 配置文件路径: 探测已有conf;
            std::string confPath = DetectChronyConfPath();
            bool confExisted = !confPath.empty();
            if (confPath.empty()) {
                confPath = "/etc/chrony/chrony.conf";
                CommandResult mkdirRes = RunCommand("mkdir -p /etc/chrony");
                if (mkdirRes.rc != 0) {
                    SLOG_ERROR << "UpdateSystemNTPServer: mkdir /etc/chrony failed, rc=" << mkdirRes.rc
                               << ", out=" << mkdirRes.output;
                    return Status {-1, "NTP配置失败"};
                }
            }

            // 读取原始配置用于回滚; 失败按空配置处理
            std::string originalConfig;
            if (confExisted) {
                Status status = AtomicFileWriter::ReadFile(confPath, originalConfig);
                if (!status.IsSuccess()) {
                    SLOG_WARN << "UpdateSystemNTPServer: read config failed(" << status.ToString()
                              << "), treat as empty config";
                    originalConfig.clear();
                }
            }

            // 构建并原子写入新配置(server行替换 + iburst + makestep保障快速精准校准)
            std::string newConfig = BuildUpdatedChronyConfig(originalConfig, req.ntp());
            if (!AtomicFileWriter::WriteAtomic(confPath, newConfig)) {
                SLOG_ERROR << "UpdateSystemNTPServer: write config failed: " << confPath;
                return Status {-1, "NTP配置失败"};
            }

            // 回滚仅在后续系统命令执行失败时触发
            // 回滚时恢复原配置、重启chrony、并按原状恢复timesyncd
            ScopeExit restoreFunc([&]() {
                if (confExisted) {
                    AtomicFileWriter::WriteAtomic(confPath, originalConfig);
                } else {
                    std::remove(confPath.c_str());
                }
                RunCommand("systemctl restart " + chronyService);
                if (timesyncdWasActive) {
                    RunCommand("systemctl enable --now systemd-timesyncd");
                }
            });

            // chrony接管NTP: 停用timesyncd避免UDP/123端口冲突, 再启用chrony并加载新配置
            CommandResult disTimesyncdRes = RunCommand("systemctl disable --now systemd-timesyncd");
            if (disTimesyncdRes.rc != 0) {
                SLOG_WARN << "UpdateSystemNTPServer: disable timesyncd failed, rc=" << disTimesyncdRes.rc
                          << ", out=" << disTimesyncdRes.output;
            }

            CommandResult enableRes = RunCommand("systemctl enable --now " + chronyService);
            if (enableRes.rc != 0) {
                SLOG_ERROR << "UpdateSystemNTPServer: enable --now " << chronyService << " failed, rc=" << enableRes.rc
                           << ", out=" << enableRes.output << ", rolling back";
                return Status {-1, "启动NTP服务失败"};
            }
            CommandResult restartRes = RunCommand("systemctl restart " + chronyService);
            if (restartRes.rc != 0) {
                SLOG_ERROR << "UpdateSystemNTPServer: restart " << chronyService << " failed, rc=" << restartRes.rc
                           << ", out=" << restartRes.output << ", rolling back";
                return Status {-1, "重启NTP服务失败"};
            }

            // 验证(带轮询): 服务活跃 + 新配置源加载
            VerifyChronyService(chronyService, req.ntp(), requireSync);

            restoreFunc.Release();

            SLOG_INFO << "NTP server updated to: " << req.ntp() << " (chrony service: " << chronyService << ")";
        } catch (const std::exception &e) {
            SLOG_ERROR << "UpdateSystemNTPServer exception: " << e.what();
            return Status {-1, std::string("更新NTP配置异常")};
        }
        return Status {};
    }

}  // namespace qifeng_ca
