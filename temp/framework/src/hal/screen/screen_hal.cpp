/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>

#include "common/logger.h"
#include "hal/screen/protocol/addr_map.h"
#include "hal/screen/protocol/diwen_protocol.h"
#include "hal/screen/screen_hal.h"
#include "hal/screen/transport/serial_transport.h"

namespace qifeng {

    namespace {
        constexpr size_t BytesPerChunk = 240;
        constexpr uint16_t AddrStepPerChunk = 0x78;
        constexpr size_t BlockThreshold = 32768;
        constexpr uint8_t FlashStatusBusy = 0x5A;                                   // byte[1]=0x5A 正在写入
        constexpr uint8_t FlashStatusDone = 0x00;                                   // byte[1]=0x00 写入完成
        constexpr std::array<uint8_t, 4> RebootPayload = {0x55, 0xAA, 0x5A, 0xA5};  // 复位指令
        constexpr std::array<uint8_t, 4> DgusStopPayload = {0x55, 0xAA, 0x5A, 0xA5};

        std::string Basename(const std::string& path) {
            auto pos = path.find_last_of("/\\");
            return (pos == std::string::npos) ? path : path.substr(pos + 1);
        }

        // 固定宽度数字字段:左补 '0' 右对齐,交给 Text 编码链(GBK+0xFFFF 结束符)。
        std::string PadLeftZero(const std::string& s, size_t width) {
            std::string r = s;
            if (r.size() > width) {
                r.resize(width);
            }
            if (r.size() < width) {
                r.insert(0, width - r.size(), '0');
            }
            return r;
        }
    }  // namespace

    ScreenHAL::~ScreenHAL() {
        Release();
    }

    ScreenResult ScreenHAL::Init(const DisplayConfig& config) {
        if (mInitialized.load(std::memory_order_acquire)) {
            SLOG_WARN << "ScreenHAL already initialized";
            return ScreenResult::OK;
        }

        switch (config.protocolType) {
            case ScreenProtocolType::Diwen:
                mProtocol = std::make_unique<DiwenProtocol>();
                break;
            default:
                SLOG_ERROR << "Unsupported protocol type";
                return ScreenResult::InvalidParameter;
        }

        switch (config.transportType) {
            case ScreenTransportType::Serial: {
                auto transport = std::make_unique<SerialTransport>(config.transport);
                transport->SetSendCompleteCallback([this](uint64_t sessionId) { OnSendComplete(sessionId); });
                transport->SetReceiveCallback([this](const uint8_t* data, size_t len) { OnReceiveData(data, len); });

                if (!transport->Init()) {
                    SLOG_ERROR << "Failed to init serial transport";
                    return ScreenResult::InvalidParameter;
                }
                mTransport = std::move(transport);
                break;
            }
            default:
                SLOG_ERROR << "Unsupported transport type";
                return ScreenResult::InvalidParameter;
        }

        mInitialized.store(true, std::memory_order_release);
        SLOG_INFO << "ScreenHAL initialized";
        return ScreenResult::OK;
    }

    void ScreenHAL::Release() {
        if (!mInitialized.load(std::memory_order_acquire)) {
            return;
        }

        if (mTransport) {
            mTransport->Release();
            mTransport.reset();
        }
        mProtocol.reset();

        {
            std::lock_guard<std::mutex> lock(mSessionMutex);
            mSessions.clear();
            mPendingAckQueue.clear();
        }

        {
            std::lock_guard<std::mutex> lock(mReceiveMutex);
            mReceiveBuffer.clear();
        }

        {
            std::lock_guard<std::mutex> lock(mCacheMutex);
            mFieldCache.clear();
            mCurrentPage.reset();
        }

        mInitialized.store(false, std::memory_order_release);
        SLOG_INFO << "ScreenHAL released";
    }

    ScreenResult ScreenHAL::SwitchPage(DisplayPage page, uint32_t timeoutMs) {
        if (!mInitialized.load(std::memory_order_acquire)) {
            return ScreenResult::NotInitialized;
        }

        {
            std::lock_guard<std::mutex> lock(mCacheMutex);
            if (mCurrentPage.has_value() && mCurrentPage.value() == page) {
                return ScreenResult::OK;
            }
        }

        Request request;
        request.address = Addr::PageSwitch;
        // 迪文页面切换需要先发0x5A01前缀再发页面编号
        request.value = std::vector<uint16_t> {0x5A01, static_cast<uint16_t>(page)};

        std::vector<uint8_t> frame;
        if (!mProtocol->Encode(request, frame)) {
            return ScreenResult::InvalidParameter;
        }

        auto sessionId = mNextSessionId.fetch_add(1, std::memory_order_relaxed);
        std::vector<std::pair<uint64_t, std::vector<uint8_t>>> frames;
        frames.emplace_back(sessionId, std::move(frame));

        auto result = SendRequests(std::move(frames), timeoutMs);
        if (result == ScreenResult::OK) {
            std::lock_guard<std::mutex> lock(mCacheMutex);
            mCurrentPage = page;
        }
        return result;
    }

