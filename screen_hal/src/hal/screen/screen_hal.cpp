/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "hal/screen/screen_hal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <tuple>
#include <utility>

#include "common/logger.h"
#include "hal/screen/protocol/addr_map.h"
#include "hal/screen/protocol/diwen_protocol.h"
#include "hal/screen/transport/serial_transport.h"

namespace qifeng {

    namespace {
        constexpr size_t BytesPerChunk = 240;
        constexpr uint16_t AddrStepPerChunk = 0x78;
        constexpr size_t BlockThreshold = 32768;
        constexpr uint8_t FlashStatusBusy = 0x5A;                                   // byte[1]=0x5A 正在写入
        constexpr uint8_t FlashStatusDone = 0x00;                                   // byte[1]=0x00 写入完成
        constexpr std::array<uint8_t, 4> RebootPayload = {0x55, 0xAA, 0x5A, 0xA5};  // 复位指令

        std::string Basename(const std::string& path) {
            auto pos = path.find_last_of("/\\");
            return (pos == std::string::npos) ? path : path.substr(pos + 1);
        }
    }  // namespace

    ScreenEngine::~ScreenEngine() {
        Release();
    }

    ScreenResult ScreenEngine::Init(const DisplayConfig& config) {
        if (mInitialized.load(std::memory_order_acquire)) {
            SLOG_WARN << "ScreenEngine already initialized";
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
                transport->SetSendCompleteCallback(
                    [this](uint64_t sessionId, bool ok) { OnSendComplete(sessionId, ok); });
                transport->SetReceiveCallback([this](const uint8_t* data, size_t len) { OnReceiveData(data, len); });

                if (!transport->Init()) {
                    SLOG_ERROR << "Failed to init serial transport";
                    return ScreenResult::TransportError;
                }
                mTransport = std::move(transport);
                break;
            }
            default:
                SLOG_ERROR << "Unsupported transport type";
                return ScreenResult::InvalidParameter;
        }

