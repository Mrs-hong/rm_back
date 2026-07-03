/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <string>
#include <vector>

#include "checker/config.h"  // CMake 生成的 CHECKER_HAS_* 宏

namespace qifeng::scm {

// 各检查器的配置子段（与 config/selftest.json 一一对应）
struct DiskConfig {
    std::vector<std::string> mounts{"/", "/data", "/opt/sophon"};
    int min_free_pct = 5;
};
struct MemoryConfig {
    int min_available_mb = 128;
};
struct DisplayConfig {
    std::string dri_path = "/dev/dri";  // DRI 设备目录路径
    int max_card_index = 8;             // 最大 card 索引（扫描 card0 ~ N-1）
};
struct FingerprintConfig {
    std::string device = "/dev/ttyS3";
    int baud = 57600;
    int timeout_ms = 1500;
};
struct MicrophoneConfig {
    std::string device = "default";
    int duration_ms = 400;
    int min_rms = 50;
    int sample_rate = 16000;  // 采样率（Hz）
    int channels = 1;         // 通道数
};
struct LightConfig {
    std::string gpio = "488";
};
struct NetworkConfig {
    std::string gateway = "192.168.1.1";
    int ping_count = 3;
    int ping_timeout_sec = 1;  // ping 单次超时（秒）
};
struct FanConfig {
    int max_temp_threshold = 85;          // 温度告警阈值（°C），0 表示不检查温度
    int hwmon_max_index = 16;             // hwmon 扫描上限
    int fan_max_index = 8;                // fan 通道扫描上限
    int thermal_zone_max_index = 16;      // thermal_zone 扫描上限
};
struct TpuConfig {
    int min_mem_mb = 0;  // 最小 TPU 显存要求（MB），0 表示不检查
};
struct ModelConfig {
    std::string path = "/opt/sophon/selftest/fsmn_fp32_.bmodel";
};

/**
 * @brief PCBA 硬件自检脚本配置
 * @details 由开发板厂商提供的脚本路径、参数、严重级别、超时及输出解析规则。
 */
struct PcbaConfig {
    std::string exe_command;                       // 脚本绝对路径或可执行文件名
    std::vector<std::string> args;                 // 传给脚本的参数列表
    std::string severity = "warning";              // "critical" 或 "warning"
    int timeout_sec = 60;                          // 脚本执行超时（秒）
    bool parse_output = false;                     // 是否对输出做关键字解析
    std::string pass_keyword = "PASS";             // 判定通过的关键字
    std::string fail_keyword = "FAIL";             // 判定失败的关键字
};

/**
 * @brief 顶层配置
 */
struct SelfTestConfig {
    int per_item_timeout_sec = 5;
    bool parallel = true;
    std::string report_path = "/var/log/qifeng-scm/selftest-report.json";
    std::string log_dir = "/var/log/qifeng-scm";

    DiskConfig disk;
    MemoryConfig memory;
    DisplayConfig display;
    TpuConfig tpu;
    FanConfig fan;
    FingerprintConfig fingerprint;
    MicrophoneConfig microphone;
    LightConfig light;
    NetworkConfig network;
    ModelConfig model;
    PcbaConfig pcba;
};

/**
 * @brief 执行上下文：注入到每个 checker 的 Run() 中
 */
struct Context {
    const SelfTestConfig &config;

    // 平台能力标志（来自编译期 + 运行期探测），决定 checker 行为
    bool has_bm_sdk = (CHECKER_HAS_BM_SDK != 0);
    bool has_alsa = (CHECKER_HAS_ALSA != 0);
    bool has_drm = (CHECKER_HAS_DRM != 0);
    bool has_gpiod = (CHECKER_HAS_GPIOD != 0);
};

/**
 * @brief 从 JSON 文件加载配置
 * @details 文件不存在或解析失败时返回内置默认并告警。日志经 SLOG_* 宏输出。
 * @param path JSON 配置文件路径
 * @return SelfTestConfig 配置对象
 */
SelfTestConfig LoadConfig(const std::string &path);

}  // namespace qifeng::scm