    ScreenResult ScreenHAL::UpdateWidget(WidgetType type, const WidgetData& data, uint32_t timeoutMs) {
        if (!mInitialized.load(std::memory_order_acquire)) {
            return ScreenResult::NotInitialized;
        }

        std::vector<Request> requests;
        if (!BuildWidgetRequests(type, data, requests)) {
            SLOG_ERROR << "Failed to build widget requests";
            return ScreenResult::InvalidParameter;
        }

        FilterUnchangedRequests(requests);
        if (requests.empty()) {
            return ScreenResult::OK;
        }

        std::vector<std::pair<uint64_t, std::vector<uint8_t>>> frames;
        auto sessionId = mNextSessionId.fetch_add(1, std::memory_order_relaxed);
        for (auto& req : requests) {
            std::vector<uint8_t> frame;
            if (!mProtocol->Encode(req, frame)) {
                SLOG_ERROR << "Failed to encode request for addr 0x" << std::hex << req.address;
                return ScreenResult::InvalidParameter;
            }
            frames.emplace_back(sessionId, std::move(frame));
        }

        auto result = SendRequests(std::move(frames), timeoutMs);
        if (result == ScreenResult::OK) {
            UpdateCache(requests);
        }
        return result;
    }

    bool ScreenHAL::BuildWidgetRequests(WidgetType type, const WidgetData& data, std::vector<Request>& out) {
        switch (type) {
            case WidgetType::IdleAvailability:  // NOLINT(bugprone-branch-clone)
                BuildIdleAvailability(std::get<IdleAvailabilityData>(data), out);
                break;
            case WidgetType::IdleSummary:
                BuildIdleSummary(std::get<IdleSummaryData>(data), out);
                break;
            case WidgetType::IdleTask:
                BuildIdleTask(std::get<IdleTaskData>(data), out);
                break;
            case WidgetType::IdleStatus:
                BuildIdleStatus(std::get<IdleStatusData>(data), out);
                break;
            case WidgetType::MeetingBasicInfo:
                BuildMeetingBasic(std::get<MeetingBasicData>(data), out);
                break;
            case WidgetType::MeetingInfo:
                BuildMeetingInfo(std::get<MeetingInfoData>(data), out);
                break;
            case WidgetType::MeetingEnergy:
                BuildMeetingEnergy(std::get<MeetingEnergyData>(data), out);
                break;
            case WidgetType::MeetingClock:
                return BuildMeetingClock(std::get<MeetingClockData>(data), out);
            case WidgetType::DeviceInfo:
                BuildDeviceInfo(std::get<DeviceInfoData>(data), out);
                break;
            case WidgetType::Fingerprint:
                BuildFingerprint(std::get<FingerprintData>(data), out);
                break;
            case WidgetType::Upgrade:
                BuildUpgrade(std::get<UpgradeData>(data), out);
                break;
            default:
                SLOG_ERROR << "Unsupported widget type: " << static_cast<int>(type);
                return false;
        }
        return true;
    }

    void ScreenHAL::BuildIdleAvailability(const IdleAvailabilityData& avail, std::vector<Request>& out) {
        out.emplace_back(Addr::AvailableTime, static_cast<uint16_t>(avail.availableTime));
        out.emplace_back(Addr::AvailableRatioLevel, avail.ratioLevel);
        out.emplace_back(Addr::AvailableRatioText, avail.ratioValue);
    }

    void ScreenHAL::BuildIdleSummary(const IdleSummaryData& summary, std::vector<Request>& out) {
        out.emplace_back(Addr::SummaryCompletedCount, summary.completedCount);
        out.emplace_back(Addr::SummaryPendingCount, summary.pendingCount);
        out.emplace_back(Addr::SummaryTotalCount, summary.totalCount);
        out.emplace_back(Addr::SummaryRatioLevel, summary.ratioLevel);
    }

