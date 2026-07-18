# win_deploy — Windows 网线直连产线部署工具

> 一套适用于 Win7/Win10/Win11 全兼容的"单机点击"产线方案：
> 通过网线直连将 Linux 脚本与文件部署到 BM1684x 边缘设备，实时回传自检结果，
> 完成流水线作业。

---

## 一、目录结构

```
win_deploy/
├── run.bat                # 主入口（产线工人双击运行）
├── setup_network.bat      # 一次性网卡配置（IT 管理员运行）
├── config.ini             # 唯一配置文件（IT 预置，工人不修改）
├── lib/                   # bat 工具库
│   ├── ini.bat            # config.ini 解析器
│   ├── log.bat            # 双输出日志（控制台 + 结果文件）
│   ├── net_check.bat      # 设备可达性预检
│   ├── ssh.bat            # plink/pscp 封装（hostkey/重试/超时）
│   ├── timeout.bat        # 本地命令超时包装
│   └── banner.bat         # 醒目 PASS/FAIL 横幅 + 蜂鸣
├── ps/
│   └── stream_decode.ps1  # PS 2.0 兼容的流式 UTF-8 解码器 + 全局超时
├── tools/
│   ├── plink.exe          # 32 位 PuTTY（Win7+ 全兼容）
│   └── pscp.exe
├── scripts/               # 设备端脚本
│   ├── _colors.sh         # 公共 ANSI 颜色码
│   ├── pcba_check.sh      # 8 阶段编排器
│   ├── check_scm_deps.sh  # SCM 依赖检查
│   ├── press_check_ssd.sh # SSD 压力测试
│   └── huayu/             # 厂商硬件检测脚本
├── config/
│   └── selftest.json      # SCM 自检阈值配置
├── deb/                   # deb 安装包放置区（IT 拷入）
│   ├── qifeng-scm-*.deb
│   └── qifeng-scm-*.deb.sha256
├── results/               # 结果归档（自动创建）
│   ├── check_<时间戳>_<IP>.txt   # 单次详细结果
│   └── batch_log.csv              # 批量汇总（IP,SN,结果,描述）
└── README.md
```

---

## 二、环境要求

| 项 | 要求 |
|---|---|
| 操作系统 | Windows 7 SP1 / Windows 10 / Windows 11（32/64 位均可） |
| PowerShell | 2.0 及以上（Win7 默认即满足） |
| 权限 | 日常运行 `run.bat` 用普通用户；首次 `setup_network.bat` 需管理员 |
| 网络 | 一根网线直连 Windows ↔ 设备 |
| 外部依赖 | **无**（plink/pscp 已随包，不需要 OpenSSH） |

---

## 三、首次部署（IT 操作，仅一次）

