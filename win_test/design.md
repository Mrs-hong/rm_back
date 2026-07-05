# Windows 端 BM1684x 边缘设备硬件自检 - 方案设计与集成指南

## 1. 需求回顾

- **目标设备**：BM1684x 半成品板子，出厂 Linux IP `192.168.112.47`，账号 `linaro/linaro`，root 密码 `linaro`
- **待检硬件**：外接磁盘（读写压力测试）、风扇、显示器、指纹模组、麦克风等
- **待测程序**：AI 推理程序（TPU 模型推理）
- **执行端**：Windows 操作员机器，通过 bat/python 脚本启动整个自检流程
- **核心依赖**：设备厂家提供半成品板子及各种硬件检测脚本，我方负责整合与自动化

## 2. 用户当前方案

1. 将厂家提供的硬件检测脚本汇总为一个总 `.sh` 脚本（`pcba_check.sh`），按合理逻辑完成各种硬件检测
2. 基于已有 `qifeng-scm` 服务，把硬件检测脚本和 TPU 推理测试融合进 SCM，命令行形式 `qf_scmc check`
3. Windows 上放置 bat 与 deb 包，bat 负责：上传 deb → 安装 SCM → 执行 `qf_scmc check` → 结果返回 Windows 终端
4. 当前阶段仅使用：`pcba`（调用汇总脚本）、`tpu`（检测 TPU 可用）、`model_inference`（TPU 模型推理）

## 3. 方案合理性分析

### 3.1 优点

| 方面 | 说明 |
|------|------|
| **复用现有框架** | `qifeng-scm` 已具备配置驱动、并发 runner、JSON 报告、UDS 命令行等完整能力，无需重新造轮子 |
| **统一报告格式** | 所有 checker 输出统一为 `CheckResult`，最终生成 `/var/log/qifeng-scm/selftest-report.json`，方便归档与自动化解析 |
| **设备端易扩展** | `pcba_check.sh` 作为厂家脚本整合器，后续新增硬件只需把厂家脚本加入 `VENDOR_SCRIPTS` 数组，或在 `selftest.json` 中新增 checker 段，无需改 C++ 核心代码 |
| **Windows 端轻量** | bat/python 只负责"上传-安装-触发-拉取"，业务逻辑放在设备端，便于维护和版本管理 |
| **可无人值守** | 一次配置后，后续只需双击 bat 或运行 python 即可复测，适合产线环境 |
| **配置驱动** | checker 启用/参数全部由 `selftest.json` 控制，无需改 C++ 代码即可增减检测项 |

### 3.2 不足与改进建议

| 不足 | 风险/影响 | 改进建议 |
|------|-----------|----------|
| **密码认证机制** | bat 版本依赖 OpenSSH，`scp.exe` 不支持命令行传密码，会卡在密码提示 | bat 版通过 SSH 密钥预配置实现免密；Python 版使用 paramiko 原生支持密码认证 |
| **安装后服务未就绪** | `dpkg -i` 后立即执行 `qf_scmc check` 可能因 `qf_scmd` 未启动而失败 | 安装后轮询 `systemctl is-active qifeng-scmd`，超时 60 秒后再继续 |
| **缺乏复测模式** | 每次都要重新安装 deb，浪费时间 | 增加 `--skip-install` 参数，设备已安装 SCM 时直接执行检查 |
| **失败时日志不便查看** | Windows 端只能看到终端输出 | 失败时主动拉取远端 journal 日志和 selftest-report.json，保存到本地 `logs/` |
| **deb 包版本号写死** | 升级 deb 后需要修改脚本 | 脚本同目录自动匹配 `qifeng-scm_*.deb`，按修改时间取最新 |
| **密码硬编码风险** | 泄露风险，且不同批次设备密码可能不同 | 密码通过命令行参数或环境变量传入，默认值仅作占位提示 |

### 3.3 推荐架构

```
 Windows 操作员
       |
       | bat (OpenSSH + 密钥)  /  python (paramiko)
       v
+-------------------------------------------+
|  BM1684x 边缘设备 (192.168.112.47)         |
|                                           |
|  qifeng-scm.deb 安装                      |
|       |                                   |
|       v                                   |
|  qf_scmd 守护进程 (systemd)               |
|       ^                                   |
|       | UDS (Unix Domain Socket)          |
|  qf_scmc check --config selftest.json     |
|       |                                   |
|       v                                   |
|  CheckerRunner                            |
|  /--------/--------/--------\             |
|  pcba    tpu    model_inference            |
|  |        |         |                     |
|  v        v         v                     |
| pcba_   bm-sdk   bmodel推理               |
| check.sh                                  |
|  |                                        |
|  v                                        |
| 厂家脚本1, 厂家脚本2, ...                  |
+-------------------------------------------+
       |
       | selftest-report.json (SCP/SFTP 拉回)
       v
 Windows 本地 logs/ 目录
```