    void ScreenHAL::BuildIdleTask(const IdleTaskData& task, std::vector<Request>& out) {
        // 状态切换时清除文本字段缓存，确保重置帧和新数据都不会被差量过滤吞掉
        {
            std::lock_guard<std::mutex> lock(mCacheMutex);
            auto it = mFieldCache.find(Addr::TaskStatus);
            if (it == mFieldCache.end() || it->second != Value(task.status)) {
                mFieldCache.erase(Addr::TaskName);
                mFieldCache.erase(Addr::TaskStatusText);
                mFieldCache.erase(Addr::TaskProgressText);
                mFieldCache.erase(Addr::TaskTotalTime);
            }
        }

        out.emplace_back(Addr::TaskStatus, task.status);
        out.emplace_back(Addr::TaskRatioLevel, task.ratioLevel);

        // 文本字段清空必须用0xFFFF：协议文档要求"不显示文本"时直接写入FFFF作为结束符
        if (task.name.empty()) {
            out.emplace_back(Addr::TaskName, static_cast<uint16_t>(0xFFFF));
        } else {
            out.emplace_back(Addr::TaskName, Text {SanitizeDisplayText(task.name, 32), 32});
        }

        // statusText 仅携带 hh:mm 时间部分，周围静态文本由显示屏预置；0x301C..0x3022 共6字=12字节
        if (task.status <= 2 || task.statusText.empty()) {
            out.emplace_back(Addr::TaskStatusText, static_cast<uint16_t>(0xFFFF));
        } else {
            out.emplace_back(Addr::TaskStatusText, Text {task.statusText, 12});
        }

        // 进度/总分钟数为纯数字，左补 '0' 右对齐（"5"→"05"）后走 Text 编码（GBK+0xFFFF 结束符）
        if (task.status > 2) {
            out.emplace_back(Addr::TaskProgressText, Text {PadLeftZero(task.progressMinutes, 2), 2});
            out.emplace_back(Addr::TaskTotalTime, Text {PadLeftZero(task.totalMinutes, 2), 2});
        } else {
            out.emplace_back(Addr::TaskProgressText, static_cast<uint16_t>(0xFFFF));
            out.emplace_back(Addr::TaskTotalTime, static_cast<uint16_t>(0xFFFF));
        }
    }

    void ScreenHAL::BuildIdleStatus(const IdleStatusData& status, std::vector<Request>& out) {
        out.emplace_back(Addr::DeviceStatus, status.status);
    }

    void ScreenHAL::BuildMeetingInfo(const MeetingInfoData& meeting, std::vector<Request>& out) {
        out.emplace_back(Addr::MeetingType, meeting.type);
        if (!meeting.name.empty()) {
            out.emplace_back(Addr::MeetingName, Text {SanitizeDisplayText(meeting.name, 32), 32});
        }
        out.emplace_back(Addr::MeetingOperationTip, meeting.operationTip);
        out.emplace_back(Addr::MeetingAudioStatus, meeting.audioStatus);
        out.emplace_back(Addr::MeetingRatioLevel, meeting.ratioLevel);
        out.emplace_back(Addr::MeetingCountDownTip, meeting.countDownTip);
        if (meeting.countDown.empty()) {
            out.emplace_back(Addr::MeetingCountDown, static_cast<uint16_t>(0xFFFF));
        } else {
            out.emplace_back(Addr::MeetingCountDown, Text {SanitizeDisplayText(meeting.countDown, 8), 8});
        }
    }

    void ScreenHAL::BuildMeetingBasic(const MeetingBasicData& basic, std::vector<Request>& out) {
        if (!basic.sponsor.empty()) {
            out.push_back({Addr::MeetingSponsor, Text {SanitizeDisplayText(basic.sponsor, 10), 10}});
        }
        if (!basic.startTime.empty()) {
            // 文档要求文本显示"2026/01/01/00:00"，直接走文本编码链
            out.push_back({Addr::MeetingStartTime, Text {SanitizeDisplayText(basic.startTime, 16), 16}});
        }
    }

    void ScreenHAL::BuildMeetingEnergy(const MeetingEnergyData& energy, std::vector<Request>& out) {
        // 文档序号19要求范围1~52，52为暂停值
        std::vector<uint16_t> clamped(energy.energyValues.size());
        for (size_t i = 0; i < energy.energyValues.size(); ++i) {
            clamped[i] = std::clamp(energy.energyValues[i], static_cast<uint16_t>(1), static_cast<uint16_t>(52));
        }
        out.emplace_back(Addr::MeetingEnergy, std::move(clamped));
    }

    bool ScreenHAL::BuildMeetingClock(const MeetingClockData& clock, std::vector<Request>& out) {
        // 文档要求 0x3189 写入 ASCII 文本 "HH:MM:SS"，每秒更新
        uint16_t h = std::min(clock.hour, static_cast<uint16_t>(23));
        uint16_t m = std::min(clock.minute, static_cast<uint16_t>(59));
        uint16_t s = std::min(clock.second, static_cast<uint16_t>(59));
        auto pad2 = [](uint16_t v) -> std::string { return v < 10 ? "0" + std::to_string(v) : std::to_string(v); };
        out.emplace_back(Addr::MeetingClock, Text {pad2(h) + ":" + pad2(m) + ":" + pad2(s), 8});
        return true;
    }

