# Windows 端 BM1684x 边缘设备硬件自检

本目录提供一套 Windows 端一键自检方案，用于对出厂 IP 为 `192.168.112.47` 的 BM1684x 边缘设备进行硬件自检和 AI 程序测试。

## 文件清单

| 文件/目录 | 说明 |
|---|---|
| `design.md` | 方案合理性分析、改进建议与风险点 |
| `run_check.bat` | Windows 批处理脚本，双击即可执行 |
| `run_check.py` | 与 bat 等效的 Python 脚本，功能完全一致 |
| `scripts/pcba_check.sh` | 设备端 PCBA 硬件自检汇总脚本，用于整合板子提供商的硬件检测脚本 |
| `config/selftest.json` | 自检配置，仅启用 pcba / tpu / model_inference |

## 环境要求

- Windows 10/11，已启用 OpenSSH 客户端（`ssh.exe` / `scp.exe` 可在命令行直接使用）。
- 操作员电脑与 BM1684x 设备网络可达（默认 IP `192.168.112.47`）。
- 设备上已插入需要检测的外设：外接磁盘、风扇、显示器、指纹模组、麦克风等。
- 同目录下放置 `qifeng-scm_*.deb` 安装包。

## 启用 Windows OpenSSH

如果命令行中找不到 `ssh.exe`，按以下步骤启用：

1. 打开"设置" → "应用" → "可选功能"。
2. 点击"添加功能"，搜索并安装"OpenSSH 客户端"。
3. 重新打开命令行窗口，输入 `ssh -V` 确认。

## 使用流程

### 1. 准备文件

在 Windows 上创建一个目录，例如 `C:\bm1684x_check\`，将以下文件放入：

```text
C:\bm1684x_check\
├── run_check.bat
├── run_check.py
├── config\selftest.json
├── scripts\pcba_check.sh
└── qifeng-scm_*.deb
```

### 2. 修改配置

根据实际硬件情况修改以下文件：

- `config/selftest.json`
  - `pcba.severity`：`warning` 或 `critical`，决定 pcba 失败是否影响整体结果。
  - `model_inference.path`：设备端实际 bmodel 路径。
  - `per_item_timeout_sec` / `parallel`：根据检测项耗时调整。

- `scripts/pcba_check.sh`
  - 将各 `TODO` 处替换为厂家提供的实际检测脚本或命令。
  - 修改外接磁盘设备节点、指纹串口、显示器检测方式等。

### 3. 执行自检

#### 方式一：双击运行 bat

```bat
run_check.bat
```

或带参数：

```bat
run_check.bat 192.168.112.47 linaro
```

#### 方式二：运行 Python 脚本

```powershell
python run_check.py
```

或带参数：

```powershell
python run_check.py --ip 192.168.112.47 --user linaro --password linaro --root-password linaro
```

密码也可以通过环境变量传入，避免出现在命令历史中：

```powershell
$env:SSH_PASSWORD="linaro"
$env:ROOT_PASSWORD="linaro"
python run_check.py
```

### 4. 查看结果

- 终端会直接输出 `qf_scmc check` 的结果。
- 远端报告会被拉回到本地 `logs\selftest-report-<IP>.json`。
- 若失败，`logs\qf_scmd-<IP>.log` 会保存最近 50 条服务日志。

## 脚本执行流程

```text
run_check.bat / run_check.py
    │
    ├─ 检查 ssh.exe / scp.exe 是否存在
    ├─ 同目录自动匹配 qifeng-scm_*.deb
    ├─ SCP 上传 deb 到 /tmp/qifeng-scm.deb
    ├─ SSH 执行 sudo dpkg -i 安装
    ├─ 轮询等待 qf_scmd 服务 active
    ├─ SCP 上传 config/selftest.json 到 /etc/qifeng-scm/selftest.json
    ├─ SCP 上传 scripts/pcba_check.sh 到 /opt/qifeng-scm/scripts/pcba_check.sh
    ├─ SSH 执行 qf_scmc check --config /etc/qifeng-scm/selftest.json
    └─ SCP 拉回 /var/log/qifeng-scm/selftest-report.json
