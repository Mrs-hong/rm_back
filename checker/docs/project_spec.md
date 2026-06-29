# BM1684 边缘设备开机自检系统 — 项目详细说明

> 版本：1.0.0 | 更新日期：2026-06-29

---

## 1. 项目概述

### 1.1 背景

BM1684 是算能（Sophgo）推出的边缘 AI 推理芯片，广泛部署于智慧安防、工业质检等场景。设备在开机后、业务上线前，需自动完成硬件健康巡检，确保磁盘、内存、TPU、网卡、显示器、指纹模组、麦克风、指示灯及小模型推理链路均处于可用状态，避免"带病上岗"。

### 1.2 目标

| 维度 | 要求 |
|------|------|
| 功能 | 覆盖 9 类硬件自检 + 可扩展脚本适配器，5 秒内完成全量检查 |
| 可靠性 | 单项检查异常不阻塞其他项；超时保护；日志持久化 |
| 可扩展 | 新增检查器仅需实现 IChecker + 一行注册；外部 .sh 脚本零代码接入 |
| 跨平台 | 主目标 ARM Linux (BM1684)；x86 WSL2 可编译运行（不可用硬件自动 SKIP） |
| 生产级 | systemd 开机自启、JSON 结构化报告、退出码语义明确 |

### 1.3 术语

| 术语 | 含义 |
|------|------|
| IChecker | 自检器统一接口，所有检查器的抽象基类 |
| ScriptChecker | 脚本适配器，将外部 .sh 包装为 IChecker |
| Runner | 并发调度器，管理检查器的并行执行与超时 |
| Registry | 检查器注册表，集中管理工厂函数 |
| Context | 运行期上下文，注入配置、日志与平台能力标志 |
| Severity | 严重级别：`critical`（失败→整体 FAIL）或 `warning`（仅告警） |
| Status | 检查状态：`PASS` / `FAIL` / `WARN` / `SKIP` |

---

## 2. 系统架构

### 2.1 分层架构

```
┌─────────────────────────────────────────────────────┐
│                    main.cpp 入口                      │
│  CLI → 配置加载 → 日志初始化 → 注册 → 执行 → 报告    │
└──────────────────────┬──────────────────────────────┘
                       │
          ┌────────────┼────────────┐
          ▼            ▼            ▼
   ┌──────────┐  ┌──────────┐  ┌──────────┐
   │  Core    │  │ Checkers │  │   HW     │
   │  核心层  │  │  检查器层 │  │ 硬件抽象层│
   ├──────────┤  ├──────────┤  ├──────────┤
   │ IChecker │  │Disk      │  │SerialPort│
   │ Registry │  │Memory    │  │Gpio      │
   │ Runner   │  │Network   │  │DrmDevice │
   │ Context  │  │Tpu       │  │AlsaCapture│
   │ Result   │  │Display   │  └──────────┘
   └──────────┘  │Fingerprint│
                 │Microphone │    ┌──────────┐
                 │Light      │    │   Util   │
                 │ModelInfer │    │  工具层   │
                 │Script     │    ├──────────┤
                 └──────────┘    │Timer     │
                                 │Subprocess│
                                 └──────────┘
          ┌─────────────────────────────────┐
          │          Third Party            │
          │  nlohmann/json | spdlog         │
          └─────────────────────────────────┘
```

### 2.2 核心流程

```
main()
  │
  ├─ 1. 解析 CLI 参数（配置文件路径，默认 /etc/bm1684-selftest/selftest.json）
  ├─ 2. load_config() → SelfTestConfig（JSON → 结构体）
  ├─ 3. default_logger().init() → 双 sink 日志（控制台 + 滚动文件）
  ├─ 4. register_all(registry) → 注册 9 个内置检查器
  ├─ 5. 追加 ScriptChecker（配置中 scripts 数组驱动）
  ├─ 6. Runner::run_all() → 并发执行，超时保护
  ├─ 7. 汇总结果：critical + FAIL → overall=FAIL → exit(1)
  ├─ 8. 输出控制台表格 + JSON 报告文件
  └─ 9. 返回退出码（0=OK, 1=FAIL）
```

### 2.3 数据流