## 4. 关键决策

### 4.1 Windows 端 SSH 方案

| 方案 | 优点 | 缺点 | 适用场景 |
|------|------|------|----------|
| **OpenSSH + 密钥（bat）** | Win10/11 内置，无需额外安装 | 密码输入需要交互，首次需配置密钥 | 有 OpenSSH 的环境 |
| **paramiko（Python）** | 原生密码认证，SFTP 替代 scp，跨平台 | 需 `pip install paramiko` | 通用环境，推荐首选 |
| **PuTTY（plink/pscp）** | 老系统兼容 | 额外安装，命令语法不同 | 仅旧版 Windows |

**推荐**：优先使用 Python + paramiko 方案，bat 版本作为补充。

### 4.2 密码外置

- 默认值为 `linaro`，但允许通过命令行参数或环境变量覆盖
- bat：`set SSH_PASSWORD=xxx` 或命令行第 3 个参数
- python：`--password xxx` 或环境变量 `SSH_PASSWORD`
- 避免将真实密码提交到版本控制

### 4.3 deb 包自动匹配

- 脚本启动时在同目录搜索 `qifeng-scm_*.deb`
- 存在多个时按修改时间取最新，避免版本号硬编码

### 4.4 服务就绪轮询

- `dpkg -i` 后等待 `qf_scmd` 进入 `active` 状态
- 最大等待 60 秒，每 2 秒轮询一次
- 超时则拉取远端日志并退出

### 4.5 当前阶段精简配置

- `selftest.json` 中仅保留 `pcba`、`tpu`、`model_inference` 三项
- 其余 checker 不出现，因此不会实例化
- 后续可按需在 JSON 中新增 checker 段来启用更多检测项

## 5. SCM 服务集成方式

### 5.1 整体架构

```
qf_scmc (CLI客户端)
    |--- UDS/JSON ---> qf_scmd (守护进程)
                          |
                          +-- 开机自检: ScmServer::RunSelfCheck()
                          +-- 手动触发: CheckHandler::Handle()
                                |
                                v
                          CheckerRunner::Run(loader)
                                |
                                v
                          CheckerRegistry::BuildAll() -- 仅实例化 JSON 中有 key 的 checker
                                |
                                v
                          Runner::RunAll() -- 并发/顺序执行 + 超时保护
                                |
                                v
                          汇总结果 -> selftest-report.json
```

### 5.2 selftest.json 中 checker 增减方法

**核心规则**：`CheckerRegistry::BuildAll()` 遍历所有已注册的 checker 工厂，仅当 JSON 根对象包含该 checker 的 `ClassName()` 作为 key 时，才实例化并注入配置。**不在 JSON 中出现的 checker 不会执行。**

**启用一个 checker**：在 JSON 根对象中添加对应 key 段即可。

```json
{
  "pcba": { ... },
  "tpu": { ... },
  "model_inference": { ... },
  "disk": { "min_free_gb": 10 }
}
```

**禁用一个 checker**：从 JSON 中删除对应 key 段即可。

### 5.3 各 checker 参数含义与调整

#### 全局参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `per_item_timeout_sec` | int | 5 | 单项 checker 超时（秒），超时后该 checker 返回 FAIL |
| `parallel` | bool | true | 是否并发执行 checker，产线调试建议 false |
| `report_path` | string | - | 报告文件输出路径 |

#### pcba checker

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `exe_command` | string | 必填 | 要执行的外部脚本路径，如 `/opt/qifeng-scm/scripts/pcba_check.sh` |
| `args` | array | [] | 传递给脚本的命令行参数 |
| `severity` | string | "warning" | `critical`=失败则整体FAIL, `warning`=仅记录 |
| `timeout_sec` | int | 300 | 脚本执行超时（秒），PCBA 检测耗时较长，建议设大 |
| `parse_output` | bool | true | 是否解析脚本输出中的 PASS/FAIL 关键字 |
| `pass_keyword` | string | "PASS" | 输出中包含此关键字视为通过 |
| `fail_keyword` | string | "FAIL" | 输出中包含此关键字视为失败 |

#### tpu checker

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `min_mem_mb` | int | 0 | TPU 可用内存最小值（MB），0 表示仅检测 TPU 是否可用 |

#### model_inference checker

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `path` | string | 必填 | bmodel 模型文件路径，如 `/opt/sophon/selftest/fsmn_fp32_.bmodel` |

#### 其他 checker（当前未启用，按需开启）