    void ScreenHAL::BuildDeviceInfo(const DeviceInfoData& info, std::vector<Request>& out) {
        out.emplace_back(Addr::DeviceWifiStatus, info.wifiStatus);
        if (!info.wifiSSID.empty()) {
            out.push_back({Addr::DeviceWifiSSID, Text {SanitizeDisplayText(info.wifiSSID, 32), 32}});
        }
        if (!info.ipAddress.empty()) {
            out.push_back({Addr::DeviceIPAddress, Text {SanitizeDisplayText(info.ipAddress, 15), 15}});
        }
    }

    void ScreenHAL::BuildFingerprint(const FingerprintData& fp, std::vector<Request>& out) {
        out.emplace_back(Addr::FingerprintRatioLevel, fp.ratioLevel);
        if (!fp.tip.empty()) {
            out.emplace_back(Addr::FingerprintTip, Text {SanitizeDisplayText(fp.tip, 48), 48});
        }
        out.emplace_back(Addr::FingerprintResult, fp.result);
    }

    void ScreenHAL::BuildUpgrade(const UpgradeData& data, std::vector<Request>& out) {
        out.emplace_back(Addr::UpdateSystemTip, data.title);
        out.emplace_back(Addr::UpdateSystemTypes, data.types);
        out.emplace_back(Addr::UpgradeLoading, data.loading);
    }

