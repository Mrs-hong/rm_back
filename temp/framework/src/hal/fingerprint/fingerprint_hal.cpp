/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <chrono>
#include <cstdint>
#include <thread>

#include "common/logger.h"
#include "hal/fingerprint/fingerprint_hal.h"
#include "hal/fingerprint/protocol/hlk_code.h"
#include "hal/fingerprint/protocol/hlk_protocol.h"
#include "hal/fingerprint/transport/serial_transport.h"

namespace qifeng {

    FingerprintHAL::~FingerprintHAL() {
        Release();
    }

    FingerprintResult FingerprintHAL::Init(const FingerprintConfig& config) {
        if (mInitialized.load(std::memory_order_acquire)) {
            SLOG_WARN << "FingerprintHAL already initialized";
            return FingerprintResult::OK;
        }

        switch (config.protocolType) {
            case FingerprintProtocolType::Hlk:
                mProtocol = std::make_unique<HlkProtocol>();
                break;
            default:
                SLOG_ERROR << "Unsupported fingerprint protocol type";
                return FingerprintResult::InvalidParameter;
        }

        switch (config.transportType) {
            case FingerprintTransportType::Serial: {
                FpSerialTransportConfig transportCfg;
                transportCfg.port = config.transport.port;
                transportCfg.baudRate = config.transport.baudRate;
                auto transport = std::make_unique<FpSerialTransport>(transportCfg);
                transport->SetSendCompleteCallback([this](uint64_t sessionId) { OnSendComplete(sessionId); });
                transport->SetReceiveCallback([this](const uint8_t* data, size_t len) { OnReceiveData(data, len); });
                transport->SetErrorCallback([this]() {
                    SLOG_ERROR << "FpHAL: transport error, connection lost";
                    mInitialized.store(false, std::memory_order_release);
                    mStateCV.notify_all();
                    mEventQueue.Stop();
                });
                if (!transport->Init()) {
                    SLOG_ERROR << "Failed to init fingerprint serial transport";
                    return FingerprintResult::InvalidParameter;
                }
                mTransport = std::move(transport);
                break;
            }
            default:
                SLOG_ERROR << "Unsupported fingerprint transport type";
                return FingerprintResult::InvalidParameter;
        }

        mInitialized.store(true, std::memory_order_release);
        mCallbackThread = std::thread(&FingerprintHAL::EventCallbackLoop, this);
        mAutoDetectThread = std::thread(&FingerprintHAL::AutoDetectLoop, this);
        SLOG_INFO << "FingerprintHAL initialized";
        return FingerprintResult::OK;
    }

    void FingerprintHAL::Release() {
        if (!mInitialized.load(std::memory_order_acquire)) {
            return;
        }
        mInitialized.store(false, std::memory_order_release);
        mStateCV.notify_all();
        if (mAutoDetectThread.joinable()) {
            mAutoDetectThread.join();
        }
        mEventQueue.Stop();
        if (mCallbackThread.joinable()) {
            mCallbackThread.join();
        }
        if (mTransport) {
            mTransport->Release();
            mTransport.reset();
        }
        mProtocol.reset();
        {
            std::lock_guard<std::mutex> lock(mReceiveMutex);
            mReceiveBuffer.clear();
        }
        {
            std::lock_guard<std::mutex> lock(mStateMutex);
            mState = FpHalState::Idle;
        }
        SLOG_INFO << "FingerprintHAL released";
    }

