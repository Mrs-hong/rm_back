# Checker 模块集成设计文档

## 1. 概述

checker 模块以**静态库**（`libqifeng_checker.a`）形式集成到 `qf_scmd` 主进程中，为 BM1684 边缘设备提供开机自检与手动自检能力。该库封装了 10 个内置硬件/软件检查器以及配置驱动的脚本检查器，通过 `CheckerRunner` 对外暴露统一接口，被 `ScmServer`（开机自检）和 `CheckHandler`（手动触发）共同调用。

---

## 2. 架构设计

### 2.1 整体架构图

```
+-------------------+       UDS/IPC        +------------------------+
|   qf_scmc (CLI)   | <------------------> |     qf_scmd (主进程)    |
|  check --config   |   JSON over UDS      |                        |
+-------------------+                       |  +------------------+  |
                                            |  |  ScmServer       |  |
                                            |  |  - RunSelfCheck()|  |
                                            |  |  - CheckHandler  |  |
                                            |  +--------+---------+  |
                                            |           |            |
                                            |           v            |
                                            |  +------------------+  |
                                            |  |  CheckerRunner   |  |
                                            |  |  - Run()         |  |
                                            |  +--------+---------+  |
                                            |           |            |
                                            |           v            |
                                            |  +------------------+  |
                                            |  | libqifeng_checker|  |
                                            |  |   (静态库)        |  |
                                            |  +------------------+  |
                                            +------------------------+
```

### 2.2 checker 库内部架构

```
+---------------------------------------------------------------+
|                    libqifeng_checker.a                         |
|                                                               |
|  +-------------+    +----------------+    +-----------+       |
|  |  IChecker   |    | CheckerRegistry|    |   Runner  |       |
|  |  (接口)     |--->| (注册+工厂)     |--->| (并发调度) |       |
|  +------+------+    +----------------+    +-----+-----+       |
|         |                                       |             |
|         v                                       v             |
|  +-------------+                         +-------------+      |
|  | 具体Checker  |                         | CheckResult |      |
|  | - DiskChecker     |                   | (单项结果)   |      |
|  | - MemoryChecker   |                   +-------------+      |
|  | - NetworkChecker  |                                        |
|  | - TpuChecker      |                                        |
|  | - ModelInferenceChecker|                                  |
|  | - DisplayChecker  |                                        |
|  | - FingerprintChecker|                                     |
|  | - MicrophoneChecker|                                      |
|  | - LightChecker    |                                        |
|  | - ScriptChecker   |                                        |
|  +-------------------+                                        |
|                                                               |
|  +----------+  +-----------+  +----------+     |
|  | hw/      |  | util/     |  | core/    |     |
|  | serial   |  | subprocess|  | context  |     |
|  | gpio     |  | time_util |  |          |     |
|  | alsa     |  |           |  |          |     |
|  | drm      |  |           |  |          |     |
|  +----------+  +-----------+  +----------+     |
+---------------------------------------------------------------+
```

核心类关系：

| 类 | 职责 |
|---|------|
| `IChecker` | 检查器统一接口，定义 `name()`/`severity()`/`run()` |
| `CheckerRegistry` | 工厂注册表，`add<T>()` 注册，`build_all()` 批量构建 |
| `Runner` | 并发调度器，对每个 checker 起异步任务 + 超时保护 |
| `CheckResult` | 单项结果数据模型（状态/耗时/消息/明细） |
| `Context` | 运行期上下文，聚合配置与平台能力标志（日志通过全局 `SLOG_*` 宏输出） |
| `ScriptChecker` | 脚本适配器，将外部 `.sh` 脚本包装为 `IChecker` |

### 2.3 与主程序的集成点

checker 库通过以下三个关键类与 `qf_scmd` 主进程集成：

#### 2.3.1 CheckerRunner

位于 `include/checker/checker_runner.h`，是主程序调用 checker 库的唯一入口：

```cpp
namespace qifeng::scm {
class CheckerRunner {
public:
    struct CheckReport {
        bool overallOk{true};        // 整体是否通过（critical 项无 FAIL）
        std::string overallStatus;   // "OK" 或 "FAIL"
        std::string reportPath;      // JSON 报告文件路径
        Json::Value details;         // 各检查项详情
        std::string summary;         // 摘要文本
    };

    static CheckReport Run(const std::string& configPath);
};
}
```

#### 2.3.2 CheckHandler

位于 `include/scmd/handlers/check_handler.h`，处理 `CHECK` 命令的 IPC 请求：