    std::string ScreenHAL::SanitizeDisplayText(const std::string& text, size_t maxGbkBytes) {
        // 屏幕不支持下划线显示，替换为连字符
        std::string result = text;
        std::replace(result.begin(), result.end(), '_', '-');
        // 按GBK字节数截断：ASCII计1字节，UTF-8多字节字符(中文)计2字节，避免截断到半个字符
        if (maxGbkBytes == 0) {
            return result;
        }
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

    void ScreenHAL::FilterUnchangedRequests(std::vector<Request>& requests) {
        std::lock_guard<std::mutex> lock(mCacheMutex);
        requests.erase(std::remove_if(requests.begin(), requests.end(),
                                      [this](const Request& req) {
                                          auto it = mFieldCache.find(req.address);
                                          if (it == mFieldCache.end()) {
                                              return false;
                                          }
                                          return it->second == req.value;
                                      }),
                       requests.end());
    }

    void ScreenHAL::UpdateCache(const std::vector<Request>& requests) {
        std::lock_guard<std::mutex> lock(mCacheMutex);
        for (const auto& req : requests) {
            mFieldCache[req.address] = req.value;
        }
    }

    void ScreenHAL::InvalidateCache() {
        std::lock_guard<std::mutex> lock(mCacheMutex);
        mFieldCache.clear();
        mCurrentPage.reset();
    }

    void ScreenHAL::OnSendComplete(uint64_t sessionId) {
        std::lock_guard<std::mutex> lock(mSessionMutex);
        auto it = mSessions.find(sessionId);
        if (it == mSessions.end()) {
            return;
        }

        auto& session = it->second;
        // 读请求应答走 HandleReadAck，不入写ACK队列
        if (session->isRead) {
            return;
        }

        if (session->sentFrames < session->totalFrames) {
            ++session->sentFrames;
        }

        // 每帧发送完成后入队等待应答
        mPendingAckQueue.push_back(sessionId);

        if (session->sentFrames == session->totalFrames) {
            session->state = SessionState::WaitingAck;
            SLOG_INFO << "Session " << sessionId << " sent " << session->sentFrames << " frames, waiting for ACK";
        }
    }

    void ScreenHAL::OnReceiveData(const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(mReceiveMutex);
        mReceiveBuffer.insert(mReceiveBuffer.end(), data, data + len);

        Frame frame;
        while (mProtocol->Decode(mReceiveBuffer, frame)) {
            if (frame.cmd == 0x83) {
                HandleReadAck(frame);
            } else if (frame.cmd == 0x82 && frame.addr == 0x4f4b) {
                // 迪文屏写应答: cmd=0x82, addr=0x4f4b("OK")
                HandleAckFrame();
            }
        }
    }

    void ScreenHAL::HandleAckFrame() {
        std::lock_guard<std::mutex> lock(mSessionMutex);
        if (mPendingAckQueue.empty()) {
            SLOG_WARN << "Received ACK but no pending session";
            return;
        }

        auto sessionId = mPendingAckQueue.front();
        mPendingAckQueue.pop_front();

        auto it = mSessions.find(sessionId);
        if (it == mSessions.end()) {
            return;
        }

        auto& session = it->second;
        ++session->ackedFrames;

        // 所有帧都收到应答，会话完成
        if (session->ackedFrames == session->totalFrames) {
            session->state = SessionState::Completed;
            session->promise.set_value({ScreenResult::OK, {}});
            SLOG_INFO << "Session " << sessionId << " completed";
        }
    }

    void ScreenHAL::HandleReadAck(const Frame& frame) {
        std::lock_guard<std::mutex> lock(mSessionMutex);
        for (auto& [id, session] : mSessions) {
            if (session->isRead && session->expectedAddr == frame.addr) {
                session->readData = frame.data;
                session->state = SessionState::Completed;
                session->promise.set_value({ScreenResult::OK, std::move(session->readData)});
                SLOG_INFO << "Read session " << id << " completed";
                return;
            }
        }
        SLOG_WARN << "Received read ACK addr=0x" << std::hex << frame.addr << " but no pending read session";
    }

    bool ScreenHAL::ParseLibId(const std::string& filePath, uint16_t& outId) {
        auto name = Basename(filePath);
        // 文件名前缀数字为库号，不足两位前补0（00.D6A ~ 43.D6A）
        if (name.size() < 2 || !std::isdigit(static_cast<unsigned char>(name[0]))) {
            return false;
        }
        size_t idx = 0;
        uint32_t id = 0;
        while (idx < name.size() && std::isdigit(static_cast<unsigned char>(name[idx]))) {
            id = id * 10 + static_cast<uint32_t>(name[idx] - '0');
            if (id > 0xFFFF) {
                return false;
            }
            ++idx;
        }
        if (idx == 0) {
            return false;
        }
        outId = static_cast<uint16_t>(id);
        return true;
    }

    bool ScreenHAL::PollFlashComplete(uint32_t timeoutMs) {
        // 触发后的统一状态轮询：读到 done(0x00) 即成功，读到 busy(0x5A) 继续等，超时失败。
        // 不强制要求先经历 busy 阶段——Flash 写入可能极快直接完成，或该地址无需写入，
        // 状态一直为 done，若强制等 busy 会误判超时（实测 block 0x153 即此情况）。
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        // 读1字(0x01)，迪文屏返回3字节: readLen回显 + 状态2字节
        // 更新中: 01 5A 02, 完成: 01 00 02
        auto readFrame = DiwenProtocol::BuildReadFrame(Addr::FlashTriggerAddr, 0x01);
        int pollCount = 0;
        bool wasBusy = false;

        while (std::chrono::steady_clock::now() < deadline) {
            std::vector<uint8_t> resp;
            auto ret = SendReadRequest(readFrame, Addr::FlashTriggerAddr, resp, 1000);
            ++pollCount;
            if (ret != ScreenResult::OK) {
                SLOG_WARN << "Upgrade: poll#" << pollCount << " read failed ret=" << static_cast<int>(ret);
                continue;
            }

            if (resp.size() >= 3) {
                if (resp[1] == FlashStatusDone) {
                    SLOG_INFO << "Upgrade: flash complete after " << pollCount << " polls"
                              << (wasBusy ? " (was busy)" : " (direct done)");
                    return true;
                }
                if (resp[1] == FlashStatusBusy) {
                    if (!wasBusy) {
                        SLOG_INFO << "Upgrade: flash busy detected at poll#" << pollCount;
                        wasBusy = true;
                    }
                } else {
                    SLOG_WARN << "Upgrade: unexpected flash status byte[1]=0x" << std::hex << static_cast<int>(resp[1]);
                }
            } else {
                SLOG_WARN << "Upgrade: short response " << resp.size() << " bytes";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        SLOG_ERROR << "Upgrade: flash complete poll timeout after " << timeoutMs << "ms, " << pollCount << " polls";
        return false;
    }

    ScreenResult ScreenHAL::TriggerFlashWrite(uint16_t blockAddr, uint32_t timeoutMsPerOp) {
        SLOG_INFO << "Upgrade: trigger flash write, blockAddr=0x" << std::hex << blockAddr
                  << " (lib=" << (blockAddr / 8) << " block=" << (blockAddr % 8) << ")";

        // 触发帧前清空接收缓冲残留字节，避免后续读应答解析错位
        {
            std::lock_guard<std::mutex> lock(mReceiveMutex);
            mReceiveBuffer.clear();
        }

        // 迪文T5L Flash触发帧载荷(12字节):
        // 5A 02 + 块地址(2B) + 数据起始地址(2B=0x8000) + 延时(2B) + 未定义(4B)
        // 块地址 = libId*8 + blockIndex (每个libId占8个32KB=256KB)
        // D5~D4延时取官方串口烧录工具抓包值0x1770(6000ms)，过小会导致Flash未完成时DGUS恢复刷新
        std::array<uint8_t, 12> payload = {0x5A,
                                           0x02,
                                           static_cast<uint8_t>((blockAddr >> 8) & 0xFF),
                                           static_cast<uint8_t>(blockAddr & 0xFF),
                                           0x80,
                                           0x00,
                                           0x17,
                                           0x70,
                                           0x00,
                                           0x00,
                                           0x00,
                                           0x00};
        auto frame = DiwenProtocol::BuildWriteFrame(Addr::FlashTriggerAddr, payload.data(), payload.size());

        auto sid = mNextSessionId.fetch_add(1, std::memory_order_relaxed);
        std::vector<std::pair<uint64_t, std::vector<uint8_t>>> frames;
        frames.emplace_back(sid, std::move(frame));
        auto ackRet = SendRequests(std::move(frames), 2000);
        if (ackRet != ScreenResult::OK) {
            SLOG_ERROR << "Upgrade: trigger frame ACK failed, ret=" << static_cast<int>(ackRet) << " blockAddr=0x"
                       << std::hex << blockAddr;
            return ScreenResult::UpgradeFailed;
        }

        // ACK后直接轮询完成状态：done 即成功，busy 继续等，不强制要求先经历 busy 阶段
        if (!PollFlashComplete(timeoutMsPerOp)) {
            SLOG_ERROR << "Upgrade: flash complete poll failed, blockAddr=0x" << std::hex << blockAddr;
            return ScreenResult::UpgradeFailed;
        }

        SLOG_INFO << "Upgrade: flash write done, blockAddr=0x" << std::hex << blockAddr;
        return ScreenResult::OK;
    }

    ScreenResult ScreenHAL::SendDataChunks(const std::vector<uint8_t>& fileData, size_t offset, uint16_t& addr,
                                           uint32_t timeoutMsPerOp) {
        size_t blockRemaining = std::min(fileData.size() - offset, BlockThreshold);
        size_t chunkCount = (blockRemaining + BytesPerChunk - 1) / BytesPerChunk;

        SLOG_INFO << "Upgrade: sending " << chunkCount << " chunks (" << blockRemaining << "B) at offset " << offset;

        constexpr int maxRetries = 3;
        constexpr uint32_t interChunkDelayMs = 20;

        auto sendStart = std::chrono::steady_clock::now();
        uint16_t currentAddr = addr;
        for (size_t i = 0; i < chunkCount; ++i) {
            size_t chunkOffset = i * BytesPerChunk;
            size_t chunkLen = std::min(BytesPerChunk, blockRemaining - chunkOffset);
            auto frame = DiwenProtocol::BuildWriteFrame(currentAddr, fileData.data() + offset + chunkOffset, chunkLen);

            ScreenResult ret = ScreenResult::Timeout;
            for (int retry = 0; retry < maxRetries; ++retry) {
                auto sessionId = mNextSessionId.fetch_add(1, std::memory_order_relaxed);
                std::vector<std::pair<uint64_t, std::vector<uint8_t>>> frames;
                frames.emplace_back(sessionId, frame);
                ret = SendRequests(std::move(frames), timeoutMsPerOp);
                if (ret == ScreenResult::OK) {
                    break;
                }
                if (retry < maxRetries - 1) {
                    SLOG_WARN << "Upgrade: chunk " << i << "/" << chunkCount << " retry " << (retry + 1) << "/"
                              << maxRetries << " ret=" << static_cast<int>(ret);
                }
            }

            if (ret != ScreenResult::OK) {
                auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - sendStart)
                        .count();
                SLOG_ERROR << "Upgrade: chunk " << i << "/" << chunkCount << " failed after " << maxRetries
                           << " retries, ret=" << static_cast<int>(ret) << " elapsed=" << elapsed << "ms";
                return ret;
            }

            currentAddr += AddrStepPerChunk;
            // 给屏幕处理上一帧的时间，避免缓冲区堆积导致后期 ACK 超时
            if (i + 1 < chunkCount) {
                std::this_thread::sleep_for(std::chrono::milliseconds(interChunkDelayMs));
            }
        }

        addr += static_cast<uint16_t>(chunkCount * AddrStepPerChunk);
        return ScreenResult::OK;
    }

    std::vector<std::string> ScreenHAL::SortUpgradeFiles(const std::vector<std::string>& filePaths) {
        // 大文件先升级
        auto sorted = filePaths;
        std::sort(sorted.begin(), sorted.end(), [](const std::string& lhs, const std::string& rhs) {
            auto fileSize = [](const std::string& path) -> long {
                std::ifstream f(path, std::ios::binary | std::ios::ate);
                return f ? static_cast<long>(f.tellg()) : -1;
            };
            long ls = fileSize(lhs);
            long rs = fileSize(rhs);
            if (ls != rs) {
                return ls > rs;
            }
            return Basename(lhs) < Basename(rhs);
        });
        return sorted;
    }

    ScreenResult ScreenHAL::UpgradeSingleFile(const std::string& filePath, uint32_t timeoutMsPerOp) {
        uint16_t libId = 0;
        if (!ParseLibId(filePath, libId)) {
            SLOG_ERROR << "Upgrade: invalid file name: " << filePath;
            return ScreenResult::UpgradeFailed;
        }

        std::ifstream ifs(filePath, std::ios::binary);
        if (!ifs.is_open()) {
            SLOG_ERROR << "Upgrade: file not found: " << filePath;
            return ScreenResult::UpgradeFailed;
        }
        std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        if (fileData.empty()) {
            SLOG_ERROR << "Upgrade: empty file: " << filePath;
            return ScreenResult::UpgradeFailed;
        }

        // 文件开始前清空接收缓冲：前一文件的残留应答字节会导致当前文件状态读取解析错位
        {
            std::lock_guard<std::mutex> lock(mReceiveMutex);
            mReceiveBuffer.clear();
        }

        size_t totalBlocks = (fileData.size() + BlockThreshold - 1) / BlockThreshold;
        // 起始块地址 = libId * 8，大文件按块线性递增（官方协议允许跨 lib 连续写入）
        uint16_t baseBlockAddr = static_cast<uint16_t>(libId) * 8;
        SLOG_INFO << "Upgrade: file=" << filePath << " lib=" << libId << " size=" << fileData.size()
                  << " blocks=" << totalBlocks << " baseBlockAddr=0x" << std::hex << baseBlockAddr;

        // Flash缓存地址从0x8000起，每包+0x78；超过32KB需分块触发Flash写入
        uint16_t addr = Addr::FlashCacheBase;
        size_t offset = 0;
        size_t blockIndex = 0;
        while (offset < fileData.size()) {
            if (offset > 0 && offset % BlockThreshold == 0) {
                if (TriggerFlashWrite(baseBlockAddr + static_cast<uint16_t>(blockIndex), 30000) != ScreenResult::OK) {
                    return ScreenResult::UpgradeFailed;
                }
                ++blockIndex;
                addr = Addr::FlashCacheBase;
            }

            auto ret = SendDataChunks(fileData, offset, addr, timeoutMsPerOp);
            if (ret != ScreenResult::OK) {
                return ret;
            }

            offset += BlockThreshold;
        }

        // 最后一块的Flash写入
        return TriggerFlashWrite(baseBlockAddr + static_cast<uint16_t>(blockIndex), 30000);
    }

    ScreenResult ScreenHAL::UpgradeFirmware(const std::vector<std::string>& filePaths, uint32_t timeoutMsPerOp) {
        if (!mInitialized.load(std::memory_order_acquire)) {
            return ScreenResult::NotInitialized;
        }
        if (filePaths.empty()) {
            return ScreenResult::InvalidParameter;
        }

        bool expected = false;
        if (!mUpgradeInProgress.compare_exchange_strong(expected, true)) {
            return ScreenResult::UpgradeInProgress;
        }

        {
            std::lock_guard<std::mutex> rlock(mReceiveMutex);
            mReceiveBuffer.clear();
        }
        {
            std::lock_guard<std::mutex> slock(mSessionMutex);
            mSessions.clear();
            mPendingAckQueue.clear();
        }

        // 预检：上次升级异常退出可能令 Flash 仍处于 busy，等待其恢复再开始
        if (!WaitForFlashIdle(30000)) {
            mUpgradeInProgress.store(false, std::memory_order_release);
            return ScreenResult::UpgradeFailed;
        }

        {
            auto stopFrame =
                DiwenProtocol::BuildWriteFrame(Addr::DgusStopEnable, DgusStopPayload.data(), DgusStopPayload.size());
            auto stopSid = mNextSessionId.fetch_add(1, std::memory_order_relaxed);
            std::vector<std::pair<uint64_t, std::vector<uint8_t>>> stopFrames;
            stopFrames.emplace_back(stopSid, std::move(stopFrame));
            auto stopRet = SendRequests(std::move(stopFrames), 2000);
            if (stopRet != ScreenResult::OK) {
                SLOG_ERROR << "Upgrade: stop DGUS refresh failed, ret=" << static_cast<int>(stopRet);
                mUpgradeInProgress.store(false, std::memory_order_release);
                return ScreenResult::UpgradeFailed;
            }
            SLOG_INFO << "Upgrade: DGUS refresh and OS core stopped";
        }

        auto sortedPaths = SortUpgradeFiles(filePaths);
        SLOG_INFO << "Upgrade: starting " << sortedPaths.size() << " files";

        ScreenResult result = ScreenResult::OK;
        try {
            for (size_t i = 0; i < sortedPaths.size(); ++i) {
                SLOG_INFO << "Upgrade: file " << (i + 1) << "/" << sortedPaths.size() << " = " << sortedPaths[i];
                result = UpgradeSingleFile(sortedPaths[i], timeoutMsPerOp);
                if (result != ScreenResult::OK) {
                    SLOG_ERROR << "Upgrade: failed at file " << (i + 1) << "/" << sortedPaths.size() << ": "
                               << sortedPaths[i];
                    if (PollFlashComplete(30000)) {
                        SLOG_INFO << "Upgrade: Flash recovered to idle after failure";
                    } else {
                        SLOG_WARN << "Upgrade: Flash still busy after failure recovery wait";
                    }
                    break;
                }
            }
        } catch (const std::exception& e) {
            SLOG_ERROR << "Upgrade: exception: " << e.what();
            result = ScreenResult::TransportError;
        }

        if (result == ScreenResult::OK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5000));
            result = SendReboot(timeoutMsPerOp);
        }

        mUpgradeInProgress.store(false, std::memory_order_release);
        return result;
    }

