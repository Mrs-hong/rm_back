# 设备自检功能使用指南

## 1. 概述

设备自检功能用于检测 BM1684 边缘设备的硬件和软件状态，在设备开机时自动执行，也可通过命令行手动触发。自检覆盖磁盘、内存、网络、TPU、模型推理、显示器、指纹模组、麦克风、LED 指示灯以及外部脚本检查等 10 余项检测，帮助运维人员快速定位设备异常。

---

## 2. 使用方式

### 2.1 开机自动自检

`scmd` 服务启动时会自动执行开机自检。自检完成后，控制台输出摘要：

```
[scmd] 开机自检完成: OK (5 passed, 0 failed, 2 warnings, 2 skipped)
```

自检失败时默认仅告警，不阻止服务启动。可通过配置文件修改此行为（见第 5 节）。

### 2.2 手动触发自检

使用 `qf_scmc check` 命令手动触发自检：

```bash
# 使用默认配置执行自检
qf_scmc check
```

输出示例：

```
Self-test OK: 5 passed, 0 failed, 2 warnings, 2 skipped
```

### 2.3 指定配置文件

通过 `--config` 参数指定自检配置文件路径：

```bash
qf_scmc check --config /path/to/selftest.json
```

---

## 3. 检查项说明

### 3.1 内置检查器

| 检查项 | 严重级别 | 说明 |
|--------|----------|------|
| disk | Critical | 检查各挂载点磁盘容量是否低于阈值，并对磁盘执行读写校验 |
| memory | Critical | 检查系统可用内存是否低于阈值，检测 ECC 错误 |
| network | Critical | 检查网卡状态是否正常，测试网关连通性（ping） |
| tpu | Critical | 检查 TPU 设备是否存在，验证 TPU 内存校验 |
| model_inference | Critical | 加载 bmodel 模型文件并执行推理，验证推理结果正确性 |
| display | Warning | 检查显示器是否连接（通过 libdrm 或 sysfs 探测） |
| fingerprint | Warning | 通过串口与指纹模组通信，验证模组响应 |
| microphone | Warning | 通过 ALSA 录制音频，计算 RMS 能量值是否达标 |
| light | Warning | 通过 libgpiod 读取 LED 指示灯电平状态 |

### 3.2 脚本检查器

| 检查项 | 严重级别 | 说明 |
|--------|----------|------|
| self_check_script | Warning | 调用外部脚本 `/usr/lib/qifeng-scm/self-check`，内部可能包含 ButtonComm、LcdSerial、TFCard、Fan 等子检查 |

脚本检查器的严重级别由 `selftest.json` 配置决定，可设为 `critical` 或 `warning`。

---

## 4. 自检结果说明

### 4.1 整体结果

| 结果 | 说明 |
|------|------|
| OK | 所有 critical 项均通过（warning 项失败不影响整体结果） |
| FAIL | 至少一个 critical 项失败 |

### 4.2 各检查项状态

| 状态 | 说明 |
|------|------|
| PASS | 通过：检查项正常 |
| FAIL | 失败：硬件存在但自检异常 |
| WARN | 告警：非阻断性问题 |
| SKIP | 跳过：所需资源不可用（硬件未装配或库未安装） |

### 4.3 结果判定规则

- critical 项 FAIL -> 整体结果为 FAIL
- warning 项 FAIL -> 整体结果仍为 OK（仅记录告警）
- SKIP 不影响整体结果（表示硬件/库未就绪，非故障）

---

## 5. 配置文件说明

### 5.1 selftest.json

路径：`/etc/qifeng-scm/selftest.json`

