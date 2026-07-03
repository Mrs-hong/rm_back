# qifeng-scm 服务管理系统

qifeng-scm（Qifeng Service Control Manager）是面向 Linux 平台的服务管理系统，通过 systemd 统一管理第三方服务的生命周期，支持服务安装、启动、停止、监控、卸载、更新及数据库自动初始化。

## 1. 项目简介

系统由两个主程序组成：

| 程序              | 运行身份 | 职责                                            |
| --------------- | ---- | --------------------------------------------- |
| `qf_scmd`   | root | 守护进程，通过 systemd 管理服务生命周期，监听 UDS socket 接收控制指令 |
| `qf_scmc` | 任意用户 | CLI 客户端，通过 UDS 与 `qf_scmd` 通信，执行服务管理命令    |

**核心能力：**

- 基于 systemd 的服务托管（自动启停、故障恢复、开机自启）
- 支持数据库服务（MariaDB、OpenGauss）的自动初始化和 SQL 脚本执行
- 服务依赖管理（自动按依赖顺序启动服务）
- 基于 UDS（Unix Domain Socket）的本地安全通信

## 2. 代码目录结构

```
qifeng-scm/
├── src/                          # 源码目录
│   ├── scmd/                     # 守护进程（qf_scmd）
│   │   ├── qifeng_scmd.cpp       # 主入口
│   │   ├── service_ctl.cpp       # 服务控制逻辑（安装、启动、停止等）
│   │   └── scmd_server.cpp       # UDS 服务端，接收并处理客户端命令
│   ├── scmctl/                   # CLI 客户端（qf_scmc）
│   │   ├── qifeng_scmctl.cpp     # 主入口
│   │   ├── cli_paser.cpp         # 命令行参数解析
│   │   ├── cli_commands.cpp      # 命令注册与分发
│   │   └── scmctl_client.cpp     # UDS 客户端，与 scmd 通信
│   ├── service_manger/           # 服务管理核心模块
│   │   ├── service_manager.cpp   # 服务安装、卸载、启停、状态查询
│   │   ├── service_generator.cpp # 生成 systemd 服务单元文件
│   │   ├── file_manager.cpp      # 服务目录、配置文件、符号链接管理
│   │   └── key_recoder.cpp       # 记录最后一次关键操作，用于异常恢复
│   ├── dbinit/                   # 数据库初始化后端（暂未使用）
│   │   ├── database_init.cpp     # 数据库初始化统一入口
│   │   ├── mysql_backend.cpp     # MariaDB/MySQL 初始化与 SQL 执行
│   │   └── opengauss_backend.cpp # OpenGauss 初始化与 SQL 执行
│   ├── service_tool/             # 服务工具模块
│   │   └── tool_mariadb.cpp      # MariaDB 交互工具（用户管理、SQL 执行、版本查询等）
│   ├── ipc/                      # 进程间通信
│   │   ├── uds.cpp               # Unix Domain Socket 通信实现
│   │   ├── protocil.cpp          # 通信协议序列化/反序列化
│   │   └── dbus_manager.cpp      # DBus 接口，调用 systemd 管理服务等
│   └── common/                   # 公共模块
│       ├── config.cpp            # 配置加载（scmd.yaml 解析）
│       ├── utils.cpp             # 工具函数
│       └── types.cpp             # 类型定义与转换
├── include/                      # 头文件目录（与 src 结构对应）
│   ├── scmd/
│   ├── scmctl/
│   ├── service_manger/
│   ├── dbinit/
│   ├── service_tool/
│   ├── ipc/
│   └── common/
├── .config/
│   └── scmd.yaml                 # 参考配置文件（安装时复制到 /etc/qifeng-scm/）
├── debian/                       # deb 打包相关文件
│   ├── qifeng-scmd.service       # systemd 服务单元
│   ├── ld.so.conf.d/
│   │   └── qifeng-scm.conf       # 私有库搜索路径配置
│   ├── postinst                  # 安装后脚本（创建目录、enable+start 服务）
│   ├── prerm                     # 卸载前脚本（停止服务）
│   └── postrm                    # 卸载后脚本（清理数据）
├── third_part/                   # 第三方依赖
│   └── qifeng_framework/         # 内部框架（提供日志、HTTP、yaml-cpp、CLI11 等）
├── CMakeLists.txt                # 顶层 CMake 配置
├── build.sh                      # 编译脚本
└── README.md                     # 本文档
```