    bool ScreenHAL::WaitForFlashIdle(uint32_t waitMs) {
        auto readFrame = DiwenProtocol::BuildReadFrame(Addr::FlashTriggerAddr, 0x01);
        auto startTime = std::chrono::steady_clock::now();
        while (true) {
            std::vector<uint8_t> resp;
            // 读取失败保守放行：无应答无法判断状态，不如让后续流程暴露真实问题
            if (SendReadRequest(readFrame, Addr::FlashTriggerAddr, resp, 1000) != ScreenResult::OK || resp.size() < 2 ||
                resp[1] != FlashStatusBusy) {
                return true;
            }
            if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime)
                    .count() >= waitMs) {
                SLOG_ERROR << "Upgrade: Flash still busy after " << waitMs << "ms, abort";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }

    ScreenResult ScreenHAL::SendReboot(uint32_t timeoutMs) {
        auto frame = DiwenProtocol::BuildWriteFrame(Addr::RebootAddr, RebootPayload.data(), RebootPayload.size());
        auto sessionId = mNextSessionId.fetch_add(1, std::memory_order_relaxed);
        std::vector<std::pair<uint64_t, std::vector<uint8_t>>> frames;
        frames.emplace_back(sessionId, std::move(frame));
        SendRequests(std::move(frames), timeoutMs);
        return ScreenResult::OK;
    }