```cpp
class CheckHandler : public ICommandHandler {
public:
    explicit CheckHandler(std::string configPath);
    ScmCommand GetCommand() const override;  // 返回 ScmCommand::CHECK
    ScmResponse Handle(const ScmRequest& request, ...) override;
};
```

支持请求中通过 `CheckRequest::configPath` 指定配置路径，为空则使用构造时传入的默认路径。

#### 2.3.3 RunSelfCheck()

位于 `src/scmd/scmd_server.cpp`，在 `main()` 中 `server.Start()` 之前调用，确保设备就绪后再进入服务循环：

```cpp
bool ScmServer::RunSelfCheck() {
    // 读取 scmd.yaml 中 selftest 段配置
    // 确定配置路径（默认 /etc/qifeng-scm/selftest.json）
    auto report = CheckerRunner::Run(mSelfTestConfigPath);
    // 输出摘要到控制台
    // 自检失败且 fail_action=halt 时返回 false，阻止启动
    // 自检通过或 fail_action=warn 时返回 true
}
```

调用时序：`main()` -> `server.RunSelfCheck()` -> 若返回 true 则 `server.Start()` -> UDS 事件循环

若 `RunSelfCheck()` 返回 false（`fail_action=halt` 且自检失败），`main()` 直接退出，不进入服务循环。

---

## 3. 接口设计

### 3.1 CheckerRunner::Run() 接口

**签名**：

```cpp
static CheckReport Run(const std::string& configPath);
```

**参数**：

| 参数 | 类型 | 说明 |
|------|------|------|
| configPath | `const std::string&` | selftest.json 配置文件路径 |

**返回值**：`CheckReport` 结构

| 字段 | 类型 | 说明 |
|------|------|------|
| overallOk | `bool` | 所有 critical 项均通过时为 true |
| overallStatus | `std::string` | "OK" 或 "FAIL" |
| reportPath | `std::string` | JSON 报告文件路径 |
| details | `Json::Value` | 各检查项详情（key 为检查项名） |
| summary | `std::string` | 摘要文本，如 "5 passed, 1 failed, 2 warnings, 2 skipped" |

**内部流程**：

1. 加载配置（`load_config`）
2. 构建上下文（`Context`）
3. 注册并构建内置检查器（`register_all` + `build_all`）
4. 追加配置驱动的脚本检查器
5. 执行自检（`Runner::run_all`）
6. 汇总结果（判定 critical 项）
7. 生成摘要
8. 写入 JSON 报告文件

### 3.2 CHECK 命令 IPC 协议

#### 请求格式

```json
{
  "command": "check",
  "data": {
    "configPath": "/etc/qifeng-scm/selftest.json"
  }
}
```

`configPath` 为可选字段，为空时使用服务端默认路径。

#### 响应格式

```json
{
  "code": 0,
  "message": "Self-test OK: 5 passed, 0 failed, 2 warnings, 2 skipped",
  "data": {
    "overall": "OK",
    "reportPath": "/var/log/qifeng-scm/selftest-report.json",
    "checks": {
      "disk": {
        "status": "PASS",
        "elapsed_ms": 12,
        "message": "...",
        "details": { ... }
      },
      "memory": { ... },
      "network": { ... },
      "tpu": { ... },
      "model_inference": { ... },
      "display": { ... },
      "fingerprint": { ... },
      "microphone": { ... },
      "light": { ... },
      "self_check_script": { ... }
    }
  }
}
```

| 字段 | 说明 |
|------|------|
| code | 0=成功, -1=失败（至少一个 critical 项 FAIL） |
| message | 摘要文本 |
| data.overall | "OK" 或 "FAIL" |
| data.reportPath | JSON 报告文件路径 |
| data.checks | 各检查项详情，key 为检查项名 |

### 3.3 CLI 命令

```bash
# 使用默认配置执行自检
qf_scmc check

# 指定配置文件路径
qf_scmc check --config /path/to/selftest.json
```

CLI 解析由 `CheckCommand`（`src/scmctl/cli_commands.cpp`）实现，将 `--config` 参数封装为 `CheckRequest`，经 UDS 发送至 `scmd`。

---

## 4. 依赖替换

checker 库原为独立项目（`checker/` 子目录），自带第三方依赖。集成到 qifeng-scm 后进行了以下替换：

### 4.1 日志

| 原始依赖 | 替换为 | 说明 |
|----------|--------|------|
| spdlog + `checker::Logger` 适配层 | qifeng_framework `SLOG_*` 宏 | checker 库直接使用 `SLOG_INFO << ...` 流式宏，与主程序共享同一日志系统，无需独立初始化 |

原有的 `checker::Logger` 适配层（`checker/log/logger.hpp` + `checker/log/logger.cpp`）已移除，所有 checker 源文件直接使用 `SLOG_*` 宏：