### 3.1 准备文件
1. 将最新 `qifeng-scm-<版本>.deb` 与对应 `.sha256` 拷入 `win_deploy\deb\`
2. 编辑 `config.ini`，按现场实际填写：
   ```ini
   [device]
   ip=192.168.112.223          ; 设备固定 IP
   ssh_user=linaro
   ssh_password=linaro
   root_password=linaro
   vendor=huayu                ; 设备厂商
   ```
3. （可选）将离线依赖包目录命名为 `offline-debs\` 放到 `win_deploy\` 下
   （含 `install_offline.sh`；当设备缺依赖时自动上传）

### 3.2 配置网卡副地址
1. 用网线连接 Windows 主机的网卡与设备
2. **右键** `setup_network.bat` → **以管理员身份运行**
3. 脚本列出所有"已连接"网卡，选择连设备的那块（多块时手动选序号）
4. 脚本自动为该网卡追加 `192.168.112.250/24` 副地址（非破坏性，不影响主网络/上网）
5. 自动 ping 验证；成功会显示绿色横幅 + 蜂鸣

> 之后日常使用无需再运行 setup_network.bat（除非换主机/换网口）。

---

## 四、产线日常使用（工人操作）

1. **双击 `run.bat`**
2. 脚本自动完成：上传文件 → 远程执行 → 实时显示 → 横幅判 PASS/FAIL
3. 完成后自动倒计时进入下一台（默认 5 秒，可在 config.ini 改）
4. 工人换下一台设备，插好网线，等设备开机后任意键继续
5. 全部完成后按 `N` 退出

**结果归档**：
- `results\check_<时间戳>_<IP>.txt` — 单台详细结果
- `results\batch_log.csv` — 批量汇总（可用 Excel 打开追溯）

---

## 五、config.ini 配置参考

| 节 | 键 | 默认值 | 说明 |
|---|---|---|---|
| `[device]` | ip | 192.168.112.223 | 设备 IP |
| | ssh_user | linaro | SSH 用户名 |
| | ssh_password | linaro | SSH 密码 |
| | root_password | linaro | root 密码（sudo -S 用，绝不落盘） |
| | ssh_port | 22 | SSH 端口 |
| | vendor | huayu | 厂商（决定 scripts/\<vendor>/） |
| `[paths]` | remote_testcheck_dir | /data/testcheck | 设备端测试根目录 |
| | remote_log | /tmp/pcba_check_output.log | 设备端日志路径 |
| | local_deb_dir | deb | 本地 deb 包目录 |
| | local_results_dir | results | 本地结果归档目录 |
| `[timing]` | exec_timeout_sec | 600 | 整体执行超时（秒）|
| | single_script_timeout_sec | 120 | 设备端单脚本超时（秒）|
| | ssh_retry | 3 | SSH 短命令重试次数 |
| | scp_retry | 3 | 上传重试次数 |
| | retry_backoff_base | 2 | 退避基数（2/4/8 秒）|
| `[production]` | auto_next | true | 完成后自动进入下一台 |
| | auto_next_delay_sec | 5 | 自动下一台倒计时 |
| | beep_on_finish | true | 完成时蜂鸣 |
| | result_keep_count | 0 | 结果保留份数（0=不限）|

---

## 六、产线作业 SOP

1. **班前**：IT 确认 `deb\` 下是最新 deb + sha256，确认 `config.ini` 配置正确
2. **首件**：双击 `run.bat`，验证首台 PASS，确认产线 PC 网卡配置有效
3. **流水**：
   - 工人插网线 → 设备开机 → 双击 run.bat（或上一台结束后自动进入下一台）
   - 看到 **绿色 PASS** 横幅 → 设备合格，放行
   - 看到 **红色 FAIL** 横幅 → 设备不合格，记录 `batch_log.csv` 中条目送修
4. **班后**：导出 `results\batch_log.csv` 作为产量/良率记录

---

## 七、故障排查

| 现象 | 可能原因 | 处理 |
|---|---|---|
| `[ERROR] 无法连接设备` | 网线未插/设备未开机/网卡未配 | 检查网线指示灯；首次运行 setup_network.bat |
| `[ERROR] SSH 凭据验证失败` | 密码错/IP 错/SSH 服务未启 | 校对 config.ini；设备端 `systemctl status sshd` |
| `[ERROR] 整体执行超时` | 设备端某脚本卡死 | 查看 results/单次结果定位卡在哪个阶段；调大 single_script_timeout_sec |
| 中文乱码 | 控制台 codepage 异常 | 已自动 chcp 65001；若仍乱码，确认 PS ≥ 2.0 |
| `未找到 qifeng-scm*.deb` | deb 包未放入 | 将 deb 拷入 `deb\` 目录 |
| 横幅后窗口立即关闭 | 自动下一台倒计时已到 | 正常；按 N 退出可手动结束 |
| plink 提示 host key 变更 | 设备重装系统后 host key 变了 | 运行 `reg delete HKCU\Software\SimonTatham\PuTTY /f` 清缓存后重试 |

---

## 八、关键设计决策

- **零外部依赖**：仅依赖随包 32 位 plink/pscp，Win7+ 全兼容，无需 OpenSSH
- **root 密码不落盘**：通过 `echo PWD \| sudo -S` stdin 管道喂入，绝不写入 `/tmp/.sudo_pwd`
- **全局超时**：PS 侧 `WaitForExit(timeout)` + `Kill()`，设备端 `timeout 120` 包装每个子脚本
- **退出码可靠**：直接取 plink 退出码（pipefail 保证），不靠日志文本 grep
- **网卡非破坏性**：追加副地址，不删除原 IP，不影响主机上网
- **PS 2.0 兼容**：仅用 .NET 2.0 API，Win7 默认环境可用

---

## 九、与旧方案 (win_check_scirpt) 的差异

详见 `.trae/documents/win_deploy_100pct_solution.md` 第二章"现状存在的问题"。
新方案解决了旧方案的 13 项缺陷，主要改进：

| 维度 | 旧方案 | 新方案 |
|---|---|---|
| Win7 兼容 | OpenSSH 兜底需输密码 | 仅靠随包 plink，零交互 |
| 网卡配置 | 无 | 一次性 setup_network.bat |
| 主机密钥 | `echo y \| plink` 脆弱 | 缓存 + 重试 |
| root 密码 | 落盘 `/tmp/.sudo_pwd` | stdin 管道，不落盘 |
| 超时 | 无 | 全局 + 单步 |
| 退出码 | 日志文本 grep | plink 直接退出码 |
| 产线批量 | 单台覆盖写 | 循环 + CSV 汇总 + 设备 SN |
| 结果反馈 | 单行文本 | 满屏 banner + 蜂鸣 |
| 配置 | 改 bat | config.ini |

---

## 十、验证清单（建议三平台各跑一遍）

- [ ] Win7 SP1 双击 run.bat 全流程无交互、无乱码
- [ ] Win10 同上
- [ ] Win11 同上
- [ ] 不运行 setup_network 直接 run.bat → 友好报错（不报 SSH 错）
- [ ] 设备关机时 run.bat → ping 预检失败、5 秒内退出
- [ ] 执行中拔网线 → 超时杀 plink，标记 FAIL，进入下一台
- [ ] 连续 5 台 → batch_log.csv 5 行
- [ ] deb 损坏 → phase3 FAIL 正确终止
- [ ] `grep -r <root_pwd> /tmp` 无命中（密码不落盘）