| Checker | ClassName | 参数示例 | 说明 |
|---------|-----------|----------|------|
| DiskChecker | `disk` | `{"min_free_gb": 10}` | 检查磁盘剩余空间 |
| MemoryChecker | `memory` | `{"min_available_mb": 512}` | 检查可用内存 |
| NetworkChecker | `network` | `{"gateway": "192.168.1.1"}` | ping 网关测试连通性 |
| FanChecker | `fan` | `{}` | 检测风扇转速和温度 |
| DisplayChecker | `display` | `{}` | 检测显示器连接状态 |
| FingerprintChecker | `fingerprint` | `{"device": "/dev/ttyS3"}` | 串口通信检测指纹模组 |
| MicrophoneChecker | `microphone` | `{"duration_sec": 2, "min_rms": 100}` | ALSA 录音检测麦克风 |

### 5.4 pcba checker 与 pcba_check.sh 的交互机制

```
pcba checker (C++)                     pcba_check.sh (Bash)
     |                                       |
     |--- fork+exec: exe_command ----------->|
     |                                       |--- 执行厂家脚本列表
     |                                       |--- 执行内联检测函数
     |<-- 退出码 + stdout/stderr ------------|
     |                                       |
     | 退出码 0 = PASS                        |
     | 退出码 1 = FAIL                        |
     | 退出码 2 = SKIP                        |
     |                                       |
     | 若 parse_output=true:                  |
     |   stdout 含 PASS 关键字 → 记录通过      |
     |   stdout 含 FAIL 关键字 → 记录失败      |
```

**退出码协议**（pcba_check.sh 必须遵守）：
- `exit 0`：全部检测项通过
- `exit 1`：存在失败项
- `exit 2`：跳过（可选，如检测环境不满足）

**输出关键字协议**（当 `parse_output=true` 时）：
- 输出中包含 `PASS` 关键字 → checker 记录为通过信息
- 输出中包含 `FAIL` 关键字 → checker 记录为失败信息
- 同时包含时，`FAIL` 优先级更高

### 5.5 severity 配置策略

| severity 值 | 行为 | 适用场景 |
|-------------|------|----------|
| `critical` | 该 checker 失败时，整体自检结果为 FAIL | TPU 可用性、模型推理等关键项 |
| `warning` | 该 checker 失败时仅记录警告，不影响整体结果 | 外设检测（风扇、显示器等非核心项） |

**当前阶段建议**：
- `tpu`：默认 critical（TPU 不可用则设备无意义）
- `model_inference`：默认 critical（核心功能）
- `pcba`：设为 warning（外设检测失败不应阻止流程）

## 6. 配置调整指南

### 6.1 根据实际硬件增减检测项

**场景 1：增加磁盘压力测试**

在 `selftest.json` 中添加 `disk` 段：
```json
{
  "disk": {
    "min_free_gb": 10
  }
}
```

**场景 2：增加风扇检测**

在 `selftest.json` 中添加 `fan` 段：
```json
{
  "fan": {}
}
```

**场景 3：只保留核心检测（当前方案）**

JSON 中只保留 `pcba`、`tpu`、`model_inference`，其余不出现即不执行。

### 6.2 根据厂家脚本修改 pcba_check.sh

**步骤**：

1. 获取厂家提供的硬件检测脚本，上传到设备端 `/opt/qifeng-scm/vendor/` 目录
2. 在 `pcba_check.sh` 的 `VENDOR_SCRIPTS` 数组中添加脚本路径：
   ```bash
   VENDOR_SCRIPTS=(
       "/opt/qifeng-scm/vendor/check_disk.sh"
       "/opt/qifeng-scm/vendor/check_fan.sh"
   )
   ```
3. 确保厂家脚本满足退出码协议（0=通过，非0=失败）
4. 如果厂家脚本不满足退出码协议，可在 `run_vendor_script()` 中包装转换逻辑
5. 如果厂家未提供某项检测脚本，可在内联检测函数中自行实现或保留占位

### 6.3 超时与并发参数调整

| 参数 | 调整建议 |
|------|----------|
| `per_item_timeout_sec` | 产线环境建议 120s，防止单项检测卡住；开发调试可用默认 5s |
| `parallel` | 产线建议 `false`（顺序执行便于观察和调试）；正式部署可改为 `true` 加速 |
| `pcba.timeout_sec` | PCBA 检测涉及多种外设，建议 300s 或更长 |
| `report_path` | 默认 `/var/log/qifeng-scm/selftest-report.json`，一般无需修改 |

## 7. 完整使用流程

### 7.1 准备阶段

1. **Windows 环境准备**
   - 方式 A（Python）：安装 Python 3.7+ 和 paramiko（`pip install paramiko`）
   - 方式 B（bat）：确保 Windows 10/11 内置 OpenSSH 已启用（设置 → 应用 → 可选功能 → OpenSSH 客户端）

2. **构建 deb 包**
   ```bash
   # 在项目根目录
   mkdir build && cd build
   cmake .. && make -j$(nproc)
   cpack -G DEB
   # 产出的 deb 包在当前目录
   ```