```
selftest.json ──load_config()──▶ SelfTestConfig
                                       │
                                       ▼
                                  Context { config, log, has_* }
                                       │
              ┌────────────────────────┼────────────────────────┐
              ▼                        ▼                        ▼
         IChecker::run(ctx)     IChecker::run(ctx)     ScriptChecker::run()
              │                        │                        │
              ▼                        ▼                        ▼
         CheckResult             CheckResult               CheckResult
              │                        │                        │
              └────────────────────────┼────────────────────────┘
                                       ▼
                              JSON report + 控制台表格
```

---

## 3. 检查器详解

### 3.1 检查器一览

| 检查器 | 类名 | 严重级别 | 检查方法 | 依赖 | ARM 行为 | x86 行为 |
|--------|------|---------|---------|------|---------|---------|
| 磁盘 | DiskChecker | critical | statvfs 容量 + POSIX 4KB 读写校验 | 无 | 检查 /, /data, /opt/sophon | PASS（只读分区记 io=ro） |
| 内存 | MemoryChecker | critical | /proc/meminfo + sysinfo | 无 | 检查可用内存阈值 | PASS |
| 网络 | NetworkChecker | critical | ioctl SIOCGIFCONF + ping 网关 | 无 | 枚举 UP 网卡并 ping | WARN（网关不可达） |
| TPU | TpuChecker | critical | Sophon SDK 设备内存 s2d/d2s 校验 | bmlib | 设备内存读写比对 | SKIP（无设备） |
| 模型推理 | ModelInferenceChecker | critical | 加载 probe.bmodel 并推理 | bmlib+bmrt | 模型加载+零张量推理 | SKIP（无设备/模型） |
| 显示器 | DisplayChecker | warning | libdrm connector / sysfs 降级 | libdrm(可选) | 检查 connected 状态 | WARN（无 /dev/dri） |
| 指纹模组 | FingerprintChecker | warning | 串口握手包 | 无 | 发送 EF01 帧头握手 | SKIP（无 /dev/ttyS3） |
| 麦克风 | MicrophoneChecker | warning | ALSA 录音 RMS 能量检测 | libasound | 16kHz 录音计算 RMS | SKIP（无 ALSA） |
| 指示灯 | LightChecker | warning | sysfs GPIO 翻转回读 | 无 | GPIO 输出翻转+回读 | SKIP（无 sysfs gpio） |
| 脚本适配 | ScriptChecker | 可配置 | timeout 包裹 bash 执行 | 无 | 执行外部 .sh | 按脚本逻辑 |

### 3.2 磁盘检查器 — 状态区分

磁盘检查器对异常做三态区分，避免 x86 验证环境误报：

| 场景 | errno | 状态 | 报告 details |
|------|-------|------|-------------|
| 读写校验通过 | — | PASS | `io=ok` |
| 只读文件系统 | EROFS | PASS | `io=ro`（非临界） |
| 真实 IO 故障 | EACCES/EIO/... | FAIL | `io=fail` |
| 挂载点不存在 | ENOENT/ENOTDIR | PASS | `not_mounted`（非临界） |
| statvfs 其他错误 | ENOMEM/... | FAIL | `statvfs failed: <msg>` |

### 3.3 脚本检查器 — 输出协议

ScriptChecker 将外部 .sh 脚本包装为 IChecker，定义了标准输出协议：

**退出码语义：**

| 退出码 | 含义 | 映射 Status |
|--------|------|-------------|
| 0 | 通过 | PASS |
| 1 | 失败 | FAIL |
| 2 | 跳过 | SKIP |
| 3 | 告警 | WARN |
| 124 | timeout 命令超时 | FAIL |
| 126/127 | 不可执行/未找到 | SKIP |
| 其他 | 未知错误 | FAIL |

**stdout 协议（可选）：**

最后一行若为合法 JSON 对象，则自动解析提取 `message` 和 `details`：

```json
{"message":"简要描述","details":{"key1":"value1","key2":"value2"}}
```

- `message`：字符串，覆盖退出码对应的默认消息
- `details`：对象，键值对填充到 CheckResult.details

若无 JSON 行，则取 stdout 末尾文本作为 message。

**配置示例：**

```json
{
  "scripts": [
    {
      "name": "fingerprint_sh",
      "path": "/etc/bm1684-selftest/scripts/check_fingerprint.sh",
      "severity": "warning",
      "timeout_sec": 5
    }
  ]
}
```

---

## 4. 配置体系

### 4.1 配置文件结构

文件：`config/selftest.json`（安装后位于 `/etc/bm1684-selftest/selftest.json`）

