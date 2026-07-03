/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/core/context.h"

#include <fstream>
#include <json/json.h>
#include <sstream>
#include <type_traits>

#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    namespace {

        /**
         * @brief 安全取值辅助：键不存在或类型不符时返回默认值
         * @details 利用 if constexpr 在编译期按类型分派 jsoncpp 对应的取值方法
         */
        template <class T>
        T GetOr(const Json::Value &j, const char* key, const T &def) {
            if (!j.isMember(key)) {
                return def;
            }
            const Json::Value &v = j[key];
            if constexpr (std::is_same_v<T, std::string>) {
                return v.isString() ? v.asString() : def;
            } else if constexpr (std::is_same_v<T, int>) {
                return v.isInt() ? v.asInt() : def;
            } else if constexpr (std::is_same_v<T, bool>) {
                return v.isBool() ? v.asBool() : def;
            } else {
                return def;
            }
        }

        /**
         * @brief 从 JSON 对象提取各子段配置
         */
        // NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
        void ApplyConfig(const Json::Value &root, SelfTestConfig &cfg) {
            cfg.per_item_timeout_sec = GetOr(root, "per_item_timeout_sec", cfg.per_item_timeout_sec);
            cfg.parallel = GetOr(root, "parallel", cfg.parallel);
            cfg.report_path = GetOr(root, "report_path", cfg.report_path);
            cfg.log_dir = GetOr(root, "log_dir", cfg.log_dir);

            if (root.isMember("disk")) {
                const Json::Value &d = root["disk"];
                cfg.disk.min_free_pct = GetOr(d, "min_free_pct", cfg.disk.min_free_pct);
                if (d.isMember("mounts") && d["mounts"].isArray()) {
                    cfg.disk.mounts.clear();
                    for (Json::ArrayIndex i = 0; i < d["mounts"].size(); ++i) {
                        const Json::Value &v = d["mounts"][i];
                        if (v.isString()) {
                            cfg.disk.mounts.push_back(v.asString());
                        }
                    }
                }
            }
            if (root.isMember("memory")) {
                cfg.memory.min_available_mb = GetOr(root["memory"], "min_available_mb", cfg.memory.min_available_mb);
            }
            if (root.isMember("display")) {
                const Json::Value &d = root["display"];
                cfg.display.dri_path = GetOr(d, "dri_path", cfg.display.dri_path);
                cfg.display.max_card_index = GetOr(d, "max_card_index", cfg.display.max_card_index);
            }
            if (root.isMember("tpu")) {
                cfg.tpu.min_mem_mb = GetOr(root["tpu"], "min_mem_mb", cfg.tpu.min_mem_mb);
            }
            if (root.isMember("fan")) {
                const Json::Value &f = root["fan"];
                cfg.fan.max_temp_threshold = GetOr(f, "max_temp_threshold", cfg.fan.max_temp_threshold);
                cfg.fan.hwmon_max_index = GetOr(f, "hwmon_max_index", cfg.fan.hwmon_max_index);
                cfg.fan.fan_max_index = GetOr(f, "fan_max_index", cfg.fan.fan_max_index);
                cfg.fan.thermal_zone_max_index = GetOr(f, "thermal_zone_max_index", cfg.fan.thermal_zone_max_index);
            }
            if (root.isMember("fingerprint")) {
                const Json::Value &f = root["fingerprint"];
                cfg.fingerprint.device = GetOr(f, "device", cfg.fingerprint.device);
                cfg.fingerprint.baud = GetOr(f, "baud", cfg.fingerprint.baud);
                cfg.fingerprint.timeout_ms = GetOr(f, "timeout_ms", cfg.fingerprint.timeout_ms);
            }
            if (root.isMember("microphone")) {
                const Json::Value &m = root["microphone"];
                cfg.microphone.device = GetOr(m, "device", cfg.microphone.device);
                cfg.microphone.duration_ms = GetOr(m, "duration_ms", cfg.microphone.duration_ms);
                cfg.microphone.min_rms = GetOr(m, "min_rms", cfg.microphone.min_rms);
                cfg.microphone.sample_rate = GetOr(m, "sample_rate", cfg.microphone.sample_rate);
                cfg.microphone.channels = GetOr(m, "channels", cfg.microphone.channels);
            }
            if (root.isMember("light")) {
                cfg.light.gpio = GetOr(root["light"], "gpio", cfg.light.gpio);
            }
            if (root.isMember("network")) {
                const Json::Value &n = root["network"];
                cfg.network.gateway = GetOr(n, "gateway", cfg.network.gateway);
                cfg.network.ping_count = GetOr(n, "ping_count", cfg.network.ping_count);
                cfg.network.ping_timeout_sec = GetOr(n, "ping_timeout_sec", cfg.network.ping_timeout_sec);
            }
            if (root.isMember("model")) {
                cfg.model.path = GetOr(root["model"], "path", cfg.model.path);
            }
            if (root.isMember("pcba")) {
                const Json::Value &p = root["pcba"];
                cfg.pcba.exe_command = GetOr(p, "exe_command", cfg.pcba.exe_command);
                cfg.pcba.severity = GetOr(p, "severity", cfg.pcba.severity);
                cfg.pcba.timeout_sec = GetOr(p, "timeout_sec", cfg.pcba.timeout_sec);
                cfg.pcba.parse_output = GetOr(p, "parse_output", cfg.pcba.parse_output);
                cfg.pcba.pass_keyword = GetOr(p, "pass_keyword", cfg.pcba.pass_keyword);
                cfg.pcba.fail_keyword = GetOr(p, "fail_keyword", cfg.pcba.fail_keyword);

                if (p.isMember("args") && p["args"].isArray()) {
                    cfg.pcba.args.clear();
                    for (Json::ArrayIndex i = 0; i < p["args"].size(); ++i) {
                        const Json::Value &v = p["args"][i];
                        if (v.isString()) {
                            cfg.pcba.args.push_back(v.asString());
                        }
                    }
                }

                // severity 仅允许 critical/warning，非法值回退为 warning
                if (cfg.pcba.severity != "critical" && cfg.pcba.severity != "warning") {
                    SLOG_WARN << "invalid pcba.severity '" << cfg.pcba.severity
                              << "', fallback to 'warning'";
                    cfg.pcba.severity = "warning";
                }
            }
        }

    }  // namespace

    SelfTestConfig LoadConfig(const std::string &path) {
        SelfTestConfig cfg;  // 内置默认
        std::ifstream ifs(path);
        if (!ifs) {
            SLOG_WARN << "config file not found: " << path << ", using built-in defaults";
            return cfg;
        }
        // 读取文件全部内容到字符串
        std::stringstream ss;
        ss << ifs.rdbuf();
        std::string content = ss.str();

        // 使用 jsoncpp 的 CharReaderBuilder + CharReader 解析
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        if (reader->parse(content.data(), content.data() + content.size(), &root, &errors)) {
            ApplyConfig(root, cfg);
            SLOG_INFO << "config loaded from " << path;
        } else {
            SLOG_WARN << "config parse failed: " << errors << ", using built-in defaults";
        }
        return cfg;
    }

}  // namespace qifeng::scm