## 3. 编译说明

### 3.1 环境要求

- 操作系统：Linux（Debian / Ubuntu 系列）
- 编译器：GCC >= 9 或 Clang >= 10
- CMake：>= 3.15
- 依赖库：
  - `libsystemd-dev`（systemd 开发库，提供 `sd-bus.h`）
  - `qifeng_framework（yaml.cpp、spdlog、cppjson）`

**安装系统依赖（Debian / Ubuntu）：**
```bash
sudo apt update
sudo apt install -y libsystemd-dev
```

**其他发行版：**
- CentOS / RHEL / Fedora：`sudo dnf install systemd-devel`
- openSUSE：`sudo zypper install systemd-devel`
- Arch Linux：`sudo pacman -S systemd`

### 3.2 使用 build.sh 编译（推荐）

```bash
# Release 编译主项目（默认）
./build.sh

# Debug 编译
./build.sh -d

# 编译所有目标（含测试）
./build.sh all -r

# 仅编译测试目标
./build.sh test -r

# 编译并安装到指定目录
./build.sh -r -i /opt/qifeng

# 清除 build 目录
./build.sh clean
```

### 3.3 直接使用 CMake 编译

```bash
# 配置（Release）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build -j$(nproc)

# 如需编译测试，添加 -DBUILD_TESTING=ON
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
```

### 3.4 编译产物

- `build/bin/qf_scmd` — 守护进程可执行文件
- `build/bin/qf_scmc` — CLI 客户端可执行文件

## 4. 配置参数说明
### 4.1 scm服务配置
#### 4.1.1 配置文件路径

- **系统配置文件**：`/etc/qifeng-scm/scmd.yaml`
- **参考配置文件**：`.config/scmd.yaml`（源码仓库中，安装时复制到系统路径）

#### 4.1.2 核心配置项

```yaml
scmd:
  log:
    level: info              # 日志级别：debug / info / warn / error
    max_file_size: 52428800  # 单个日志文件最大字节数（50MB）
    max_files: 7             # 日志文件滚动保留数量
    path: /var/log/qifeng-scm # 日志目录
  uds:
    socket_path: /run/qifeng-scm/scmd.sock  # UDS socket 路径
    socket_mode: "0666"                       # socket 权限（任意用户可连接）
  opt_timeout_sec: 10         # 操作超时时间（秒） (未完全实现、下一步实现)
  service_dir: /var/lib/qifeng-scm/services  # 服务安装目录
  back_dir: /var/lib/qifeng-scm/backup       # 备份目录
  data_dir: /var/lib/qifeng-scm/data         # 数据目录
```

#### 4.1.3 配置优先级

代码内置了与 `.config/scmd.yaml` 一致的系统路径默认值。启动时：

1. 优先读取 `/etc/qifeng-scm/scmd.yaml` 中的配置；
2. 若配置文件不存在或解析失败，使用代码中的默认值。

**生产环境建议**：保持 `/etc/qifeng-scm/scmd.yaml` 中的路径与默认值一致，确保 `FileManager` 与 `ConfigLoader` 路径完全同步。

### 4.2 安装的服务配置

每个服务通过一个 YAML 文件定义其元信息、执行方式、资源限制和依赖关系，安装时由 `scmd` 解析并管理。

#### 4.2.1 配置示例

