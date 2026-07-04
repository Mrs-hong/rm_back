/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/fingerprint_checker.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include <array>
#include <chrono>
#include <cstddef>

#include "checker/hw/serial_port.h"
#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    void FingerprintChecker::ParseConfig(const Json::Value &j, FingerprintConfig &cfg) {
        cfg.device = j.isMember("device") && j["device"].isString() ? j["device"].asString() : cfg.device;
        cfg.baud = j.isMember("baud") && j["baud"].isInt() ? j["baud"].asInt() : cfg.baud;
        cfg.timeout_ms = j.isMember("timeout_ms") && j["timeout_ms"].isInt() ? j["timeout_ms"].asInt()
                                                                              : cfg.timeout_ms;
    }

    CheckResult FingerprintChecker::Run() {
        CheckResult r(Name());
        auto t0 = std::chrono::steady_clock::now();

        SerialPort sp(mConfig.device, mConfig.baud);
        if (!sp.IsOpen()) {
            // 设备节点缺失：视为未装配，跳过
            r.status = Status::SKIPPED;
            r.message = "device not present: " + mConfig.device;
            r.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - t0)
                                                .count());
            SLOG_INFO << "[fingerprint] " << r.message;
            return r;
        }

        // 握手包示意（常见的指纹模组读指纹特征命令）
        // 实际部署请按模组 datasheet 替换
        const std::array<uint8_t, 12> cmd = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                                             0x01, 0x00, 0x03, 0x01, 0x00, 0x05};
        sp.Write(cmd.data(), cmd.size());

        // 限时读响应
        std::array<uint8_t, 32> buf{};
        ssize_t n = sp.Read(buf.data(), buf.size(), mConfig.timeout_ms);
        r.details.emplace_back("resp_len", std::to_string(n > 0 ? n : 0));

        // 应答中通常含状态码 0x00(成功) 或 0x07(无手指)；只要回包即认为模组在线
        bool alive = (n > 0) && (memchr(buf.data(), 0x00, static_cast<size_t>(n)) ||
                                 memchr(buf.data(), 0x07, static_cast<size_t>(n)));
        r.status = alive ? Status::PASS : Status::WARNING;
        r.message = alive ? "fingerprint module alive" : "no valid response";
        r.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - t0)
                                            .count());
        SLOG_INFO << "[fingerprint] " << r.message << " (" << r.elapsed_ms << "ms)";
        return r;
    }

}  // namespace qifeng::scm