```cpp
// 替换前：printf 风格适配层
LOG_INFO("[disk] %s (%dms)", r.message.c_str(), r.elapsed_ms);

// 替换后：直接使用 SLOG 流式宏
SLOG_INFO << "[disk] " << r.message << " (" << r.elapsed_ms << "ms)";
```

### 4.2 JSON

| 原始依赖 | 替换为 | 说明 |
|----------|--------|------|
| nlohmann/json（`checker/third_party/nlohmann/json.hpp`） | jsoncpp（via qifeng_framework） | 集成到 scmd 后，`CheckerRunner` 和 `ScriptChecker` 使用 `Json::Value` |

注意：checker 子目录的独立可执行文件（`checker/src/main.cpp`）已移除，所有自检功能均通过 `CheckerRunner` 集成到 scmd 主进程中。

---

## 5. 条件编译

checker 库通过 CMake 选项和编译宏控制可选系统库的启用：

### 5.1 CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `WITH_BM1684_SDK` | ON | 启用 Sophon SDK（bmlib/bmrt/bmcv） |
| `WITH_ALSA` | ON | 启用 ALSA 麦克风采集 |
| `WITH_DRM` | ON | 启用 libdrm 显示器探测 |
| `WITH_GPIOD` | ON | 启用 libgpiod GPIO 控制 |

### 5.2 编译宏

| 宏 | 值 | 影响的检查器 | 未启用时行为 |
|----|----|-------------|-------------|
| `CHECKER_HAS_BM_SDK` | 0/1 | TpuChecker, ModelInferenceChecker | 运行期返回 `Status::kSkipped` |
| `CHECKER_HAS_ALSA` | 0/1 | MicrophoneChecker | 运行期返回 `Status::kSkipped` |
| `CHECKER_HAS_DRM` | 0/1 | DisplayChecker | 降级为 sysfs 探测 |
| `CHECKER_HAS_GPIOD` | 0/1 | LightChecker | 运行期返回 `Status::kSkipped` |

宏由 CMake `configure_file` 从 `config.h.in` 生成，存放在 `${CMAKE_BINARY_DIR}/generated/checker/config.h`。运行期通过 `Context::has_bm_sdk` / `has_alsa` / `has_drm` 标志传递给各检查器。

### 5.3 探测逻辑

```cmake
# Sophon SDK 探测
find_path(BM_SDK_INCLUDE_DIR NAMES bmlib_runtime.h ...)
find_library(BM_LIB_BMLIB NAMES bmlib ...)
find_library(BM_LIB_BMRT NAMES bmrt ...)
find_library(BM_LIB_BMCV NAMES bmcv ...)
# 全部找到时 CHECKER_HAS_BM_SDK=1，否则告警

# ALSA 探测
find_path(ALSA_INCLUDE_DIR NAMES asoundlib.h ...)
find_library(ALSA_LIB NAMES asound ...)
# 找到时 CHECKER_HAS_ALSA=1

# libdrm 探测
find_path(DRM_INCLUDE_DIR NAMES xf86drm.h ...)
find_library(DRM_LIB NAMES drm ...)
# 找到时 CHECKER_HAS_DRM=1，否则 display_checker 降级为 sysfs

# libgpiod 探测
find_path(GPIOD_INCLUDE_DIR NAMES gpiod.h ...)
find_library(GPIOD_LIB NAMES gpiod ...)
# 找到时 CHECKER_HAS_GPIOD=1，否则 light_checker 返回 Skipped
```

---

## 6. 脚本集成

### 6.1 ScriptChecker 机制

`ScriptChecker` 将外部 `.sh` 脚本包装为 `IChecker`，纳入统一框架，享受并发调度、超时保护、结构化报告和 severity 分级。

配置示例（`selftest.json` 中 `scripts` 段）：

```json
{
  "scripts": [
    {
      "name": "self_check_script",
      "path": "/usr/lib/qifeng-scm/self-check",
      "severity": "warning",
      "timeout_sec": 60
    }
  ]
}
```

### 6.2 脚本输出协议

#### 退出码含义

| 退出码 | 含义 | 映射状态 |
|--------|------|----------|
| 0 | 通过 | PASS |
| 1 | 失败 | FAIL |
| 2 | 跳过 | SKIP |
| 3 | 告警 | WARN |
| 124 | 超时（`timeout` 命令返回） | FAIL |
| 126 | 不可执行 | SKIP |
| 127 | 命令未找到 | SKIP |
| 其他 | 未知失败 | FAIL |

#### JSON 协议行

脚本 stdout 最后一行若为合法 JSON 对象，将被解析为元数据：