3. **网络连通**
   - 确保 Windows 与设备在同一网段（设备出厂 IP `192.168.112.47`）
   - `ping 192.168.112.47` 验证连通性

4. **准备文件**
   - 将 deb 包、脚本、配置放到同一目录：
     ```
     win_test/
     ├── run_check.bat          # bat 脚本
     ├── run_check.py           # Python 脚本
     ├── qifeng-scm_x.x.x_arm64.deb  # deb 包
     ├── config/
     │   └── selftest.json      # 自检配置
     └── scripts/
         └── pcba_check.sh      # PCBA 检测脚本
     ```

### 7.2 首次运行（安装 SCM + 执行自检）

**Python 版（推荐）**：
```cmd
python run_check.py --ip 192.168.112.47 --user linaro --password linaro
```

**bat 版**：
```cmd
run_check.bat 192.168.112.47 linaro
```

**执行流程**：
1. 脚本检查依赖（Python 检查 paramiko，bat 检查 ssh.exe/scp.exe）
2. 自动匹配同目录的 `qifeng-scm_*.deb`
3. 上传 deb 包到设备 `/tmp/qifeng-scm.deb`
4. 远端执行 `sudo dpkg -i` 安装 SCM
5. 等待 `qf_scmd` 服务就绪（最多 60 秒）
6. 上传 `selftest.json` 到 `/etc/qifeng-scm/selftest.json`
7. 上传 `pcba_check.sh` 到 `/opt/qifeng-scm/scripts/pcba_check.sh` 并设置可执行权限
8. 执行 `qf_scmc check --config /etc/qifeng-scm/selftest.json`
9. 拉回 `selftest-report.json` 到本地 `logs/` 目录
10. 输出结果摘要

### 7.3 日常复测（跳过安装）

如果设备已安装 SCM，使用 `--skip-install` 跳过安装步骤：

```cmd
python run_check.py --skip-install
```

```cmd
run_check.bat --skip-install
```

此时只执行：上传配置 → 上传脚本 → 执行检查 → 拉回报告。

### 7.4 结果查看与解读

**终端输出**：脚本执行过程中会实时显示远端输出。

**JSON 报告**：拉回的 `logs/selftest-report-<IP>.json` 包含完整检测详情：

```json
{
  "overall": "OK",
  "summary": "3 passed, 0 failed, 0 warnings, 0 skipped",
  "checks": {
    "pcba": { "status": "PASS", "message": "...", "elapsed_ms": 12345 },
    "tpu": { "status": "PASS", "message": "TPU available", "elapsed_ms": 678 },
    "model_inference": { "status": "PASS", "message": "Inference OK", "elapsed_ms": 2345 }
  }
}
```

| 字段 | 含义 |
|------|------|
| `overall` | `OK` = 所有关键项通过, `FAIL` = 存在 critical 项失败 |
| `status` | `PASS`/`FAIL`/`WARNING`/`SKIPPED` |
| `message` | 该检测项的描述信息 |
| `elapsed_ms` | 该检测项耗时（毫秒） |

## 8. 风险点与缓解措施

| 风险 | 缓解措施 |
|------|----------|
| 设备出厂 IP/密码变更 | 脚本支持 `--ip`/`--password` 参数，默认值可覆盖 |
| 厂家脚本路径或格式不一致 | `pcba_check.sh` 作为模板，明确标注 TODO，由厂家脚本填充 |
| TPU SDK 或 bmodel 不存在 | `tpu`/`model_inference` checker 在缺少依赖时返回 SKIPPED，不会直接失败 |
| 外设未插导致检测失败 | `pcba` 的 `severity` 可配置为 `warning`，根据产线要求决定是否阻断流程 |
| Windows 端 OpenSSH 未启用 | 提供 Python + paramiko 替代方案，无需 OpenSSH |
| 网络不稳定导致传输中断 | 关键步骤后检查返回码，失败即退出并拉取远端日志 |
| SCM 服务安装后未正常启动 | 轮询等待 60 秒，超时拉取 journal 日志辅助排查 |
| 多次安装旧版本残留 | 安装前可选执行 `dpkg -r qifeng-scm` 卸载旧版 |

## 9. 后续可扩展方向

- **增加更多 checker**：在 `selftest.json` 中补充对应段即可启用 `disk`/`memory`/`network`/`fan`/`display`/`fingerprint`/`microphone` 等检测
- **结果可视化**：将 `selftest-report.json` 用网页或 Excel 解析，生成产线检测报表
- **批量检测**：扩展脚本支持读取设备列表文件，循环对多台设备执行检测
- **CI/CD 集成**：将 Python 脚本接入 Jenkins/GitLab CI，实现自动化硬件回归测试
- **自动回滚**：安装失败时自动卸载并恢复旧版本
- **远程配置下发**：通过 SCM 服务的 reload 机制动态更新自检配置，无需重新上传