    ScreenResult ScreenHAL::SendRequests(std::vector<std::pair<uint64_t, std::vector<uint8_t>>>&& frames,
                                         uint32_t timeoutMs) {
        if (frames.empty()) {
            return ScreenResult::OK;
        }

        auto sessionId = frames[0].first;
        auto session = std::make_unique<Session>();
        session->id = sessionId;
        session->totalFrames = static_cast<uint32_t>(frames.size());
        auto future = session->promise.get_future();

        {
            std::lock_guard<std::mutex> lock(mSessionMutex);
            mSessions[sessionId] = std::move(session);
        }

        for (auto& [sid, frame] : frames) {
            mTransport->Send(sid, frame);
        }

        auto status = future.wait_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs));
        if (status == std::future_status::timeout) {
            std::lock_guard<std::mutex> lock(mSessionMutex);
            mSessions.erase(sessionId);
            std::erase(mPendingAckQueue, sessionId);
            SLOG_WARN << "Session " << sessionId << " timed out after " << timeoutMs << "ms";
            return ScreenResult::Timeout;
        }

        auto [result, _] = future.get();
        {
            std::lock_guard<std::mutex> lock(mSessionMutex);
            mSessions.erase(sessionId);
        }

        return result;
    }

    ScreenResult ScreenHAL::SendReadRequest(const std::vector<uint8_t>& frame, uint16_t expectedAddr,
                                            std::vector<uint8_t>& outData, uint32_t timeoutMs) {
        auto sessionId = mNextSessionId.fetch_add(1, std::memory_order_relaxed);
        auto session = std::make_unique<Session>();
        session->id = sessionId;
        session->isRead = true;
        session->expectedAddr = expectedAddr;
        auto future = session->promise.get_future();

        {
            std::lock_guard<std::mutex> lock(mSessionMutex);
            mSessions[sessionId] = std::move(session);
        }

        if (!mTransport->Send(sessionId, frame)) {
            std::lock_guard<std::mutex> lock(mSessionMutex);
            mSessions.erase(sessionId);
            return ScreenResult::TransportError;
        }

        auto status = future.wait_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs));
        if (status == std::future_status::timeout) {
            {
                std::lock_guard<std::mutex> rlock(mReceiveMutex);
                mReceiveBuffer.clear();
            }
            std::lock_guard<std::mutex> lock(mSessionMutex);
            mSessions.erase(sessionId);
            SLOG_WARN << "Read session " << sessionId << " timed out after " << timeoutMs << "ms";
            return ScreenResult::Timeout;
        }

        auto [result, data] = future.get();
        {
            std::lock_guard<std::mutex> lock(mSessionMutex);
            mSessions.erase(sessionId);
        }

        if (result == ScreenResult::OK) {
            outData = std::move(data);
        }
        return result;
    }

}  // namespace qifeng
