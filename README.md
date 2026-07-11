# qifeng-scm 服务管理系统

qifeng-scm 是面向边缘设备（Sophon BM1684X）的服务管理系统，提供服务的安装、卸载、升级、启停、自检等全生命周期管理能力。

## 系统组成

| 程序 | 说明 |
|------|------|
| `qf_scmd` | 守护进程，以 systemd 服务运行，监听 UDS 接收命令 |
| `qf_scmc` | CLI 客户端，通过 UDS 与 `qf_scmd` 通信 |

两者通过 Unix Domain Socket + JSON 行协议通信。

## 目录结构

```
rm_back/
├── CMakeLists.txt          # 顶层 CMake 配置
├── build.sh                # 编译打包脚本
├── .config/                # 参考配置文件（scmd.yaml、selftest.json）
├── debian/                 # Debian 打包脚本（postinst/prerm/postrm/preinst/conffiles）
├── doc/                    # 设计文档
│   ├── checker-design.md   # 自检模块设计
│   ├── checker-usage.md    # 自检模块使用
│   ├── upgrade.md          # 升级流程说明
│   └── command-flow-analysis.md  # 命令流程分析
├── include/                # 头文件
│   ├── common/             # 公共类型、配置、工具
│   ├── ipc/                # IPC 协议定义
│   ├── scmd/               # 服务端核心（dispatcher、handlers、service_ctl）
│   ├── service_manger/     # 领域管理器（service/nginx/model/upgrade/database）
│   ├── checker/            # 设备自检框架
│   └── scmctl/             # CLI 客户端
├── src/                    # 源文件（与 include 结构对应）
├── test/                   # 单元测试
├── third_part/             # 第三方库（qifeng_framework 等）
├── script/                 # 脚本与前端资源
└── model/                  # 自检模型文件
```

## 依赖

### 必需依赖
- **qifeng_framework**：自研基础框架（日志、网络、工具），位于 `third_part/qifeng_framework`
- **systemd**：服务管理与 journal 日志（libsystemd）
- **libmysqlclient**：MariaDB/MySQL 数据库操作
- **yaml-cpp**：YAML 配置解析
- **jsoncpp**：JSON 序列化
- **CLI11**：命令行解析（header-only，随 qifeng_framework 提供）

### 可选依赖（自检模块）
- **Sophon SDK (libsophon)**：TPU 模型推理（BM1684X）
- **libdrm**：显示器检测
- **ALSA (libasound)**：麦克风检测
- **libgpiod**：GPIO 指示灯检测

可选依赖通过 CMake 选项控制，WSL2 交叉编译时可关闭。

## 编译

```bash
# Release 编译（默认）
./build.sh

# Debug 编译
./build.sh -d

# 编译并安装到指定目录
./build.sh -r -i /opt/qifeng

# 编译测试目标
./build.sh test -d

# 打包部署产物到 dist/
./build.sh pack

# 清除 build 目录
./build.sh clean
```

### 可选库开关

```bash
# 全部关闭（WSL2 交叉编译场景）
./build.sh --with-all=OFF

# 单独控制
./build.sh --with-bm-sdk=OFF --with-alsa=OFF --with-drm=OFF --with-gpiod=OFF
```

### 交叉编译注意事项
- ARM 目标（BM1684X）需使用对应架构的交叉编译工具链
- x86 开发环境编译时 libsophon 相关库不会打包安装，运行时需目标系统已安装

## 安装

```bash
# 安装 deb 包
sudo dpkg -i qifeng-scm_<version>_<arch>.deb

# 安装后自动完成：
# 1. 创建 /var/lib/qifeng-scm/{services,data,backup} 目录
# 2. 复制配置到 /etc/qifeng-scm/
# 3. 注册 systemd 服务 qifeng-scmd.service
# 4. 设置开机自启并启动
```

### 关键路径
| 路径 | 说明 |
|------|------|
| `/usr/bin/qf_scmd` | 守护进程 |
| `/usr/bin/qf_scmc` | CLI 客户端 |
| `/etc/qifeng-scm/scmd.yaml` | 主配置文件 |
| `/etc/qifeng-scm/selftest.json` | 自检配置 |
| `/var/lib/qifeng-scm/services/` | 已装服务目录 |
| `/var/lib/qifeng-scm/data/` | 数据目录 |
| `/var/lib/qifeng-scm/backup/` | 备份目录 |
| `/var/lib/qifeng-scm/log/scmd.log` | 运行日志 |
| `/run/qifeng-scm/scmd.sock` | UDS 套接字 |

## 命令使用

所有命令通过 `qf_scmc` 执行，需 root 权限。

### 服务生命周期