    bool FingerprintHAL::SetCallback(FingerprintCallback callback) {
        if (!callback) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mCallbackMutex);
        mCallback = std::move(callback);
        return true;
    }

    FingerprintResult FingerprintHAL::Enroll(uint8_t regIdx, RegisterResult& result, uint32_t timeoutMs) {
        if (GetState() == FpHalState::Verifying) {
            return FingerprintResult::DeviceBusy;
        }
        SetState(FpHalState::Enrolling);
        PostEvent(FingerprintEventType::FingerDown);

        auto ret = WaitFingerPresent(timeoutMs);
        if (ret != FingerprintResult::OK) {
            return ret;
        }

        ret = RegisterFingerprint(regIdx, timeoutMs);
        if (ret != FingerprintResult::OK) {
            return ret;
        }

        auto queryDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(KQueryDelayMs));
        while (true) {
            if (GetState() != FpHalState::Enrolling) {
                return FingerprintResult::Canceled;
            }
            const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         queryDeadline - std::chrono::steady_clock::now())
                                         .count();
            if (remainingMs <= 0) {
                ret = FingerprintResult::Timeout;
                break;
            }
            ret = QueryRegisterResult(result, static_cast<uint32_t>(remainingMs));
            if (ret != FingerprintResult::DeviceBusy) {
                break;
            }
            if (std::chrono::steady_clock::now() >= queryDeadline) {
                ret = FingerprintResult::Timeout;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(KQueryRetryIntervalMs));
        }
        if (ret != FingerprintResult::OK) {
            return ret;
        }
        result.fingerId += 1;

        PostEvent(FingerprintEventType::FingerUp);
        ret = WaitFingerRelease(timeoutMs);
        if (ret != FingerprintResult::OK) {
            return ret;
        }

        return FingerprintResult::OK;
    }

    FingerprintResult FingerprintHAL::Save(uint16_t fingerId, SaveResult& result, uint32_t timeoutMs) {
        auto ret = SaveFingerprintTemplate(fingerId, timeoutMs);
        SetState(FpHalState::Idle);
        if (ret != FingerprintResult::OK) {
            return ret;
        }
        result.fingerId = fingerId;
        return ret;
    }

    FingerprintResult FingerprintHAL::DeleteFinger(uint32_t fingerId, DeleteResult& result, uint32_t timeoutMs) {
        fingerId -= 1;
        auto ret = Delete(fingerId, timeoutMs);
        if (ret != FingerprintResult::OK) {
            return ret;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(KQueryDelayMs));
        return QueryDeleteResult(result, timeoutMs);
    }

    FingerprintResult FingerprintHAL::Verify(MatchResult& result, uint32_t timeoutMs) {
        if (GetState() != FpHalState::Idle) {
            return FingerprintResult::DeviceBusy;
        }
        SetState(FpHalState::Verifying);

        // 识别
        auto ret = MatchFingerprint(timeoutMs);
        if (ret != FingerprintResult::OK) {
            SetState(FpHalState::Idle);
            return ret;
        }

        // 查询
        std::this_thread::sleep_for(std::chrono::milliseconds(KQueryDelayMs));
        // 取消可能落在查询延时窗口内，状态已被 Cancel 恢复为 Idle，无需再发送查询
        if (GetState() != FpHalState::Verifying) {
            return FingerprintResult::Canceled;
        }
        ret = QueryMatchResult(result, timeoutMs);
        if (ret != FingerprintResult::OK) {
            SetState(FpHalState::Idle);
            return ret;
        }

        // 自学习
        if (result.matchId != 0xFFFF) {
            SelfLearn(result.matchId, timeoutMs);
        }

        SetState(FpHalState::Idle);
        return ret;
    }

    FingerprintResult FingerprintHAL::Cancel(uint32_t timeoutMs) {
        InterruptActiveCmd();
        auto ret = CancelOperation(timeoutMs);
        if (ret != FingerprintResult::OK) {
            SLOG_WARN << "FpHAL: cancel operation failed, ret=" << static_cast<int>(ret)
                      << ", force restoring Idle to avoid deadlock";
        }
        SetState(FpHalState::Idle);
        return ret;
    }

    FingerprintResult FingerprintHAL::SetLed(FingerprintLedState state, uint32_t timeoutMs) {
        return SetLedControl(state, timeoutMs);
    }

    FingerprintResult FingerprintHAL::DetectFinger(bool& detected, uint32_t timeoutMs) {
        Request req;
        req.cmd = HlkCmd::FingerStatus;
        Frame response;
        auto result = SendAndWait(req, response, timeoutMs);
        if (result != FingerprintResult::OK) {
            return result;
        }
        if (response.errorCode != HlkResp::Ok) {
            return MapDeviceError(response.errorCode);
        }
        detected = !response.data.empty() && (response.data[0] != 0);
        return FingerprintResult::OK;
    }

    FingerprintResult FingerprintHAL::WaitFingerPresent(uint32_t timeoutMs) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true) {
            if (GetState() != FpHalState::Enrolling) {
                return FingerprintResult::Canceled;
            }
            bool detected = false;
            auto ret = DetectFinger(detected, timeoutMs);
            if (ret != FingerprintResult::OK && ret != FingerprintResult::DeviceBusy) {
                return ret;
            }
            if (ret == FingerprintResult::OK && detected) {
                return FingerprintResult::OK;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return FingerprintResult::Timeout;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(KFingerPollIntervalMs));
        }
    }

    FingerprintResult FingerprintHAL::WaitFingerRelease(uint32_t timeoutMs) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true) {
            if (GetState() != FpHalState::Enrolling) {
                return FingerprintResult::Canceled;
            }
            bool detected = false;
            auto ret = DetectFinger(detected, timeoutMs);
            if (ret != FingerprintResult::OK && ret != FingerprintResult::DeviceBusy) {
                return ret;
            }
            if (ret == FingerprintResult::OK && !detected) {
                return FingerprintResult::OK;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return FingerprintResult::Timeout;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(KFingerPollIntervalMs));
        }
    }

    FingerprintResult FingerprintHAL::RegisterFingerprint(uint8_t regIdx, uint32_t timeoutMs) {
        Request req;
        req.cmd = HlkCmd::Enroll;
        req.data.push_back(regIdx);
        Frame response;
        auto result = SendAndWait(req, response, timeoutMs);
        if (result != FingerprintResult::OK) {
            return result;
        }
        return MapDeviceError(response.errorCode);
    }

    FingerprintResult FingerprintHAL::QueryRegisterResult(RegisterResult& regResult, uint32_t timeoutMs) {
        Request req;
        req.cmd = HlkCmd::EnrollQuery;
        Frame response;
        auto result = SendAndWait(req, response, timeoutMs);
        if (result != FingerprintResult::OK) {
            return result;
        }
        if (response.data.size() >= 3) {
            regResult.fingerId = ParseBE16(response.data.data());
            regResult.progress = response.data[2];
        }
        return MapDeviceError(response.errorCode);
    }

    FingerprintResult FingerprintHAL::SaveFingerprintTemplate(uint16_t fingerId, uint32_t timeoutMs) {
        Request req;
        req.cmd = HlkCmd::Save;
        uint16_t slotId = fingerId - 1;
        req.data.push_back(static_cast<uint8_t>((slotId >> 8) & 0xFF));
        req.data.push_back(static_cast<uint8_t>(slotId & 0xFF));
        Frame response;
        auto result = SendAndWait(req, response, timeoutMs);
        if (result != FingerprintResult::OK) {
            return result;
        }
        return MapDeviceError(response.errorCode);
    }

    FingerprintResult FingerprintHAL::QuerySaveResult(SaveResult& saveResult, uint32_t timeoutMs) {
        Request req;
        req.cmd = HlkCmd::SaveQuery;
        Frame response;
        auto result = SendAndWait(req, response, timeoutMs);
        if (result != FingerprintResult::OK) {
            return result;
        }
        if (response.data.size() >= 2) {
            saveResult.fingerId = ParseBE16(response.data.data());
        }
        return MapDeviceError(response.errorCode);
    }

    FingerprintResult FingerprintHAL::MatchFingerprint(uint32_t timeoutMs) {
        Request req;
        req.cmd = HlkCmd::Verify;
        Frame response;
        auto result = SendAndWait(req, response, timeoutMs);
        if (result != FingerprintResult::OK) {
            return result;
        }
        return MapDeviceError(response.errorCode);
    }

    FingerprintResult FingerprintHAL::QueryMatchResult(MatchResult& matchResult, uint32_t timeoutMs) {
        Request req;
        req.cmd = HlkCmd::VerifyQuery;
        Frame response;
        auto result = SendAndWait(req, response, timeoutMs);
        if (result != FingerprintResult::OK) {
            return result;
        }
        if (response.data.size() >= 6) {
            matchResult.matchResult = ParseBE16(response.data.data());
            matchResult.matchScore = ParseBE16(response.data.data() + 2);
            matchResult.matchId = ParseBE16(response.data.data() + 4);
            if (matchResult.matchId != 0xFFFF) {
                matchResult.matchId += 1;
            }
        }
        return MapDeviceError(response.errorCode);
    }

    FingerprintResult FingerprintHAL::Delete(uint32_t fingerId, uint32_t timeoutMs) {
        Request req;
        req.cmd = HlkCmd::DeleteFinger;
        req.data.push_back(0x00);
        req.data.push_back(static_cast<uint8_t>((fingerId >> 8) & 0xFF));
        req.data.push_back(static_cast<uint8_t>(fingerId & 0xFF));
        Frame response;
        auto result = SendAndWait(req, response, timeoutMs);
        if (result != FingerprintResult::OK) {
            return result;
        }
        // 删除不存在的ID视为幂等成功
        if (response.errorCode == HlkResp::InvalidId) {
            return FingerprintResult::OK;
        }
        return MapDeviceError(response.errorCode);
    }

    FingerprintResult FingerprintHAL::QueryDeleteResult(DeleteResult& deleteResult, uint32_t timeoutMs) {
        Request req;
        req.cmd = HlkCmd::DeleteFingerQuery;
        Frame response;
        auto result = SendAndWait(req, response, timeoutMs);
        if (result != FingerprintResult::OK) {
            return result;
        }
        if (response.data.size() >= 2) {
            deleteResult.fingerId = ParseBE16(response.data.data());
        }
        // 删除不存在的ID视为幂等成功
        if (response.errorCode == HlkResp::InvalidId) {
            return FingerprintResult::OK;
        }
        return MapDeviceError(response.errorCode);
    }

    FingerprintResult FingerprintHAL::CancelOperation(uint32_t timeoutMs) {
        Request req;
        req.cmd = HlkCmd::Cancel;
        Frame response;
        auto result = SendAndWait(req, response, timeoutMs);
        if (result != FingerprintResult::OK) {
            return result;
        }
        return MapDeviceError(response.errorCode);
    }

    FingerprintResult FingerprintHAL::SetLedControl(FingerprintLedState state, uint32_t timeoutMs) {
        struct LedCmd {
            uint8_t mode, color, p1, p2, p3;
        };
        static constexpr LedCmd kStateMap[] = {
            {0, 0, 0, 0, 0},      // Off：关闭
            {1, 2, 0, 0, 0},      // Red：红色常亮
            {1, 1, 0, 0, 0},      // Green：绿色常亮
            {3, 4, 100, 0, 100},  // BlueBreath：PWM 呼吸灯，周期2S
        };
        const auto idx = static_cast<size_t>(state);
        if (idx >= sizeof(kStateMap) / sizeof(kStateMap[0])) {
            return FingerprintResult::InvalidParameter;
        }
        const auto& s = kStateMap[idx];

        Request req;
        req.cmd = HlkCmd::LedControl;
        req.data = {s.mode, s.color, s.p1, s.p2, s.p3};
        Frame response;
        auto result = SendAndWait(req, response, timeoutMs);
        if (result != FingerprintResult::OK) {
            return result;
        }
        return MapDeviceError(response.errorCode);
    }

    FingerprintResult FingerprintHAL::SelfLearn(uint16_t fingerId, uint32_t timeoutMs) {
        Request req;
        req.cmd = HlkCmd::SelfLearn;
        Frame response;
        auto result = SendAndWait(req, response, timeoutMs);
        if (result != FingerprintResult::OK) {
            return result;
        }
        return MapDeviceError(response.errorCode);
    }

    FingerprintResult FingerprintHAL::SendAndWait(const Request& req, Frame& response, uint32_t timeoutMs) {
        if (!mInitialized.load(std::memory_order_acquire)) {
            return FingerprintResult::NotInitialized;
        }
        auto stateResult = GuardCmd(req.cmd);
        if (stateResult != FingerprintResult::OK) {
            SLOG_ERROR << "SendAndWait: GuardCmd failed, cmd=0x" << std::hex << req.cmd
                       << " mState=" << static_cast<int>(mState);
            return stateResult;
        }

        std::vector<uint8_t> bytes;
        if (!mProtocol->Encode(req, bytes)) {
            return FingerprintResult::InvalidParameter;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        std::unique_lock<std::mutex> lock(mChannelMutex);
        if (req.cmd == HlkCmd::FingerStatus) {
            if (!mChannel.IsIdle()) {
                return FingerprintResult::DeviceBusy;
            }
        } else if (!mChannelCV.wait_until(lock, deadline, [this] { return mChannel.IsIdle(); })) {
            SLOG_WARN << "FpHAL: [" << HlkCmd::Name(req.cmd) << "] queue wait timed out after " << timeoutMs << "ms";
            return FingerprintResult::Timeout;
        }

        mChannel = {};
        mChannel.phase = CmdChannel::Phase::Waiting;
        mChannel.cmd = req.cmd;

        if (!mTransport->Send(req.cmd, bytes)) {
            mChannel = {};
            mChannelCV.notify_all();
            return FingerprintResult::QueueFull;
        }

        const bool done = mChannelCV.wait_until(lock, deadline, [this] { return mChannel.IsDone(); });
        FingerprintResult result = FingerprintResult::OK;
        if (mChannel.phase == CmdChannel::Phase::Canceled) {
            SLOG_INFO << "FpHAL: [" << HlkCmd::Name(req.cmd) << "] canceled while waiting";
            result = FingerprintResult::Canceled;
        } else if (!done) {
            SLOG_WARN << "FpHAL: [" << HlkCmd::Name(req.cmd) << "] timed out after " << timeoutMs << "ms";
            result = FingerprintResult::Timeout;
        } else {
            response = std::move(mChannel.response);
        }
        mChannel = {};
        mChannelCV.notify_all();
        return result;
    }

    FingerprintResult FingerprintHAL::GuardCmd(uint16_t cmd) {
        std::lock_guard<std::mutex> lock(mStateMutex);

        // 设备空闲或灯光控制命令，允许执行任意命令
        if (mState == FpHalState::Idle || cmd == HlkCmd::LedControl) {
            return FingerprintResult::OK;
        }

        // 非空闲状态下，仅允许部分命令继续执行
        bool allowed = false;

        // Cancel 命令在任何工作状态下都允许执行
        if (cmd == HlkCmd::Cancel) {
            allowed = true;
        }

        // 录入流程中允许的命令：Enroll、FingerStatus、EnrollQuery、Save、SaveQuery
        if (!allowed && mState == FpHalState::Enrolling) {
            allowed = (cmd == HlkCmd::Enroll) || (cmd == HlkCmd::FingerStatus) || (cmd == HlkCmd::EnrollQuery) ||
                      (cmd == HlkCmd::Save) || (cmd == HlkCmd::SaveQuery);
        }

        // 验证流程中允许的命令：Verify、VerifyQuery、FingerStatus
        if (!allowed && mState == FpHalState::Verifying) {
            allowed = (cmd == HlkCmd::Verify) || (cmd == HlkCmd::VerifyQuery) || 
                        (cmd == HlkCmd::FingerStatus) || (cmd == HlkCmd::SelfLearn);
        }

        // 当前状态下不允许执行该命令
        if (!allowed) {
            return FingerprintResult::DeviceBusy;
        }

        return FingerprintResult::OK;
    }

    void FingerprintHAL::InterruptActiveCmd() {
        std::lock_guard<std::mutex> lock(mChannelMutex);
        // Cancel 命令自身除外，确保模组侧取消指令仍能正常收发
        if (mChannel.phase == CmdChannel::Phase::Waiting && mChannel.cmd != HlkCmd::Cancel) {
            mChannel.phase = CmdChannel::Phase::Canceled;
            mChannelCV.notify_all();
            SLOG_INFO << "FpHAL: [" << HlkCmd::Name(mChannel.cmd) << "] interrupted by cancel";
        }
    }

    void FingerprintHAL::OnSendComplete(uint64_t cmd) {
        std::lock_guard<std::mutex> lock(mChannelMutex);
        if (mChannel.phase == CmdChannel::Phase::Waiting && mChannel.cmd == static_cast<uint16_t>(cmd)
            && mChannel.cmd != HlkCmd::FingerStatus) {
            SLOG_INFO << "FpHAL: [" << HlkCmd::Name(mChannel.cmd) << "] sent";
        }
    }

    void FingerprintHAL::OnReceiveData(  // NOLINT(readability-function-cognitive-complexity)
        const uint8_t* data, size_t len) {
        std::vector<Frame> decodedFrames;
        {
            std::lock_guard<std::mutex> lock(mReceiveMutex);
            mReceiveBuffer.insert(mReceiveBuffer.end(), data, data + len);

            Frame frame;
            while (mProtocol->Decode(mReceiveBuffer, frame)) {
                decodedFrames.push_back(std::move(frame));
            }
        }

        std::lock_guard<std::mutex> channelLock(mChannelMutex);
        for (auto& frame : decodedFrames) {
            // 单命令协议下响应帧须与在途命令匹配，迟到/异常帧直接丢弃
            if (mChannel.phase == CmdChannel::Phase::Waiting && mChannel.cmd == frame.cmd) {
                if (frame.cmd != HlkCmd::FingerStatus) {
                    SLOG_INFO << "FpHAL: [" << HlkCmd::Name(frame.cmd) << "] completed";
                }
                mChannel.response = std::move(frame);
                mChannel.phase = CmdChannel::Phase::Completed;
                mChannelCV.notify_all();
            } else {
                SLOG_WARN << "FpHAL: unexpected frame cmd=0x" << std::hex << frame.cmd << " ["
                          << HlkCmd::Name(frame.cmd) << "]";
            }
        }
    }

    void FingerprintHAL::EventCallbackLoop() {
        while (mInitialized.load(std::memory_order_acquire)) {
            FingerprintEvent event {};
            if (!mEventQueue.WaitPop(event)) {
                break;
            }
            std::lock_guard<std::mutex> lock(mCallbackMutex);
            if (mCallback) {
                mCallback(event);
            }
        }
    }

    void FingerprintHAL::AutoDetectLoop() {  // NOLINT(readability-function-cognitive-complexity)
        auto fingerDownSince = std::chrono::steady_clock::time_point {};
        enum class Phase : uint8_t { WaitFinger, Timing, WaitRelease };
        Phase phase = Phase::WaitFinger;

        while (mInitialized.load(std::memory_order_acquire)) {
            // 外部录入优先级最高，录入中则清空状态并挂起等待
            if (GetState() == FpHalState::Enrolling) {
                phase = Phase::WaitFinger;
                if (!WaitForIdle()) {
                    return;
                }
                continue;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(KAutoDetectIntervalMs));

            bool detected = false;
            auto detectRet = DetectFinger(detected, KAutoDetectTimeoutMs);
            if (detectRet == FingerprintResult::DeviceBusy) {
                continue;
            }
            if (detectRet != FingerprintResult::OK) {
                phase = Phase::WaitFinger;
                continue;
            }

            switch (phase) {
                case Phase::WaitFinger:
                    if (detected) {
                        fingerDownSince = std::chrono::steady_clock::now();
                        PostEvent(FingerprintEventType::Recognizing);
                        phase = Phase::Timing;
                    }
                    break;

                case Phase::Timing:
                    if (!detected) {
                        PostEvent(FingerprintEventType::RecognizingEnd);
                        phase = Phase::WaitFinger;
                    } else if (std::chrono::steady_clock::now() - fingerDownSince >=
                               std::chrono::milliseconds(KFingerHoldThresholdMs)) {
                        // PostEvent(FingerprintEventType::RecognizingEnd);
                        TriggerRecord();
                        phase = Phase::WaitRelease;
                    }
                    break;

                case Phase::WaitRelease:
                    if (!detected) {
                        phase = Phase::WaitFinger;
                    }
                    break;

                default:
                    break;
            }
        }
    }

    bool FingerprintHAL::WaitForIdle() {
        std::unique_lock<std::mutex> lock(mStateMutex);
        mStateCV.wait(lock,
                      [this] { return !mInitialized.load(std::memory_order_acquire) || mState == FpHalState::Idle; });
        return mInitialized.load(std::memory_order_acquire);
    }

    void FingerprintHAL::TriggerRecord() {
        FingerprintEvent event {};
        event.type = FingerprintEventType::Record;

        MatchResult matchResult {};
        auto ret = Verify(matchResult, KAutoDetectTimeoutMs);
        if (ret == FingerprintResult::OK) {
            event.matched = (matchResult.matchId != 0xFFFF);
            event.fingerId = matchResult.matchId;
            event.score = matchResult.matchScore;
        }

        SLOG_INFO << "FpHAL: post Record event matched=" << event.matched << " fingerId=" << event.fingerId
                  << " score=" << event.score;
        mEventQueue.Push(event);
    }

    void FingerprintHAL::PostEvent(FingerprintEventType type) {
        FingerprintEvent event;
        event.type = type;
        SLOG_INFO << "FpHAL: post event type=" << static_cast<int>(type);
        mEventQueue.Push(event);
    }

    bool FingerprintHAL::SetState(FpHalState state) {
        if (!mInitialized.load(std::memory_order_acquire)) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mStateMutex);
            mState = state;
        }
        if (state == FpHalState::Idle) {
            mStateCV.notify_one();
        }
        return true;
    }

    FpHalState FingerprintHAL::GetState() const {
        std::lock_guard<std::mutex> lock(mStateMutex);
        return mState;
    }

    FingerprintResult FingerprintHAL::MapDeviceError(uint32_t errorCode) {
        switch (errorCode) {
            case HlkResp::Ok:
                return FingerprintResult::OK;

            case HlkResp::Busy:
                SLOG_DEBUG << "FpHAL: device busy, errorCode=0x" << std::hex << errorCode;
                return FingerprintResult::DeviceBusy;

            case HlkResp::DuplicateFinger:
                SLOG_ERROR << "FpHAL: device error, errorCode=0x" << std::hex << errorCode;
                return FingerprintResult::DuplicateFinger;

            case HlkResp::FingerTimeout:
                SLOG_ERROR << "FpHAL: device error, errorCode=0x" << std::hex << errorCode;
                return FingerprintResult::Timeout;

            case HlkResp::SmallContactArea:
                SLOG_ERROR << "FpHAL: device error, errorCode=0x" << std::hex << errorCode;
                return FingerprintResult::SmallContactArea;

            default:
                SLOG_ERROR << "FpHAL: device error, errorCode=0x" << std::hex << errorCode;
                return FingerprintResult::DeviceError;
        }
    }

    uint16_t FingerprintHAL::ParseBE16(const uint8_t* data) {
        return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]));
    }

}  // namespace qifeng