```json
{
  "per_item_timeout_sec": 5,
  "parallel": true,
  "report_path": "/var/log/qifeng-scm/selftest-report.json",
  "log_dir": "/var/log/qifeng-scm",
  "disk": {
    "mounts": ["/", "/data", "/opt/sophon"],
    "min_free_pct": 5
  },
  "memory": {
    "min_available_mb": 128
  },
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
  "light": {
    "gpio": "488"
  },
  "network": {
    "gateway": "192.168.1.1",
    "ping_count": 3
  },
  "model": {
    "path": "/etc/qifeng-scm/fsmn_fp32_.bmodel"
  },
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

各字段含义：

| 字段 | 说明 |
|------|------|
| `per_item_timeout_sec` | 每个检查项的超时秒数，超时则判定为 FAIL |
| `parallel` | 是否并发执行检查（true=并发，false=顺序便于调试） |
| `report_path` | 自检报告 JSON 文件输出路径 |
| `log_dir` | 自检日志存放目录 |
| `disk.mounts` | 需检查的磁盘挂载点列表 |
| `disk.min_free_pct` | 磁盘最低可用空间百分比，低于此值判定为 FAIL |
| `memory.min_available_mb` | 最低可用内存（MB），低于此值判定为 FAIL |
| `fingerprint.device` | 指纹模组串口设备路径 |
| `fingerprint.baud` | 串口通信波特率 |
| `fingerprint.timeout_ms` | 串口通信超时（毫秒） |
| `microphone.device` | ALSA 录音设备名 |
| `microphone.duration_ms` | 录音时长（毫秒） |
| `microphone.min_rms` | 最低 RMS 能量值，低于此值判定为 FAIL |
| `light.gpio` | LED 指示灯 GPIO 编号 |
| `network.gateway` | 网关 IP 地址，用于连通性测试 |
| `network.ping_count` | ping 测试次数 |
| `model.path` | 探测模型文件路径 |
| `scripts` | 外部脚本检查器列表 |
| `scripts[].name` | 脚本检查项名称（在报告中显示） |
| `scripts[].path` | 脚本文件绝对路径 |
| `scripts[].severity` | 严重级别：`critical`（失败影响整体结果）或 `warning`（仅告警） |
| `scripts[].timeout_sec` | 脚本执行超时秒数 |

### 5.2 scmd.yaml selftest 段

路径：`/etc/qifeng-scm/scmd.yaml`

```yaml
scmd:
  selftest:
    enabled: true                                    # 是否开机自检
    config_path: /etc/qifeng-scm/selftest.json       # 自检配置文件路径
    fail_action: warn                                # 自检失败动作