```bash
# 安装服务（从 tar 包或目录）
qf_scmc install -n <服务名> -d <tar包路径>

# 启动服务（不指定 -n 时操作 scmd 自身）
qf_scmc start -n <服务名>

# 停止服务
qf_scmc stop -n <服务名>        # 停止单个服务
qf_scmc stop -a                 # 停止所有已装服务

# 重启服务
qf_scmc restart -n <服务名>     # 重启单个服务
qf_scmc restart -a              # 重启所有服务

# 卸载服务
qf_scmc uninstall -n <服务名>   # 卸载单个服务
qf_scmc uninstall -a            # 卸载所有已装服务（保留 scmd 自身）

# 重载服务配置
qf_scmc reload -n <服务名>      # 重载单个服务
qf_scmc reload -a               # 重载所有服务
```

### 升级

```bash
# 从 tar 包升级服务
qf_scmc upgrade -n <服务名> -d <新版本tar包>

# 一体化升级（服务+模型+Nginx，从服务内部 soft_dir 查找素材）
qf_scmc upgrades -n <服务名>

# 一体化升级（指定外部素材目录）
qf_scmc upgrades -n <服务名> -d <素材目录>
```

### 查询

```bash
# 查看所有已装服务
qf_scmc list

# 查看服务详情（含 CPU、内存、运行时间）
qf_scmc info -n <服务名>

# 查看服务详情（含错误诊断）
qf_scmc info -n <服务名> --error

# 查看操作日志（最近 N 条）
qf_scmc log -n 50

# 查看服务 journal 日志
qf_scmc slog -n <服务名> --count 20
```

### Nginx 与模型管理

```bash
# 独立配置 nginx
qf_scmc init_nginx -d <配置目录>

# 重置 nginx 配置
qf_scmc reset_nginx -wait   # 等待状态（所有路由 404）
qf_scmc reset_nginx -now    # 恢复正常
qf_scmc reset_nginx -back   # 全部无效（仅欢迎页）

# 模型管理
qf_scmc add_model <模型路径>       # 安装/升级模型
qf_scmc clear_model -n <模型名>    # 停用并备份模型
```

### 其他

```bash
# 设备自检
qf_scmc check

# 查看版本
qf_scmc --version

# 使 scmd 优雅退出
qf_scmc kill
```

## 卸载

```bash
# 卸载所有已装服务（保留 scmd 自身）
qf_scmc uninstall -a

# 卸载 deb 包（保留配置和数据）
sudo dpkg -r qifeng-scm

# 彻底卸载（清除配置和数据）
sudo dpkg --purge qifeng-scm
# purge 会删除 /var/lib/qifeng-scm 和 /var/log/qifeng-scm
```

## 升级

### Deb 包升级

```bash
sudo dpkg -i qifeng-scm_<new_version>_<arch>.deb
```

升级流程：
1. **preinst**：备份 `/etc/qifeng-scm/scmd.yaml` 和 `selftest.json` 到 `.bak`
2. **prerm**：`qf_scmc stop -a` 停止所有服务，停止 scmd
3. 解包新版本文件
4. **postinst**：恢复 `.bak` 配置，保留 `/var/lib/qifeng-scm/services` 数据，重启 scmd

`scmd.yaml` 和 `selftest.json` 声明为 conffiles，dpkg 升级时提供配置保留策略。

### 服务升级

服务升级支持两种模式：

- **全量升级**：无 `up_detail.yaml` 时，备份旧版本后全量覆盖
- **细粒度升级**：有 `up_detail.yaml` 时，按 replace/add/clear 列表精确替换

升级前自动备份数据库（若服务依赖 mariadb），升级失败自动回滚文件和数据库。

## 注意事项

1. **权限要求**：所有 `qf_scmc` 命令需以 root 执行（UDS 套接字权限 0666，但服务管理操作需 root）
2. **数据库服务依赖**：若服务依赖 MariaDB，需先安装并运行 MariaDB
3. **模型文件管理**：模型文件统一存放在 `/var/lib/qifeng-scm/models`，通过 `QIFENG_MODEL_DIR` 环境变量注入给服务
4. **开机自检**：`scmd.yaml` 中 `selftest.enabled=true` 时，scmd 启动前执行设备自检；`fail_action=halt` 时自检失败会中止启动
5. **日志轮转**：默认 50MB/文件、7 个文件，可通过 `scmd.yaml` 的 `log` 段配置
6. **CPU 使用率**：`info` 命令显示的 CPU 使用率已按核心数归一化（0%~100%），1.0 表示占满所有核心

## 更多文档

- [自检模块设计](doc/checker-design.md)
- [自检模块使用](doc/checker-usage.md)
- [升级流程说明](doc/upgrade.md)
- [命令流程分析](doc/command-flow-analysis.md)
