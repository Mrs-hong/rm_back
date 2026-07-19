# 运维操作手册

本文档汇总 qifeng-scm 在生产环境中常用的运维操作命令，包括安装验证、日志查看、卸载清理等场景。

## 1. 服务管理

### 1.1 systemd 单元操作

```bash
# 查看 scmd 守护进程状态
systemctl status qifeng-scmd.service

# 启动 / 停止 / 重启 scmd（推荐通过 scmc 客户端操作，详见 README）
sudo systemctl start qifeng-scmd.service
sudo systemctl stop qifeng-scmd.service
sudo systemctl restart qifeng-scmd.service

# 启用 / 禁用开机自启
sudo systemctl enable qifeng-scmd.service
sudo systemctl disable qifeng-scmd.service

# 查看具体服务（如 mariadb）状态
systemctl status scmd_mariadb.service
```

### 1.2 UDS socket 检查

```bash
# 确认 socket 已创建
ls -la /run/qifeng-scm/scmd.sock

# 确认 socket 权限（默认 0666 允许任意用户连接）
stat -c "%a %n" /run/qifeng-scm/scmd.sock
```

## 2. 日志查看

```bash
# 实时跟踪 scmd 守护进程日志
sudo journalctl -u qifeng-scmd.service -f

# 查看 scmd 最近 100 行日志
sudo journalctl -u qifeng-scmd.service -n 100

# 查看指定时间段的日志
sudo journalctl -u qifeng-scmd.service --since "2026-07-19 10:00" --until "2026-07-19 12:00"

# 查看业务服务日志（以 mariadb 为例）
sudo journalctl -u scmd_mariadb.service -f

# 查看 scmd 自身应用日志（位于数据目录的 log/ 子目录）
sudo tail -f /var/lib/qifeng-scm/log/scmd.log
```

## 3. 卸载与清理

### 3.1 常规卸载

```bash
# 普通卸载（保留数据目录和日志）
sudo dpkg -r qifeng-scm

# 完全卸载（清理所有数据和日志）
sudo dpkg --purge qifeng-scm

# 强制卸载（当包损坏或无法正常卸载时使用）
sudo dpkg --purge --force-all qifeng-scm
```

**说明：**
- 包名为 `qifeng_scm`（下划线），不是 `qifeng-scm`（连字符）
- `dpkg -r` 仅卸载程序文件，保留以下数据目录：
  - `/var/lib/qifeng-scm/` — 根目录（含 services/、data/、backup/、log/、tmp/ 等子目录）
- `dpkg --purge` 完全卸载，会删除 `/var/lib/qifeng-scm` 整个根目录

### 3.2 残留文件手动清理

如遇到卸载后文件残留（容器环境或权限问题），可执行以下手动清理步骤：

```bash
# 手动清理可执行文件
sudo rm -f /usr/bin/qf_scmd /usr/bin/qf_scmc

# 手动清理库文件
sudo rm -rf /usr/lib/qifeng-scm

# 手动清理配置文件
sudo rm -f /etc/qifeng-scm/symlinks.txt

# 手动清理动态链接器配置
sudo rm -f /etc/ld.so.conf.d/qifeng-scm.conf
sudo ldconfig

# 手动清理 systemd 服务文件
sudo rm -f /lib/systemd/system/qifeng-scmd.service
sudo systemctl daemon-reload
```

## 4. 数据目录结构

| 路径 | 说明 |
| --- | --- |
| `/var/lib/qifeng-scm/services/` | 服务安装目录（每个服务一个子目录） |
| `/var/lib/qifeng-scm/data/` | 数据目录（数据库和用户数据） |
| `/var/lib/qifeng-scm/backup/` | 备份目录（升级前自动备份） |
| `/var/lib/qifeng-scm/log/` | 日志目录（scmd 自身应用日志） |
| `/var/lib/qifeng-scm/tmp/` | 临时目录（解包、中转文件） |
| `/run/qifeng-scm/` | 运行时目录（UDS socket，由 systemd RuntimeDirectory 管理） |
| `/etc/qifeng-scm/scmd.yaml` | 系统配置文件 |

## 5. 故障排查

### 5.1 scmd 无法启动

```bash
# 1. 查看启动失败原因
sudo journalctl -u qifeng-scmd.service -b --no-pager | tail -50

# 2. 检查配置文件语法
sudo cat /etc/qifeng-scm/scmd.yaml

# 3. 检查 socket 目录权限
ls -ld /run/qifeng-scm/

# 4. 手动前台运行查看输出
sudo /usr/bin/qf_scmd --foreground
```

### 5.2 客户端无法连接

```bash
# 1. 确认 scmd 在运行
systemctl is-active qifeng-scmd.service

# 2. 确认 socket 存在且权限正确
ls -la /run/qifeng-scm/scmd.sock

# 3. 测试连接
qf_scmc version
```

### 5.3 服务操作超时

```bash
# 查看当前 opt_timeout_sec 配置
grep opt_timeout_sec /etc/qifeng-scm/scmd.yaml

# 查看服务自身的停止超时
systemctl show scmd_mariadb.service -p TimeoutStopUSec
```