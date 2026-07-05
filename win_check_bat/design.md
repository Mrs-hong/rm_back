# Windows 端 BM1684x 边缘设备硬件自检方案分析

## 1. 需求回顾

- **目标设备**：BM1684x 半成品板子，出厂 Linux IP `192.168.112.47`，账号 `linaro/linaro`，root 密码 `linaro`。
- **待检硬件**：外接磁盘、风扇、显示器、指纹模组、麦克风等。
- **待测程序**：AI 推理程序（TPU 模型推理）。
- **执行端**：Windows 操作员机器，通过 bat 脚本启动整个自检流程。

## 2. 用户当前方案

1. 将厂家提供的硬件检测脚本汇总为一个总 `.sh` 脚本。
2. 基于已有 `qifeng-scm` 服务，把硬件检测脚本和 TPU 推理测试融合进 SCM，命令行形式 `qf_scmc checkxx`。
3. Windows 上放置 bat 与 deb 包，bat 负责：
   - 将 deb 包传输到设备；
   - 安装 SCM；
   - 执行 `qf_scmc check`；
   - 将结果详情返回到 Windows 终端。
4. 目前 `/home/hong/code/rm_back/include/checker/` 中已有大量 checker，当前阶段预计只使用：
   - `pcba`：调用汇总脚本；
   - `tpu`：检测 TPU 可用；
   - `model_inference`：检测 TPU 模型推理。

## 3. 方案合理性分析

### 3.1 优点

| 方面 | 说明 |
|---|---|
| **复用现有框架** | `qifeng-scm` 已具备配置驱动、并发 runner、JSON 报告、UDS 命令行等完整能力，无需重新造轮子。 |
| **统一报告格式** | 所有 checker 输出统一为 `CheckResult`，最终生成 `/var/log/qifeng-scm/selftest-report.json`，方便归档与自动化解析。 |
| **设备端易扩展** | `pcba_check.sh` 作为厂家脚本整合器，后续新增硬件只需把厂家脚本加入 `VENDOR_SCRIPTS` 数组，或在 `selftest.json` 中新增 checker 段，无需改 C++ 核心代码。 |
| **Windows 端轻量** | bat 只负责"上传-安装-触发-拉取"，业务逻辑放在设备端，便于维护和版本管理。 |
| **可无人值守** | 一次配置后，后续只需双击 bat 即可复测，适合产线环境。 |

### 3.2 不足与改进建议

| 不足 | 风险/影响 | 改进建议 |
|---|---|---|
| 密码可能硬编码在 bat 中 | 泄露风险，且不同批次设备密码可能不同 | 通过命令行参数或环境变量传入；模板中给出占位提示。 |
| bat 直接依赖 SSH/SCP | 旧版 Windows 没有自带 OpenSSH | 明确依赖 Windows 10/11 内置 OpenSSH；同时提供 Python 脚本作为替代。 |
| 安装 deb 后服务可能未就绪 | `dpkg -i` 后立即执行 `qf_scmc check` 可能因 `qf_scmd` 未启动而失败 | 安装后轮询 `systemctl is-active qifeng-scmd` 或 `qf_scmc list`，超时后再继续。 |
| deb 包版本号写死 | 升级 deb 后需要修改 bat | 脚本同目录自动匹配 `qifeng-scm_*.deb`，取最新修改时间者。 |
| 失败时远端日志不便查看 | Windows 端只能看到终端输出 | bat/python 在失败时主动拉取远端 journal 与报告文件，保存到本地 `logs/`。 |
| 缺乏回滚/卸载逻辑 | 多次安装旧版本残留可能导致问题 | 提供可选的 `uninstall` 流程或安装前 `dpkg -r qifeng-scm`。 |

### 3.3 推荐架构

```text
 Windows 操作员
       |
       | ssh.exe / scp.exe (OpenSSH)
       v
+---------------------------+
|  BM1684x 边缘设备         |
|  192.168.112.47           |
|                           |
|  qifeng-scm.deb 安装     |
|       |                   |
|       v                   |
|  qf_scmd 守护进程        |
|       ^                   |
|       | UDS               |
|  qf_scmc check            |
|       |                   |
|       v                   |
|  CheckerRunner            |
|  /--------/-------\       |
|  pcba    tpu    model     |
|  |        |       |       |
|  v        v       v       |
| pcba_   bm-sdk  bmodel   |
| check.sh                  |
+---------------------------+
       |
       | JSON 报告
       v
 Windows 终端 / 本地日志
```

## 4. 关键决策

1. **Windows 端优先使用内置 OpenSSH**：
   - 理由：Win10/Win11 已内置 `ssh.exe` 和 `scp.exe`，无需下载 PuTTY，减少分发依赖。
   - 兼容：若环境确实只有 PuTTY，可简单替换为 `plink.exe` / `pscp.exe`，命令结构类似。

2. **密码外置**：
   - 默认值为 `linaro`，但允许通过命令行参数或环境变量覆盖。
   - 避免将真实密码提交到版本控制。

3. **deb 包自动匹配**：
   - 脚本启动时在同目录搜索 `qifeng-scm_*.deb`。
   - 存在多个时按修改时间取最新，避免版本号硬编码。

4. **服务就绪轮询**：
   - `dpkg -i` 后等待 `qf_scmd` 进入 `active` 状态，最大等待 60 秒，降低首次连接失败概率。

5. **当前阶段精简配置**：
   - `selftest.json` 中仅保留 `pcba`、`tpu`、`model_inference`。
   - 其余 checker 不出现，因此不会实例化，后续可按需开启。

## 5. 风险点

| 风险 | 缓解措施 |
|---|---|
| 设备出厂 IP/密码变更 | 通过参数外置，bat/python 均支持自定义。 |
| 厂家脚本路径或格式不一致 | `pcba_check.sh` 作为模板，明确标注 TODO，由厂家脚本填充。 |
| TPU SDK 或 bmodel 不存在 | `tpu` / `model_inference` checker 在缺少依赖时返回 `SKIPPED`，不会直接失败。 |
| 外设未插导致检测失败 | `pcba` checker 的 `severity` 可配置为 `warning`，根据产线要求决定。 |
| Windows 端 OpenSSH 未启用 | 在 README 中给出启用方法；同时提供 Python 脚本作为备用。 |
| 网络不稳定导致 SCP/SSH 中断 | 关键命令后检查 `%ERRORLEVEL%` / `returncode`，失败即退出并打印日志。 |

## 6. 后续可扩展方向

- **增加更多 checker**：后续若需要检测内存、磁盘、网络、风扇等，只需在 `selftest.json` 中补充对应段。
- **结果可视化**：将 `selftest-report.json` 用网页或 Excel 解析，生成产线检测报表。
- **批量检测**：扩展 bat/python 支持读取设备列表，循环对多台设备执行检测。
- **CI/CD 集成**：将 Python 脚本接入 Jenkins/GitLab CI，实现自动化硬件回归测试。