```json
{
  "per_item_timeout_sec": 5,
  "parallel": true,
  "report_path": "/var/log/bm1684-selftest/report.json",
  "log_dir": "/var/log/bm1684-selftest",
  "disk": {
    "mounts": ["/", "/data", "/opt/sophon"],
    "min_free_pct": 5
  },
  "memory": { "min_available_mb": 128 },
  "fingerprint": {
    "device": "/dev/ttyS3",
    "baud": 57600,
    "timeout_ms": 1500
  },
  "microphone": {
    "device": "default",
    "duration_ms": 400,
    "min_rms": 50
  },
  "light": { "gpio": "488" },
  "network": {
    "gateway": "192.168.1.1",
    "ping_count": 3
  },
  "model": { "path": "/opt/sophon/selftest/probe.bmodel" },
  "scripts": [
    {
      "name": "fingerprint_sh",
      "path": "/etc/bm1684-selftest/scripts/check_fingerprint.sh",
      "severity": "warning",
      "timeout_sec": 5
    }
  ]
}
```

### 4.2 配置字段说明

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| per_item_timeout_sec | int | 5 | 单项检查超时秒数 |
| parallel | bool | true | 并发执行（false 为顺序调试） |
| report_path | string | /var/log/.../report.json | JSON 报告输出路径 |
| log_dir | string | /var/log/.../ | 日志文件目录（空则仅控制台） |
| disk.mounts | string[] | ["/","/data","/opt/sophon"] | 待检查的挂载点列表 |
| disk.min_free_pct | int | 5 | 最低剩余空间百分比 |
| memory.min_available_mb | int | 128 | 最低可用内存(MB) |
| fingerprint.device | string | /dev/ttyS3 | 指纹模组串口设备 |
| fingerprint.baud | int | 57600 | 串口波特率 |
| fingerprint.timeout_ms | int | 1500 | 串口响应超时(ms) |
| microphone.device | string | default | ALSA 录音设备名 |
| microphone.duration_ms | int | 400 | 录音时长(ms) |
| microphone.min_rms | int | 50 | 最低 RMS 能量阈值 |
| light.gpio | string | 488 | GPIO 引脚编号 |
| network.gateway | string | 192.168.1.1 | 网关地址 |
| network.ping_count | int | 3 | ping 次数 |
| model.path | string | /opt/sophon/.../probe.bmodel | 探测模型路径 |
| scripts[].name | string | — | 脚本检查项名（唯一标识） |
| scripts[].path | string | — | 脚本绝对路径 |
| scripts[].severity | string | warning | "critical" 或 "warning" |
| scripts[].timeout_sec | int | 5 | 脚本执行超时 |

### 4.3 配置加载策略

- 文件存在 → 解析并覆盖默认值（部分覆盖，未指定字段保留默认）
- 文件不存在 → 使用内置默认值 + LOG_WARN
- JSON 解析失败 → 使用内置默认值 + LOG_WARN
- 字段类型不匹配 → 使用该字段默认值（`get_or<T>` 模板安全取值）

---

## 5. 报告格式

### 5.1 JSON 报告结构

```json
{
  "timestamp": "2026-06-29T15:35:28Z",
  "version": "1.0.0",
  "overall": "OK",
  "checks": {
    "disk": {
      "status": "PASS",
      "elapsed_ms": 0,
      "message": "disk ok",
      "details": {
        "/": "total=1031018MB avail=960272MB free=93% io=ok"
      }
    },
    "network": {
      "status": "WARN",
      "elapsed_ms": 4,
      "message": "gateway unreachable",
      "details": {
        "interfaces": "eth0",
        "gateway": "192.168.1.1",
        "gateway_reachable": "no"
      }
    },
    "fingerprint_sh": {
      "status": "SKIP",
      "elapsed_ms": 3,
      "message": "device not present",
      "details": {
        "severity": "warning",
        "script": "/etc/bm1684-selftest/scripts/check_fingerprint.sh",
        "exit_code": "2",
        "device": "/dev/ttyS3"
      }
    }
  }
}
```

### 5.2 退出码语义

| 退出码 | 含义 | 触发条件 |
|--------|------|---------|
| 0 | OK | 无 critical 项 FAIL |
| 1 | FAIL | 存在 critical 项 FAIL |

判定逻辑：内置 critical 项（disk/memory/network/tpu/model_inference）FAIL → overall=FAIL；脚本项通过 details.severity="critical" 判定。