        mInitialized.store(true, std::memory_order_release);
        SLOG_INFO << "ScreenEngine initialized";
        return ScreenResult::OK;
    }

    void ScreenEngine::Release() {
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
            std::lock_guard<std::mutex> lock(mTouchMutex);
            mTouchCallback = nullptr;
        }

        mInitialized.store(false, std::memory_order_release);
        SLOG_INFO << "ScreenEngine released";
    }

    // ── 收发原语 ──

    ScreenResult ScreenEngine::SendRequests(const std::vector<Request>& requests, uint32_t timeoutMs) {
        if (!mInitialized.load(std::memory_order_acquire)) {
            return ScreenResult::NotInitialized;
        }
        if (requests.empty()) {
            return ScreenResult::OK;
        }

        // 请求编码：同一会话顺序发送（串口本身有序，屏按序处理）；
        // gapMs 随帧透传，由发送循环在帧间执行（如 0xB0 触控开关需间隔 20ms）
        auto sessionId = mNextSessionId.fetch_add(1, std::memory_order_relaxed);
        std::vector<OutgoingFrame> frames;
        frames.reserve(requests.size());
        for (const auto& req : requests) {
            std::vector<uint8_t> frame;
            if (!mProtocol->Encode(req, frame)) {
                SLOG_ERROR << "Failed to encode request for addr 0x" << std::hex << req.address;
                return ScreenResult::InvalidParameter;
            }
            frames.push_back(OutgoingFrame {sessionId, std::move(frame), req.gapMs});
        }
        return SendFrames(std::move(frames), timeoutMs);
    }

    ScreenResult ScreenEngine::ReadRegister(uint16_t addr, uint8_t readLen, std::vector<uint8_t>& outData,
                                            uint32_t timeoutMs) {
        if (!mInitialized.load(std::memory_order_acquire)) {
            return ScreenResult::NotInitialized;
        }
        auto frame = DiwenProtocol::BuildReadFrame(addr, readLen);
        return SendReadFrame(frame, addr, outData, timeoutMs);
    }

    void ScreenEngine::SetTouchCallback(TouchCallback callback) {
        std::lock_guard<std::mutex> lock(mTouchMutex);
        mTouchCallback = std::move(callback);
    }

    // ── 会话管理与传输 ──

    ScreenResult ScreenEngine::SendFrames(std::vector<OutgoingFrame>&& frames, uint32_t timeoutMs) {
        if (frames.empty()) {
            return ScreenResult::OK;
        }

        auto sessionId = frames[0].sessionId;
        auto session = std::make_unique<Session>();
        session->id = sessionId;
        session->totalFrames = static_cast<uint32_t>(frames.size());
        auto future = session->promise.get_future();

        {
            std::lock_guard<std::mutex> lock(mSessionMutex);
            mSessions[sessionId] = std::move(session);
        }

        for (size_t i = 0; i < frames.size(); ++i) {
            auto& frame = frames[i];
            // 入队失败（队列满/传输已停止）说明该帧永远不会发出，重试无意义 → 立即返回，
            // 避免调用方空等到超时才失败（会话由下次超时重同步统一清理）
            if (!mTransport->Send(frame.sessionId, frame.data)) {
                SLOG_ERROR << "Session " << sessionId << " send failed (transport not ready)";
                std::lock_guard<std::mutex> rlock(mReceiveMutex);
                std::lock_guard<std::mutex> lock(mSessionMutex);
                ResetAckStateLocked(ScreenResult::TransportError);
                return ScreenResult::TransportError;
            }
            // 帧间节流：屏端对控制类指令（如 0xB0 触控开关）要求连续下发间隔 20ms，
            // 太快会来不及逐条处理而漏指令。末帧无需等待（后面没有帧了）
            if (frame.gapMs > 0 && i + 1 < frames.size()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(frame.gapMs));
            }
        }

        auto status = future.wait_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs));
        if (status == std::future_status::timeout) {
            SLOG_WARN << "Session " << sessionId << " timed out after " << timeoutMs << "ms, resyncing ACK state";
            std::lock_guard<std::mutex> rlock(mReceiveMutex);  // 锁序：接收缓冲 → 会话表
            std::lock_guard<std::mutex> lock(mSessionMutex);
            ResetAckStateLocked(ScreenResult::Timeout);
            return ScreenResult::Timeout;
        }

        ScreenResult result = ScreenResult::OK;
        try {
            result = future.get().first;
        } catch (const std::future_error& e) {
            // Release/升级清理会销毁未赋值的 promise → 这里必须兜住，否则异常逃逸到业务线程
            SLOG_WARN << "Session " << sessionId << " aborted: " << e.what();
            result = ScreenResult::TransportError;
        }

        {
            std::lock_guard<std::mutex> lock(mSessionMutex);
            // 正常完成时其队列条目已被 ACK 全部弹出；异常路径下可能仍残留条目，
            // 故意保留作为"迟到应答"的占位（见 ResetAckStateLocked 注释）
            mSessions.erase(sessionId);
        }

        return result;
    }

    ScreenResult ScreenEngine::SendReadFrame(const std::vector<uint8_t>& frame, uint16_t expectedAddr,
                                             std::vector<uint8_t>& outData, uint32_t timeoutMs) {
        outData.clear();  // 失败时不留调用方的陈旧数据
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
            SLOG_WARN << "Read session " << sessionId << " timed out after " << timeoutMs << "ms, resyncing ACK state";
            std::lock_guard<std::mutex> rlock(mReceiveMutex);  // 锁序：接收缓冲 → 会话表
            std::lock_guard<std::mutex> lock(mSessionMutex);
            ResetAckStateLocked(ScreenResult::Timeout);
            return ScreenResult::Timeout;
        }

        auto [result, data] = std::make_pair(ScreenResult::TransportError, std::vector<uint8_t> {});
        try {
            std::tie(result, data) = future.get();
        } catch (const std::future_error& e) {
            // 同 SendFrames：会话被提前销毁时不得让异常逃逸
            SLOG_WARN << "Read session " << sessionId << " aborted: " << e.what();
            result = ScreenResult::TransportError;
        }

        {
            std::lock_guard<std::mutex> lock(mSessionMutex);
            mSessions.erase(sessionId);
        }

        if (result == ScreenResult::OK) {
            outData = std::move(data);
        }
        return result;
    }

    void ScreenEngine::ResetAckStateLocked(ScreenResult reason) {
        // 调用方须持有 mReceiveMutex 与 mSessionMutex（锁序：接收缓冲 → 会话表）
        for (auto& [id, session] : mSessions) {
            if (session->state == SessionState::Completed) {
                continue;  // 结果已交付，不重复赋值（重复 set_value 会抛异常）
            }
            session->state = SessionState::Completed;
            session->promise.set_value({reason, {}});
        }
        mSessions.clear();
        mPendingAckQueue.clear();
        mReceiveBuffer.clear();
    }

    void ScreenEngine::OnSendComplete(uint64_t sessionId, bool ok) {
        std::lock_guard<std::mutex> lock(mSessionMutex);
        auto it = mSessions.find(sessionId);
        if (it == mSessions.end()) {
            return;  // 会话已超时/已被重同步清理
        }

        auto& session = it->second;

        if (!ok) {
            // 写失败：本帧不会到达屏幕（不会有应答），立即以 TransportError 结束会话，
            // 避免调用方空等到超时；该会话此前已写出的帧仍可能回 ACK，
            // 其队列条目刻意保留，用于吸收这些迟到应答（不吃掉后续会话的计数）
            SLOG_ERROR << "Session " << sessionId << " write failed, session aborted";
            if (session->state != SessionState::Completed) {
                session->state = SessionState::Completed;
                session->promise.set_value({ScreenResult::TransportError, {}});
            }
            mSessions.erase(it);
            return;
        }

        // 读请求应答走 HandleReadAck，不入写 ACK 队列
        if (session->isRead) {
            return;
        }

        if (session->sentFrames >= session->totalFrames) {
            return;  // 防御：重复完成回调不产生额外待应答条目
        }
        ++session->sentFrames;
        // 每帧发送完成入队等待应答（一个条目 = 一个已写出、待一个应答的帧）
        mPendingAckQueue.push_back(sessionId);

        if (session->sentFrames == session->totalFrames) {
            session->state = SessionState::WaitingAck;
            SLOG_INFO << "Session " << sessionId << " sent " << session->sentFrames << " frames, waiting for ACK";
        }
    }

    void ScreenEngine::OnReceiveData(const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(mReceiveMutex);
        mReceiveBuffer.insert(mReceiveBuffer.end(), data, data + len);

        Frame frame;
        while (mProtocol->Decode(mReceiveBuffer, frame)) {
            if (frame.cmd == 0x83) {
                // 0x83 帧双重身份：挂起读会话的应答 or 屏主动上传的触控事件。
                // 两者格式完全相同，先按读会话地址匹配，未命中再上抛触控回调
                // （依赖"触控键值 VP 段与主动读地址段分开规划"的工程纪律）。
                if (!HandleReadAck(frame)) {
                    DispatchTouchEvent(frame);
                }
            } else if (frame.cmd == 0x82 && frame.addr == 0x4f4b) {
                // 迪文屏写应答: cmd=0x82, addr=0x4f4b("OK")
                HandleAckFrame();
            }
        }
    }

    void ScreenEngine::HandleAckFrame() {
        std::lock_guard<std::mutex> lock(mSessionMutex);
        if (mPendingAckQueue.empty()) {
            SLOG_WARN << "Received ACK but no pending session";
            return;
        }

        auto sessionId = mPendingAckQueue.front();
        mPendingAckQueue.pop_front();

        auto it = mSessions.find(sessionId);
        if (it == mSessions.end()) {
            // 会话已因超时/写失败销毁：该条目是吸收迟到应答的占位，直接消耗
            return;
        }

        auto& session = it->second;
        if (session->ackedFrames >= session->totalFrames) {
            // 重复应答（屏端多发/占位耗尽）：忽略，避免二次 set_value 抛异常
            SLOG_WARN << "Duplicate ACK for completed session " << sessionId << ", ignored";
            return;
        }
        ++session->ackedFrames;

        // 所有帧都收到应答，会话完成
        if (session->ackedFrames == session->totalFrames) {
            session->state = SessionState::Completed;
            session->promise.set_value({ScreenResult::OK, {}});
            SLOG_INFO << "Session " << sessionId << " completed";
        }
    }

    bool ScreenEngine::HandleReadAck(const Frame& frame) {
        std::lock_guard<std::mutex> lock(mSessionMutex);
        for (auto& [id, session] : mSessions) {
            if (!session->isRead || session->expectedAddr != frame.addr) {
                continue;
            }
            if (session->state == SessionState::Completed) {
                return true;  // 已配对过（重复应答）：吃掉该帧，不重复赋值
            }
            session->state = SessionState::Completed;
            session->promise.set_value({ScreenResult::OK, frame.data});
            SLOG_INFO << "Read session " << id << " completed";
            return true;
        }
        // 未命中挂起读会话：可能是触控上传帧，交由调用方继续上抛
        return false;
    }

    void ScreenEngine::DispatchTouchEvent(const Frame& frame) {
        TouchCallback callback;
        {
            std::lock_guard<std::mutex> lock(mTouchMutex);
            callback = mTouchCallback;
        }
        if (!callback) {
            SLOG_WARN << "Unexpected 0x83 frame addr=0x" << std::hex << frame.addr
                      << " (neither pending read nor touch callback set)";
            return;
        }

        TouchEvent event;
        event.keyVp = frame.addr;
        // data[0] 为字数回显，键值取 data[1..2]（产品协议恒 0x0000）
        if (frame.data.size() >= 3) {
            event.value = static_cast<uint16_t>((frame.data[1] << 8) | frame.data[2]);
        }
        // 事件回调在接收线程上下文执行，线程约束见 SetTouchCallback 注释
        callback(event);
    }

    ScreenResult ScreenEngine::SetTouchEnabled(bool enabled, uint32_t timeoutMs) {
        if (!mInitialized.load(std::memory_order_acquire)) {
            return ScreenResult::NotInitialized;
        }

        // 0x00FC DGUS_STOP_EN 接口（【开发指南】5.1 节）：
        //   写 0x55AA 0x5A5A 停止 DGUS 刷新与触控处理；写 0x0000 0x0000 全部恢复
        Request request;
        request.address = Addr::DgusStopEnable;
        request.value = std::vector<uint16_t> {static_cast<uint16_t>(enabled ? 0x0000 : 0x55AA),
                                               static_cast<uint16_t>(enabled ? 0x0000 : 0x5A5A)};
        auto result = SendRequests({request}, timeoutMs);
        SLOG_INFO << "Touch " << (enabled ? "enabled" : "suspended") << ", ret=" << static_cast<int>(result);
        return result;
    }

    // ── 固件升级 ──

    bool ScreenEngine::ParseLibId(const std::string& filePath, uint16_t& outId) {
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

    bool ScreenEngine::PollFlashComplete(uint32_t timeoutMs) {
        // 触发后的统一状态轮询：读到 done(0x00) 即成功，读到 busy(0x5A) 继续等，超时失败。
        // 不强制要求先经历 busy 阶段——Flash 写入可能极快直接完成，或该地址无需写入，
        // 状态一直为 done，若强制等 busy 会误判超时（实测 block 0x153 即此情况）。
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        // 读1字(0x01)，迪文屏返回3字节: readLen回显 + 状态2字节
        // 更新中: 01 5A 02, 完成: 01 00 02
        std::vector<uint8_t> resp;
        int pollCount = 0;
        bool wasBusy = false;

        while (std::chrono::steady_clock::now() < deadline) {
            auto ret = ReadRegister(Addr::FlashTriggerAddr, 0x01, resp, 1000);
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

    ScreenResult ScreenEngine::TriggerFlashWrite(uint16_t blockAddr, uint32_t timeoutMsPerOp) {
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
        std::vector<OutgoingFrame> frames;
        frames.push_back(OutgoingFrame {sid, std::move(frame), 0});
        auto ackRet = SendFrames(std::move(frames), 2000);
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

    ScreenResult ScreenEngine::SendDataChunks(const std::vector<uint8_t>& fileData, size_t offset, uint16_t& addr,
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
                std::vector<OutgoingFrame> frames;
                frames.push_back(OutgoingFrame {sessionId, frame, 0});
                ret = SendFrames(std::move(frames), timeoutMsPerOp);
                if (ret == ScreenResult::OK) {
                    break;
                }
                if (retry < maxRetries - 1) {
                    SLOG_WARN << "Upgrade: chunk " << i << "/" << chunkCount << " retry " << (retry + 1) << "/"
                              << maxRetries << " ret=" << static_cast<int>(ret);
                }
            }

            if (ret != ScreenResult::OK) {
                auto elapsed = static_cast<long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - sendStart)
                        .count());
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

    std::vector<std::string> ScreenEngine::SortUpgradeFiles(const std::vector<std::string>& filePaths) {
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

    ScreenResult ScreenEngine::UpgradeSingleFile(const std::string& filePath, uint32_t timeoutMsPerOp) {
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

    ScreenResult ScreenEngine::UpgradeFirmware(const std::vector<std::string>& filePaths, uint32_t timeoutMsPerOp) {
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

        // 失败恢复：升级过程会停止 0x00FC（DGUS 刷新与触控），任何失败退出都必须恢复，
        // 否则界面停止刷新且触控失效，只能依靠外部重新上电
        auto restoreDisplay = [this, timeoutMsPerOp]() {
            if (SetTouchEnabled(true, timeoutMsPerOp) != ScreenResult::OK) {
                SLOG_ERROR << "Upgrade: failed to restore DGUS refresh/touch, power cycle may be required";
                return;
            }
            SLOG_INFO << "Upgrade: DGUS refresh and touch restored";
        };

        // 预检：上次升级异常退出可能令 Flash 仍处于 busy，等待其恢复再开始
        if (!WaitForFlashIdle(30000)) {
            mUpgradeInProgress.store(false, std::memory_order_release);
            return ScreenResult::UpgradeFailed;
        }

        // 升级前奏（顺序依据 summary.md 2.3/3.3 规范流程）：
        // 1. 停止触控（0xFC 模式3）：防升级期间触摸产生的上传帧混入应答流干扰 ACK 配对
        if (SetTouchEnabled(false, 2000) != ScreenResult::OK) {
            SLOG_ERROR << "Upgrade: suspend touch failed";
            restoreDisplay();  // 停触控帧可能已部分生效，保守恢复
            mUpgradeInProgress.store(false, std::memory_order_release);
            return ScreenResult::UpgradeFailed;
        }
        SLOG_INFO << "Upgrade: touch suspended";

        // 2. 停止 DGUS 刷新 + OS 核：暂存区 0x8000~0xFFFF 与 SP 属性区重叠，
        //    不停的话 GUI 核会把升级数据当控件属性改写
        {
            Request stopReq;
            stopReq.address = Addr::DgusStopEnable;
            stopReq.value = std::vector<uint16_t> {0x55AA, 0x5A5A};
            auto stopRet = SendRequests({stopReq}, 2000);
            if (stopRet != ScreenResult::OK) {
                SLOG_ERROR << "Upgrade: stop DGUS refresh failed, ret=" << static_cast<int>(stopRet);
                restoreDisplay();
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
        } else {
            // 升级未完成：不重启（避免用半套固件启动），改为恢复 0x00FC 让旧固件继续可用
            restoreDisplay();
        }

        mUpgradeInProgress.store(false, std::memory_order_release);
        return result;
    }

    bool ScreenEngine::WaitForFlashIdle(uint32_t waitMs) {
        std::vector<uint8_t> resp;
        auto startTime = std::chrono::steady_clock::now();
        while (true) {
            // 读取失败保守放行：无应答无法判断状态，不如让后续流程暴露真实问题
            if (ReadRegister(Addr::FlashTriggerAddr, 0x01, resp, 1000) != ScreenResult::OK || resp.size() < 2 ||
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

    ScreenResult ScreenEngine::SendReboot(uint32_t timeoutMs) {
        auto frame = DiwenProtocol::BuildWriteFrame(Addr::RebootAddr, RebootPayload.data(), RebootPayload.size());
        auto sessionId = mNextSessionId.fetch_add(1, std::memory_order_relaxed);
        std::vector<OutgoingFrame> frames;
        frames.push_back(OutgoingFrame {sessionId, std::move(frame), 0});
        SendFrames(std::move(frames), timeoutMs);
        // 屏复位后无应答属正常，不依据应答判定结果
        return ScreenResult::OK;
    }

}  // namespace qifeng