```

各字段含义：

| 字段 | 说明 |
|------|------|
| `selftest.enabled` | 是否在 scmd 启动时执行开机自检（true/false） |
| `selftest.config_path` | selftest.json 配置文件路径 |
| `selftest.fail_action` | 自检失败时的动作：`warn`（仅告警，不阻止启动）或 `halt`（阻止 scmd 启动） |

---

## 6. 自检报告

### 6.1 报告位置

自检报告以 JSON 格式写入：

```
/var/log/qifeng-scm/selftest-report.json
```

报告路径可通过 `selftest.json` 中的 `report_path` 字段自定义。

### 6.2 报告格式

```json
{
  "timestamp": "2026-06-30T10:00:00+08:00",
  "overall": "OK",
  "checks": {
    "disk": {
      "status": "PASS",
      "elapsed_ms": 12,
      "message": "all mounts OK",
      "details": { ... }
    },
    "memory": {
      "status": "PASS",
      "elapsed_ms": 3,
      "message": "812MB available",
      "details": { "available_mb": "812" }
    }
    ...
  }
}
```

| 字段 | 说明 |
|------|------|
| `timestamp` | 自检执行时间（ISO 8601 格式） |
| `overall` | 整体结果：`OK` 或 `FAIL` |
| `checks` | 各检查项详情 |
| `checks.<item>.status` | 检查项状态：`PASS`/`FAIL`/`WARN`/`SKIP` |
| `checks.<item>.elapsed_ms` | 检查耗时（毫秒） |
| `checks.<item>.message` | 一句话结论 |
| `checks.<item>.details` | 附加明细键值对 |

---

## 7. 部署路径

| 文件 | 路径 | 说明 |
|------|------|------|
| self-check 脚本 | `/usr/lib/qifeng-scm/self-check` | 外部自检脚本（ScriptChecker 调用） |
| self-check 配置 | `/etc/qifeng-scm/self-check.ini` | self-check 脚本的配置文件 |
| 探测模型 | `/etc/qifeng-scm/fsmn_fp32_.bmodel` | ModelInferenceChecker 使用的 bmodel 文件 |
| 自检配置 | `/etc/qifeng-scm/selftest.json` | 自检项配置文件 |
| 主配置 | `/etc/qifeng-scm/scmd.yaml` | scmd 主配置（含 selftest 段） |
| 自检报告 | `/var/log/qifeng-scm/selftest-report.json` | 自检结果 JSON 报告 |
| 自检日志 | `/var/log/qifeng-scm/selftest.log` | 自检详细日志 |

---

## 8. 常见问题排查

### 8.1 自检整体 FAIL

**步骤**：

1. 查看自检报告，定位 FAIL 的检查项：
   ```bash
   cat /var/log/qifeng-scm/selftest-report.json
   ```
2. 根据失败的检查项，参考下表排查

### 8.2 disk 检查失败

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| 磁盘容量不足 | 数据积累过多 | `df -h` 查看各挂载点可用空间 |
| 读写校验失败 | 磁盘坏块或只读 | `touch /data/test && rm /data/test` 测试写入 |

### 8.3 memory 检查失败

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| 可用内存不足 | 进程内存泄漏 | `free -m` 查看内存使用，`top` 排查高内存进程 |
| ECC 错误 | 硬件故障 | `dmesg | grep -i ecc` 查看 ECC 日志 |

### 8.4 network 检查失败

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| 网卡状态异常 | 网线未连接或驱动异常 | `ip link` 查看网卡状态 |
| 网关不可达 | 网络配置错误或网关离线 | `ping <gateway>` 手动测试，`ip route` 查看路由 |

### 8.5 tpu 检查失败

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| TPU 设备不存在 | Sophon SDK 未安装或驱动未加载 | `ls /dev/bm*` 查看设备节点，`lsmod | grep bm` 查看驱动 |
| TPU 内存校验失败 | 硬件故障 | 重新加载驱动 `sudo rmmod bm_pci && sudo modprobe bm_pci` |

### 8.6 model_inference 检查失败

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| 模型文件不存在 | 部署不完整 | `ls -la /etc/qifeng-scm/fsmn_fp32_.bmodel` 确认文件存在 |
| 模型加载失败 | TPU 异常或模型损坏 | 先修复 TPU，再检查模型文件 MD5 |
| 推理结果异常 | 模型与设备不匹配 | 确认模型为 BM1684 平台编译 |

### 8.7 display 检查为 SKIP/WARN

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| SKIP | libdrm 未安装 | `dpkg -l | grep libdrm` 确认安装 |
| WARN | 显示器未连接 | 正常现象（无头部署场景），不影响整体结果 |

### 8.8 fingerprint 检查失败

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| 串口打开失败 | 设备路径错误或权限不足 | `ls -la /dev/ttyS3` 确认设备存在且有权限 |
| 通信超时 | 指纹模组未响应 | 检查串口线连接，确认波特率配置正确 |

### 8.9 microphone 检查失败

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| SKIP | ALSA 库未安装 | `dpkg -l | grep libasound` 确认安装 |
| 录音失败 | 麦克风设备不可用 | `arecord -l` 查看录音设备列表 |
| RMS 低于阈值 | 麦克风静音或音量过低 | `alsamixer` 调整录音增益，或降低 `min_rms` 配置值 |

### 8.10 light 检查失败

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| SKIP | libgpiod 未安装 | `dpkg -l \| grep libgpiod` 确认安装 |
| GPIO 读取失败 | GPIO 编号错误或权限不足 | `gpiodetect` 查看芯片列表，`gpioinfo` 查看线状态，`gpioget gpiochip0 488` 手动读取验证 |

### 8.11 self_check_script 检查失败

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| SKIP | 脚本文件不存在或不可执行 | `ls -la /usr/lib/qifeng-scm/self-check` 确认文件存在且有执行权限 |
| FAIL | 脚本内部子检查失败 | 手动执行脚本查看输出：`bash /usr/lib/qifeng-scm/self-check` |
| 超时 | 脚本执行时间过长 | 增大 `selftest.json` 中 `timeout_sec` 配置值 |

### 8.12 自检报告未生成

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| 报告目录不存在 | 首次部署未创建 | `mkdir -p /var/log/qifeng-scm` |
| 权限不足 | scmd 进程无写入权限 | `ls -la /var/log/qifeng-scm/` 检查目录权限 |