```

## 如何根据实际情况调整

### 整合厂家硬件检测脚本

`scripts/pcba_check.sh` 的核心作用是**调用板子提供商的硬件检测脚本**，而不是重新实现所有检测逻辑。

1. 将厂家提供的检测脚本放到设备端 `/opt/qifeng-scm/vendor/` 目录（也可通过环境变量 `VENDOR_DIR` 指定其他路径）。
2. 修改 `scripts/pcba_check.sh` 顶部的 `VENDOR_SCRIPTS` 数组，填入厂家脚本的绝对路径或相对 `VENDOR_DIR` 的相对路径：

    ```bash
    VENDOR_SCRIPTS=(
        "/opt/qifeng-scm/vendor/check_disk.sh"
        "/opt/qifeng-scm/vendor/check_fan.sh"
        "check_display.sh"          # 相对路径，会自动补全为 ${VENDOR_DIR}/check_display.sh
    )
    ```

3. 厂家脚本需要满足：
    - 可执行（`chmod +x`）；
    - 退出码 `0` 表示通过，非 `0` 表示失败；
    - 输出中建议包含 `PASS` / `FAIL` 关键字，便于 pcba checker 做输出解析。

4. 若厂家未提供某个单项脚本，可在 `scripts/pcba_check.sh` 的内联检测区补充 `check_xxx` 函数实现。

### 新增硬件检测项

1. 在 `scripts/pcba_check.sh` 中新增一个 `check_xxx` 函数。
2. 在主流程中调用该函数并使用 `report_result` 记录结果。
3. 确保函数退出码为 0 表示通过，非 0 表示失败。

### 调整检测项严重级别

修改 `config/selftest.json` 中 `pcba.severity`：

- `critical`：pcba 失败会导致整体自检不通过。
- `warning`：pcba 失败只记录，不影响整体结果。

### 启用更多 checker

若后续需要启用 `disk`、`memory`、`network`、`fan`、`display`、`microphone` 等 checker：

1. 在 `config/selftest.json` 中补充对应配置段。
2. 将配置文件和必要资源一起上传到设备端固定路径。
3. 无需修改 C++ 核心代码，SCM 框架会根据配置自动实例化对应 checker。

### 更换 deb 包版本

无需修改 bat/python 脚本。将新版本的 `qifeng-scm_*.deb` 放入同目录，脚本会自动匹配最新修改时间的包。

### 多台设备批量检测

当前脚本为单设备设计。批量检测可：

- 编写一个外层批处理/ PowerShell 循环调用 `run_check.py --ip <ip>`。
- 或扩展 Python 脚本读取 `devices.txt` 列表。

## SCM 服务集成说明

- `qf_scmd` 为守护进程，负责接收 `qf_scmc` 命令并执行服务生命周期和自检。
- `qf_scmc check --config <path>` 会触发 `CheckerRunner` 加载指定 `selftest.json`。
- 只有 JSON 中出现的顶层 key 才会被实例化为 checker，因此精简配置即可只跑需要的项。
- 自检报告默认写入 `/var/log/qifeng-scm/selftest-report.json`。

## 故障排查

| 现象 | 排查方向 |
|---|---|
| `ssh.exe 未找到` | 确认 Windows OpenSSH 客户端已安装并加入 PATH。 |
| `未找到 qifeng-scm_*.deb` | 确认 deb 包与脚本在同一目录。 |
| `上传 deb 包失败` | 检查网络是否可达，IP/用户名/密码是否正确。 |
| `安装 qifeng-scm 失败` | 查看 `logs\qf_scmd-<IP>.log`，检查依赖冲突。 |
| `等待 qf_scmd 服务超时` | SSH 执行 `systemctl status qifeng-scmd` 查看服务状态。 |
| `自检执行失败` | 查看终端输出和 `logs\selftest-report-<IP>.json` 中的详细结果。 |
| `pcba 检测项失败` | 检查外设是否插好，并核对 `scripts/pcba_check.sh` 中的设备节点。 |
| `model_inference 失败` | 确认 bmodel 路径正确，TPU SDK 已安装，`TEST_PROGRAM` 可执行。 |

## 注意事项

- 密码默认使用设备出厂值 `linaro`，生产环境建议通过参数或环境变量传入。
- `StrictHostKeyChecking=no` 用于简化首次连接，生产环境建议预先分发 host key。
- 当前 `scripts/pcba_check.sh` 为模板，包含大量 TODO，接入实际厂家脚本前请勿直接用于产线。
- 厂家提供的硬件检测脚本建议统一放到设备端 `/opt/qifeng-scm/vendor/` 目录，并在 `scripts/pcba_check.sh` 的 `VENDOR_SCRIPTS` 数组中注册。