### 5.3 控制台输出示例

```
ITEM               STATUS   TIME     MESSAGE
----               ------   ----     -------
disk               PASS     0     ms disk ok
memory             PASS     0     ms memory ok
network            WARN     4     ms gateway unreachable
tpu                SKIP     0     ms no TPU device
model_inference    SKIP     0     ms probe model not deployed
display            WARN     0     ms no drm device
fingerprint        SKIP     0     ms device not present: /dev/ttyS3
microphone         SKIP     0     ms ALSA not available
light              SKIP     0     ms gpio 488 not available
fingerprint_sh     SKIP     3     ms device not present
```

---

## 6. 并发与超时机制

### 6.1 并发调度

- `parallel=true`：每个 IChecker 起一个 `std::async` 任务，主线程 `future::wait_for(timeout)` 逐个等待
- `parallel=false`：顺序执行但仍保留超时包裹，便于单步调试

### 6.2 超时保护

| 层级 | 机制 | 超时值 |
|------|------|--------|
| Runner → IChecker | `std::future::wait_for()` | `per_item_timeout_sec`（默认 5s） |
| ScriptChecker → .sh | `timeout` 命令包裹 | `scripts[].timeout_sec`（默认 5s） |
| NetworkChecker → ping | ping 自带 `-W 1` | 每次等待 1s |
| SerialPort → read | `select()` | `fingerprint.timeout_ms`（默认 1500ms） |
| systemd 整体 | `TimeoutStartSec` | 120s（systemd 单元配置） |

### 6.3 异常安全

- IChecker::run() 抛出异常 → Runner 捕获，标记 Status::kFail + "exception: ..."
- C++17 无原生线程取消，被超时的任务自然结束（依赖 IO 自身短超时）

---

## 7. 条件编译与平台适配

### 7.1 CMake 探测

| 宏 | 条件 | 影响 |
|----|------|------|
| CHECKER_HAS_BM_SDK | 找到 bmlib/bmrt/bmcv | TPU/模型推理检查器有真实实现 |
| CHECKER_HAS_ALSA | 找到 libasound | 麦克风检查器有真实录音实现 |
| CHECKER_HAS_DRM | 找到 libdrm | 显示器检查器用 DRM API |

未找到时对应检查器编译为桩实现（直接返回 SKIP），不影响构建。

### 7.2 运行期探测

- TPU：`bm_dev_request(&handle, 0)` 失败 → SKIP
- 模型推理：probe.bmodel 不存在 → SKIP
- 指纹模组：/dev/ttyS3 不存在 → SKIP
- 显示器：/dev/dri 不存在 → WARN
- GPIO：/sys/class/gpio/export 不可写 → SKIP
- 麦克风：PCM 设备打开失败 → SKIP

---

## 8. 第三方库

| 库 | 版本 | 许可证 | 路径 | 用途 |
|----|------|--------|------|------|
| nlohmann/json | v3.11.3 | MIT | third_party/nlohmann/json.hpp | JSON 解析与序列化 |
| spdlog | v1.13.0 | MIT | third_party/spdlog/spdlog/ | 高性能日志（bundled fmt） |

两者均为 header-only，vendored 内置于 `third_party/`，构建时无需联网。

CMake 通过 `checker_third_party` INTERFACE 库传播 include 路径：

```cmake
add_library(checker_third_party INTERFACE)
target_include_directories(checker_third_party INTERFACE
  ${CMAKE_SOURCE_DIR}/third_party
  ${CMAKE_SOURCE_DIR}/third_party/spdlog
)
```

---

## 9. 日志系统

### 9.1 架构

```
调用方
  │
  ▼ LOG_INFO("fmt %s", arg)   ← printf 风格宏
  │
  ▼ Logger::log(level, fmt, ...)
  │
  ├─ vsnprintf 格式化 → std::string
  │
  ▼ spdlog::logger（pImpl 隐藏）
     │
     ├─ stderr_color_sink_mt    → 彩色控制台
     └─ rotating_file_sink_mt   → 滚动文件（1MB × 3 份）
```

### 9.2 日志级别

| 级别 | 用途 |
|------|------|
| TRACE | 极细粒度调试 |
| DEBUG | 调试信息 |
| INFO | 正常运行信息（默认） |
| WARN | 非阻断异常 |
| ERROR | 严重错误 |

### 9.3 日志格式