```json
{"message":"简要描述","details":{"key":"value"}}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| message | string | 覆盖默认的 message 文本 |
| details | object | 附加明细键值对，合并到 `CheckResult::details` |

若无 JSON 行，则取 stdout 末尾文本作为 message，details 为空。

### 6.3 self-check 脚本

`/usr/lib/qifeng-scm/self-check` 是一个外部脚本（由部署包提供），通过 `ScriptChecker` 调度。该脚本内部可能调用多个子检查（如 ButtonComm、LcdSerial、TFCard、Fan），每个子检查结果通过退出码和 JSON 协议行汇报。

---

## 7. 开机自检流程

`RunSelfCheck()` 在 `main()` 中 `server.Start()` 之前调用，确保设备就绪后再进入服务循环：

```
main()
    |
    +---> 创建 ServiceControl、ScmServer
    |
    +---> server.RunSelfCheck()     // 在 Start() 之前执行
    |         |
    |         +---> 读取 scmd.yaml 中 selftest 段配置
    |         +---> 检查 selftest.enabled，若禁用则直接返回 true
    |         +---> 确定配置路径（默认 /etc/qifeng-scm/selftest.json）
    |         +---> CheckerRunner::Run(configPath)
    |         |         |
    |         |         +---> load_config() 加载配置
    |         |         +---> register_all() + 脚本检查器
    |         |         +---> Runner::run_all() 并发执行
    |         |         +---> 汇总结果 + 写报告
    |         |
    |         +---> 输出摘要到控制台
    |         +---> 自检通过或 fail_action=warn -> 返回 true
    |         +---> 自检失败且 fail_action=halt -> 返回 false
    |
    +---> RunSelfCheck() 返回 false?
    |         |
    |         +---> 是 -> 进程退出（return 1），不进入服务循环
    |         +---> 否 -> 继续
    |
    +---> server.Start(socketPath)   // 启动 UDS 事件循环
    |         |
    |         +---> RecoverLastOperation()
    |         +---> RegisterHandlers()（含 CheckHandler）
    |         +---> UDS 初始化 + 事件循环
    |
    v
