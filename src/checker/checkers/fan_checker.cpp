/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/fan_checker.h"

#include <cstring>
#include <fstream>
#include <string>

#include <chrono>

#include "qifeng_framework/common/logger.h"

#if defined(CHECKER_HAS_BM_SDK) && CHECKER_HAS_BM_SDK
    #include "bmlib_runtime.h"
#endif

namespace qifeng::scm {

    namespace {

        // Read fan speed (RPM) from sysfs hwmon
        int ReadFanSpeedFromSysfs(const FanConfig &cfg) {
            for (int hwmonIdx = 0; hwmonIdx < cfg.hwmon_max_index; ++hwmonIdx) {
                for (int fanIdx = 1; fanIdx <= cfg.fan_max_index; ++fanIdx) {
                    std::string path = "/sys/class/hwmon/hwmon" + std::to_string(hwmonIdx) +
                                       "/fan" + std::to_string(fanIdx) + "_input";
                    std::ifstream ifs(path);
                    if (ifs.good()) {
                        int rpm = 0;
                        ifs >> rpm;
                        if (rpm > 0) {
                            return rpm;
                        }
                    }
                }
            }
            return -1;
        }

        // Read temperature (C) from sysfs thermal_zone
        // sysfs temp unit is milli-C, divide by 1000
        int ReadTempFromSysfs(const FanConfig &cfg) {
            for (int tzIdx = 0; tzIdx < cfg.thermal_zone_max_index; ++tzIdx) {
                std::string path = "/sys/class/thermal/thermal_zone" + std::to_string(tzIdx) + "/temp";
                std::ifstream ifs(path);
                if (ifs.good()) {
                    int milliC = 0;
                    ifs >> milliC;
                    if (milliC > 0) {
                        return milliC / 1000;
                    }
                }
            }
            return -1;
        }

    }  // namespace

    void FanChecker::ParseConfig(const Json::Value &j, FanConfig &cfg) {
        cfg.max_temp_threshold = j.isMember("max_temp_threshold") && j["max_temp_threshold"].isInt()
                                     ? j["max_temp_threshold"].asInt()
                                     : cfg.max_temp_threshold;
        cfg.hwmon_max_index = j.isMember("hwmon_max_index") && j["hwmon_max_index"].isInt()
                                  ? j["hwmon_max_index"].asInt()
                                  : cfg.hwmon_max_index;
        cfg.fan_max_index =
            j.isMember("fan_max_index") && j["fan_max_index"].isInt() ? j["fan_max_index"].asInt() : cfg.fan_max_index;
        cfg.thermal_zone_max_index = j.isMember("thermal_zone_max_index") && j["thermal_zone_max_index"].isInt()
                                         ? j["thermal_zone_max_index"].asInt()
                                         : cfg.thermal_zone_max_index;
    }

    // NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
    CheckResult FanChecker::Run() {
        CheckResult r(Name());
        auto t0 = std::chrono::steady_clock::now();

        int chipTemp = -1;
        int boardTemp = -1;
        int fanSpeed = -1;
        bool usedSdk = false;

        // 1) Prefer Sophon SDK (BM devices)
#if defined(CHECKER_HAS_BM_SDK) && CHECKER_HAS_BM_SDK
        bm_handle_t handle = nullptr;
        if (bm_dev_request(&handle, 0) == BM_SUCCESS && handle) {
            auto guard = std::unique_ptr<void, void (*)(void*)>(
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                handle, [](void* h) { bm_dev_free(reinterpret_cast<bm_handle_t>(h)); });

            unsigned int val = 0;
            if (bm_get_chip_temp(handle, &val) == BM_SUCCESS) {
                chipTemp = static_cast<int>(val);
            }
            if (bm_get_board_temp(handle, &val) == BM_SUCCESS) {
                boardTemp = static_cast<int>(val);
            }
            if (bm_get_fan_speed(handle, &val) == BM_SUCCESS) {
                fanSpeed = static_cast<int>(val);
            }
            usedSdk = true;
        }
#else
        // BM SDK unavailable, will use sysfs fallback below
#endif

        // 2) Fallback to sysfs when SDK unavailable or read failed
        if (chipTemp < 0) {
            chipTemp = ReadTempFromSysfs(mConfig);
        }
        if (fanSpeed < 0) {
            fanSpeed = ReadFanSpeedFromSysfs(mConfig);
        }

        // 3) Log results
        r.details.emplace_back("chip_temp", chipTemp >= 0 ? std::to_string(chipTemp) : "n/a");
        r.details.emplace_back("board_temp", boardTemp >= 0 ? std::to_string(boardTemp) : "n/a");
        r.details.emplace_back("fan_speed_rpm", fanSpeed >= 0 ? std::to_string(fanSpeed) : "n/a");
        r.details.emplace_back("source", usedSdk ? "sdk" : "sysfs");
        SLOG_INFO << "[fan] chip_temp=" << (chipTemp >= 0 ? std::to_string(chipTemp) : "n/a")
                  << " board_temp=" << (boardTemp >= 0 ? std::to_string(boardTemp) : "n/a")
                  << " fan=" << (fanSpeed >= 0 ? std::to_string(fanSpeed) : "n/a") << " RPM"
                  << " (" << (usedSdk ? "sdk" : "sysfs") << ")";

        auto elapsed = [&]() {
            return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - t0)
                                        .count());
        };

        // 4) Both unavailable -> SKIPPED
        if (chipTemp < 0 && fanSpeed < 0) {
            r.status = Status::SKIPPED;
            r.message = "no fan or temperature sensor found";
            r.elapsed_ms = elapsed();
            SLOG_INFO << "[fan] " << r.message;
            return r;
        }

        // 5) Temperature threshold check
        int maxTemp = mConfig.max_temp_threshold;
        if (maxTemp > 0) {
            unsigned int threshold = static_cast<unsigned int>(maxTemp);
            bool overTemp = false;
            if (chipTemp >= 0 && static_cast<unsigned int>(chipTemp) > threshold) {
                overTemp = true;
            }
            if (boardTemp >= 0 && static_cast<unsigned int>(boardTemp) > threshold) {
                overTemp = true;
            }
            if (overTemp) {
                r.status = Status::FAIL;
                r.message = "temperature over threshold (max " + std::to_string(maxTemp) +
                            "): chip=" + (chipTemp >= 0 ? std::to_string(chipTemp) : "n/a") +
                            " board=" + (boardTemp >= 0 ? std::to_string(boardTemp) : "n/a");
                r.elapsed_ms = elapsed();
                SLOG_ERROR << "[fan] " << r.message;
                return r;
            }
        }

        // 6) Fan stall check: RPM=0 with temp>0 means fan may be stalled
        if (fanSpeed == 0 && chipTemp > 0) {
            r.status = Status::WARNING;
            r.message = "fan speed is 0 RPM while temperature > 0, fan may be stalled";
            r.elapsed_ms = elapsed();
            SLOG_WARN << "[fan] " << r.message;
            return r;
        }

        // 7) All good
        r.status = Status::PASS;
        r.message = "fan ok: chip=" + (chipTemp >= 0 ? std::to_string(chipTemp) : "n/a") +
                    " board=" + (boardTemp >= 0 ? std::to_string(boardTemp) : "n/a") +
                    " fan=" + (fanSpeed >= 0 ? std::to_string(fanSpeed) : "n/a") + " RPM";
        r.elapsed_ms = elapsed();
        SLOG_INFO << "[fan] " << r.message << " (" << r.elapsed_ms << "ms)";
        return r;
    }

}  // namespace qifeng::scm