```yaml
# Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.

serviceName: test_service_a   # 必要字段，且必须唯一
version: 1.0.0                # 必要字段，格式必须为 x.x.x
autoStart: false              # 是否随 scmd 自动启动

execution:                    # 必要字段，路径须相对软件包根目录，不能包含 ".."
  command: ./bin/test_service_a  # 必要字段
  workDir: ./bin
  dataDir: ./data
  exitSignal: 15
  timeoutStopSec: 10
  args: ["18002"]

initDB_sql_dir: ./sql         # 初始化数据库脚本目录
db_output_dir: ./db_output    # 存放创建用户和密码的文件目录

resources:
  ports: [8080, 8443]
  Mem: "500M"
  CPU: 200
  requires:
    - serviceName: test_service_c
      version: "1.0.0"
```

#### 4.2.2 字段说明

| 字段 | 是否必填 | 类型 | 默认值 | 说明 |
|------|---------|------|--------|------|
| `serviceName` | **必填** | string | — | 服务名称，必须唯一且非空 |
| `version` | **必填** | string | — | 版本号，格式必须为 `x.x.x` |
| `execution` | **必填** | object | — | 执行配置，路径必须为相对路径且不能包含 `..` |
| `execution.command` | **必填** | string | — | 启动命令，非空相对路径 |
| `execution.workDir` | 可选 | string | — | 工作目录，相对路径 |
| `execution.dataDir` | 可选 | string | — | 数据目录，相对路径 |
| `execution.args` | 可选 | string[] | — | 启动参数列表 |
| `execution.exitSignal` | 可选 | int | `15` (SIGTERM) | 优雅停止信号 |
| `execution.timeoutStopSec` | 可选 | uint32 | `5` | 停止超时时间（秒），超时后强制终止 |
| `autoStart` | 可选 | bool | `false` | 是否随 scmd 自动启动 |
| `initDB_sql_dir` | 可选 | string | — | 数据库初始化 SQL 脚本目录，相对路径 |
| `db_output_dir` | 可选 | string | — | 数据库用户/密码输出目录（仅在 `initDB_sql_dir` 存在时生效） |
| `resources` | 可选 | object | — | 资源限制与依赖配置 |
| `resources.ports` | 可选 | int[] | — | 需要开放的端口列表，范围 1–65535 |
| `resources.Mem` | 可选 | string | — | 内存限制，格式为 `"xM"`（如 `"500M"`），对应 systemd 的 `MemoryMax` |
| `resources.CPU` | 可选 | int | — | CPU 限制百分比，可大于 100 表示多核（如 `200` = 2 核），范围 0–100000 |
| `resources.requires` | 可选 | object[] | — | 服务依赖列表，不允许重复依赖，不能依赖自身 |
| `resources.requires[].serviceName` | 可选 | string | — | 依赖的服务名称 |
| `resources.requires[].version` | 可选 | string | `""` | 依赖的服务版本 |

#### 4.2.3 约束与校验规则

1. **路径安全**：`execution.command`、`execution.workDir`、`execution.dataDir`、`initDB_sql_dir` 必须为相对路径，不允许包含 `..` 以防止目录穿越。
2. **版本格式**：`version` 必须符合 `x.x.x` 语义化版本格式。
3. **依赖去重**：`resources.requires` 中同一 `serviceName` 不允许重复声明，且不能依赖自身。
4. **数据库自动识别**：系统根据 `serviceName` 自动判断是否为数据库服务；若非数据库服务，会进一步检查其依赖中是否包含数据库服务，并自动关联数据库配置。



## 5. 打包流程

### 5.1 编译并打包

```bash
# 1. 编译
./build.sh -r

# 2. 进入 build 目录并打包
cd build
cpack -G DEB
```

### 5.2 生成的包

- **包名**：`qifeng_scm-0.0.1-Linux.deb`
- **位置**：`build/qifeng_scm-0.0.1-Linux.deb`

### 5.3 包内主要内容