```
2026-06-29 23:35:28 [info] === BM1684 SelfTest v1.0.0 start ===
2026-06-29 23:35:28 [warning] config file not found: xxx, using built-in defaults
```

- warn 及以上级别立即 flush 到磁盘
- 滚动策略：单文件 1MB，最多保留 3 份

---

## 10. 部署与运维

### 10.1 目录布局

```
/usr/local/bin/bm1684-selftest                    # 可执行文件
/etc/bm1684-selftest/selftest.json                # 配置文件
/etc/bm1684-selftest/scripts/*.sh                 # 外部检测脚本
/var/log/bm1684-selftest/selftest.log             # 日志文件
/var/log/bm1684-selftest/report.json              # 检测报告
/etc/systemd/system/bm1684-selftest.service       # systemd 单元
```

### 10.2 开机自启流程

```
systemd
  │
  ├─ local-fs.target    ← 磁盘就绪
  ├─ systemd-udevd      ← 设备节点就绪
  │
  ▼ bm1684-selftest.service (Before=basic.target)
     │
     └─ /usr/local/bin/bm1684-selftest /etc/bm1684-selftest/selftest.json
         │
         ├─ exit 0 → OK，继续启动
         └─ exit 1 → FAIL，不影响启动（RemainAfterExit=yes）
```

### 10.3 安装脚本

```bash
sudo bash scripts/install.sh [/usr/local]
```

自动完成：二进制部署 → 配置拷贝 → systemd 使能。

---

## 11. 项目目录结构

```
checker/
├── CMakeLists.txt              # CMake 构建脚本
├── .clang-format               # 代码格式配置
├── config/
│   └── selftest.json           # 运行期配置
├── include/checker/
│   ├── core/                   # 核心抽象层
│   │   ├── checker.h           # IChecker 接口
│   │   ├── context.h           # 配置与上下文
│   │   ├── registry.h          # 注册表
│   │   ├── result.h            # 结果数据模型
│   │   └── runner.h            # 并发调度器
│   ├── checkers/               # 具体检查器
│   │   ├── disk_checker.h
│   │   ├── memory_checker.h
│   │   ├── network_checker.h
│   │   ├── tpu_checker.h
│   │   ├── model_inference_checker.h
│   │   ├── display_checker.h
│   │   ├── fingerprint_checker.h
│   │   ├── microphone_checker.h
│   │   ├── light_checker.h
│   │   └── script_checker.h    # 脚本适配器
│   ├── hw/                     # 硬件抽象
│   │   ├── serial_port.h
│   │   ├── gpio.h
│   │   ├── drm_device.h
│   │   └── alsa_capture.h
│   ├── log/
│   │   └── logger.hpp          # 日志器（spdlog 外观）
│   ├── util/
│   │   ├── subprocess.h
│   │   └── time_util.h
│   └── generated/
│       └── config.h.in         # CMake 生成配置模板
├── src/                        # 实现文件（与 include 一一对应）
│   ├── main.cpp
│   ├── core/
│   ├── checkers/
│   ├── hw/
│   ├── log/
│   └── util/
├── scripts/
│   ├── check_fingerprint.sh    # 示例脚本
│   ├── install.sh              # 部署脚本
│   └── bm1684-selftest.service # systemd 单元
├── tests/
│   └── test_json.cpp           # JSON + 配置加载测试
├── third_party/                # vendored 第三方库
│   ├── nlohmann/json.hpp
│   └── spdlog/spdlog/
└── docs/                       # 文档
```

---

## 12. 设计决策记录

| 决策 | 选择 | 理由 |
|------|------|------|
| 脚本集成方式 | 混合适配器（原项目内扩展） | 同一调度器/超时/报告统一管理，避免双套代码 |
| JSON 库 | nlohmann/json (vendored) | 成熟稳定，MIT，header-only，替代自写 mini-JSON |
| 日志库 | spdlog (vendored, pImpl) | 高性能异步日志，MIT，pImpl 隐藏避免 API 污染 |
| 配置格式 | JSON（非 YAML/TOML） | C++ 原生解析简单，嵌入式设备无需额外依赖 |
| 并发模型 | std::async + future::wait_for | C++17 标准库，无需引入额外并发框架 |
| 磁盘 IO 检测 | POSIX open/write/read | 比 ofstream 可靠捕获 errno（EROFS 等） |
| 脚本超时 | coreutils `timeout` 命令 | 最简单可靠，无需实现进程组管理 |
