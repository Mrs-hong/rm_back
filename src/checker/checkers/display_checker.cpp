/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/display_checker.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

#include "checker/hw/drm_device.h"
#include "checker/hw/serial_port.h"
#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    /**
     * @brief 迪文串口屏探测：发送读版本号指令，检查应答帧
     */
    static DisplayInfo ProbeSerialDisplay(const DisplayConfig &cfg) {
        DisplayInfo info;

        SerialPort sp(cfg.device, cfg.baud);
        if (!sp.IsOpen()) {
            info.notes.emplace_back("cannot open serial: " + cfg.device);
            return info;
        }
        info.device = cfg.device;

        // 迪文探测：写入待机控制寄存器（地址 0x0084, 值 0x0000=唤醒）
        // 指令: 5A A5 07 82 00 84 5A 01 00 00
        // 帧头=5A A5, 长度=07, 命令=82(写寄存器), 地址=00 84, 写入模式=5A 01, 数据=00 00(唤醒)
        const std::array<uint8_t, 10> cmd = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x00};
        sp.Write(cmd.data(), cmd.size());

        std::array<uint8_t, 64> buf = {};
        ssize_t n = sp.Read(buf.data(), buf.size(), cfg.timeout_ms);
        if (n >= 6 && buf[0] == 0x5A && buf[1] == 0xA5 && buf[3] == 0x82) {
            // 有效写应答: 5A A5 03 82 4F 4B ("OK")
            info.connected = true;
            info.notes.emplace_back("dwin display responded: " + std::to_string(n) + " bytes");
        } else {
            info.notes.emplace_back("dwin display no response (got " + std::to_string(n) + " bytes)");
        }
        return info;
    }

    void DisplayChecker::ParseConfig(const Json::Value &j, DisplayConfig &cfg) {
        if (j.isMember("type") && j["type"].isString()) {
            cfg.type = j["type"].asString();
        }
        cfg.dri_path = j.isMember("dri_path") && j["dri_path"].isString() ? j["dri_path"].asString() : cfg.dri_path;
        cfg.max_card_index = j.isMember("max_card_index") && j["max_card_index"].isInt() ? j["max_card_index"].asInt()
                                                                                         : cfg.max_card_index;
        if (j.isMember("device") && j["device"].isString()) {
            cfg.device = j["device"].asString();
        }
        if (j.isMember("baud") && j["baud"].isInt()) {
            cfg.baud = j["baud"].asInt();
        }
        if (j.isMember("timeout_ms") && j["timeout_ms"].isInt()) {
            cfg.timeout_ms = j["timeout_ms"].asInt();
        }
    }

    CheckResult DisplayChecker::Run() {
        CheckResult r(Name());
        auto t0 = std::chrono::steady_clock::now();

        // 根据 type 选择探测方式：serial(迪文串口屏) 或 drm(HDMI/DP)
        DisplayInfo info;
        if (mConfig.type == "serial") {
            info = ProbeSerialDisplay(mConfig);
        } else {
            info = ProbeDisplay(mConfig);
        }

        r.details.emplace_back("type", mConfig.type);
        r.details.emplace_back("device", info.device.empty() ? "none" : info.device);
        r.details.emplace_back("connected", info.connected ? "yes" : "no");
        for (const auto &n : info.notes) {
            r.details.emplace_back("note", n);
        }

        if (info.device.empty()) {
            r.status = Status::WARNING;
            r.message = "no display device";
        } else if (info.connected) {
            r.status = Status::PASS;
            r.message = "display connected (" + mConfig.type + ")";
        } else {
            r.status = Status::WARNING;
            r.message = "display not connected (" + mConfig.type + ")";
        }
        r.elapsed_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
        SLOG_INFO << "[display] " << r.message << " (" << r.elapsed_ms << "ms)";
        return r;
    }

}  // namespace qifeng::scm