| 路径                                        | 说明           |
| ----------------------------------------- | ------------ |
| `/usr/bin/qf_scmd`                    | 守护进程可执行文件    |
| `/usr/bin/qf_scmc`                  | CLI 客户端可执行文件 |
| `/usr/lib/qifeng-scm/*.so*`               | 第三方依赖动态库     |
| `/lib/systemd/system/qifeng-scmd.service` | systemd 服务单元 |
| `/etc/qifeng-scm/scmd.yaml`               | 参考配置文件       |
| `/etc/ld.so.conf.d/qifeng-scm.conf`       | 私有库搜索路径配置    |
| `postinst` / `prerm` / `postrm`           | deb 维护脚本     |


## 6. 安装与使用示例

### 6.1 安装 deb 包

```bash
sudo dpkg -i qifeng_scm-0.0.1-Linux.deb

# 如提示依赖缺失，自动修复
sudo apt-get install -f
```

### 6.2 安装后自动行为

安装完成后，`postinst` 脚本会自动执行以下操作：

1. **创建系统目录**
   - `/var/lib/qifeng-scm/*` — 服务和数据目录
   - `/var/log/qifeng-scm` — 日志目录
   - `/run/qifeng-scm` — 运行时目录（由 systemd RuntimeDirectory 管理）

2. **配置动态链接库**
   - 安装第三方依赖库到 `/usr/lib/qifeng-scm/`
   - 创建正确的符号链接结构
   - 更新动态链接器缓存（`ldconfig`）

3. **设置 systemd 服务**
   - `systemctl enable qifeng-scmd.service` — **设置开机自启**
   - `systemctl start qifeng-scmd.service` — **立即启动服务**

> **注意**：安装完成后无需手动启动，`qf_scmd` 守护进程会自动运行，并设置为开机自启。

### 6.3 验证安装

```bash
# 查看守护进程状态
systemctl status qifeng-scmd.service

# 确认 socket 已创建
ls -la /run/qifeng-scm/scmd.sock

# 测试客户端（任意用户均可执行）
qf_scmc version
```

### 6.4 常用命令

```bash
# 列出已安装服务
qf_scmc list

# 安装服务（以 mariadb 为例）
qf_scmc install -n mariadb --tar_dir ./t_mariadb.tar.gz

# 启动服务
qf_scmc start -n mariadb

# 查看服务状态
qf_scmc info -n mariadb

# 停止服务
qf_scmc stop -n mariadb

# 卸载服务
qf_scmc uninstall -n mariadb
```

### 6.5 卸载

```bash
# 普通卸载（保留数据目录和日志）
sudo dpkg -r qifeng_scm

# 完全卸载（清理所有数据和日志）
sudo dpkg --purge qifeng_scm

# 强制卸载（当包损坏或无法正常卸载时使用）
sudo dpkg --purge --force-all qifeng_scm
```

**卸载后清理残留文件（如有需要）：**

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

**说明：**
- 包名为 `qifeng_scm`（下划线），不是 `qifeng-scm`（连字符）
- `dpkg -r` 仅卸载程序文件，保留以下数据目录：
  - `/var/lib/qifeng-scm/services/` — 服务配置
  - `/var/lib/qifeng-scm/data/` — 服务端数据
  - `/var/lib/qifeng-scm/backup/` — 备份数据
  - `/var/log/qifeng-scm/` — 日志
- `dpkg --purge` 完全卸载，会删除 `/var/lib/qifeng-scm` 和 `/var/log/qifeng-scm`
- 如遇到卸载后文件残留（容器环境或权限问题），可执行上述手动清理步骤

## 7. 开发与调试

### 7.1 开发模式运行

开发阶段可直接运行编译产物，无需安装：

```bash
# 以 root 运行守护进程（开发时可指定环境变量覆盖 socket 路径）
sudo ./build/bin/qf_scmd

# 在另一个终端执行客户端命令
./build/bin/qf_scmc list
```

### 7.2 查看日志

```bash
# 守护进程日志
sudo journalctl -u qifeng-scmd.service -f

# 服务日志（以 mariadb 为例）
sudo journalctl -u scmd_mariadb.service -f
```

## 8. 许可证

Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