服务运行中（等待 scmctl 命令）
```

自检失败时的行为由 `scmd.yaml` 中 `selftest.fail_action` 控制：

| 值 | 行为 |
|----|------|
| `warn` | 仅告警，不阻止服务启动（默认） |
| `halt` | 阻止 scmd 启动，`main()` 返回 1 |

---

## 8. 自检报告格式

报告以 JSON 格式写入 `report_path` 指定的路径（默认 `/var/log/qifeng-scm/selftest-report.json`）：

```json
{
  "timestamp": "2026-06-30T10:00:00+08:00",
  "overall": "OK",
  "checks": {
    "disk": {
      "status": "PASS",
      "elapsed_ms": 12,
      "message": "all mounts OK",
      "details": {
        "/": "95% free",
        "/data": "88% free",
        "/opt/sophon": "72% free"
      }
    },
    "memory": {
      "status": "PASS",
      "elapsed_ms": 3,
      "message": "812MB available",
      "details": {
        "available_mb": "812"
      }
    },
    "network": {
      "status": "PASS",
      "elapsed_ms": 3021,
      "message": "gateway reachable",
      "details": {
        "gateway": "192.168.1.1",
        "ping_loss_pct": "0"
      }
    },
    "tpu": {
      "status": "PASS",
      "elapsed_ms": 150,
      "message": "TPU device OK",
      "details": {}
    },
    "model_inference": {
      "status": "PASS",
      "elapsed_ms": 520,
      "message": "inference OK",
      "details": {
        "model": "/opt/sophon/selftest/fsmn_fp32_.bmodel"
      }
    },
    "display": {
      "status": "WARN",
      "elapsed_ms": 8,
      "message": "no display connected",
      "details": {}
    },
    "fingerprint": {
      "status": "PASS",
      "elapsed_ms": 200,
      "message": "serial communication OK",
      "details": {
        "device": "/dev/ttyS3"
      }
    },
    "microphone": {
      "status": "PASS",
      "elapsed_ms": 450,
      "message": "RMS=120",
      "details": {
        "rms": "120"
      }
    },
    "light": {
      "status": "PASS",
      "elapsed_ms": 5,
      "message": "GPIO 488 high",
      "details": {}
    },
    "self_check_script": {
      "status": "PASS",
      "elapsed_ms": 3200,
      "message": "all sub-checks passed",
      "details": {
        "severity": "warning",
        "script": "/usr/lib/qifeng-scm/self-check",
        "exit_code": "0"
      }
    }
  }
}
```

---

## 9. 配置文件说明

### 9.1 selftest.json

路径：`/etc/qifeng-scm/selftest.json`

```json
{
  "per_item_timeout_sec": 5,
  "parallel": true,
  "report_path": "/var/log/qifeng-scm/selftest-report.json",
  "log_dir": "/var/log/qifeng-scm",
  "disk": { "mounts": ["/", "/data", "/opt/sophon"], "min_free_pct": 5 },
  "memory": { "min_available_mb": 128 },
  "fingerprint": { "device": "/dev/ttyS3", "baud": 57600, "timeout_ms": 1500 },
  "microphone": { "device": "default", "duration_ms": 400, "min_rms": 50 },
  "light": { "gpio": "488" },
  "network": { "gateway": "192.168.1.1", "ping_count": 3 },
  "model": { "path": "/opt/sophon/selftest/fsmn_fp32_.bmodel" },
  "scripts": [
    {
      "name": "self_check_script",
      "path": "/usr/lib/qifeng-scm/self-check",
      "severity": "warning",
      "timeout_sec": 60
    }
  ]
}
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `per_item_timeout_sec` | int | 5 | 每个检查项的超时秒数 |
| `parallel` | bool | true | 是否并发执行（false 为顺序模式，便于调试） |
| `report_path` | string | `/var/log/qifeng-scm/selftest-report.json` | JSON 报告文件路径 |
| `log_dir` | string | `/var/log/qifeng-scm` | 自检日志目录 |
| `disk.mounts` | string[] | `["/", "/data", "/opt/sophon"]` | 需检查的挂载点 |
| `disk.min_free_pct` | int | 5 | 最低可用空间百分比 |
| `memory.min_available_mb` | int | 128 | 最低可用内存 MB |
| `fingerprint.device` | string | `/dev/ttyS3` | 指纹模组串口设备 |
| `fingerprint.baud` | int | 57600 | 串口波特率 |
| `fingerprint.timeout_ms` | int | 1500 | 串口通信超时 ms |
| `microphone.device` | string | `default` | ALSA 录音设备名 |
| `microphone.duration_ms` | int | 400 | 录音时长 ms |
| `microphone.min_rms` | int | 50 | 最低 RMS 能量值 |
| `light.gpio` | string | `488` | LED GPIO 编号 |
| `network.gateway` | string | `192.168.1.1` | 网关地址（用于 ping 测试） |
| `network.ping_count` | int | 3 | ping 次数 |
| `model.path` | string | `/opt/sophon/selftest/fsmn_fp32_.bmodel` | 探测模型文件路径 |
| `scripts` | array | [] | 脚本检查器列表 |
| `scripts[].name` | string | - | 检查项名（报告 key，需唯一） |
| `scripts[].path` | string | - | 脚本绝对路径 |
| `scripts[].severity` | string | `warning` | 严重级别：`critical` 或 `warning` |
| `scripts[].timeout_sec` | int | 5 | 脚本执行超时秒数 |

### 9.2 scmd.yaml selftest 段

路径：`/etc/qifeng-scm/scmd.yaml`

```yaml
scmd:
  selftest:
    enabled: true                                    # 是否开机自检
    config_path: /etc/qifeng-scm/selftest.json       # 自检配置文件路径
    fail_action: warn                                # 自检失败动作
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `selftest.enabled` | bool | true | 是否在 scmd 启动时执行自检 |
| `selftest.config_path` | string | `/etc/qifeng-scm/selftest.json` | selftest.json 配置文件路径 |
| `selftest.fail_action` | string | `warn` | 自检失败动作：`warn`（仅告警）或 `halt`（阻止启动） |

---

## 10. 扩展指南

### 10.1 新增内置检查器

1. 在 `checker/include/checker/checkers/` 下新建头文件，实现 `IChecker` 接口
2. 在 `checker/src/checkers/` 下新建实现文件
3. 在 `checker/src/checkers/register_all.cpp` 中添加 `reg.add<YourChecker>();`
4. 在 `checker/CMakeLists.txt` 的 `add_library` 中添加源文件
5. 如需配置参数，在 `checker/include/checker/core/context.h` 的 `SelfTestConfig` 中添加子段

### 10.2 新增脚本检查器

无需修改代码，在 `selftest.json` 的 `scripts` 数组中添加配置即可：

```json
{
  "name": "my_check",
  "path": "/usr/lib/qifeng-scm/my-check.sh",
  "severity": "warning",
  "timeout_sec": 10
}
```

脚本需遵循退出码协议（0=pass, 1=fail, 2=skip, 3=warn）和可选的 JSON 协议行。
