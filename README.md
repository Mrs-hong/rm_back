# qifeng-scm 服务管理系统

qifeng-scm（Qifeng Service Control Manager）是面向 Linux 平台的服务管理系统，通过 systemd 统一管理第三方服务的生命周期，支持服务安装、启动、停止、监控、卸载、升级及数据库自动初始化。

## 1. 项目简介

系统由两个主程序组成：

| 程序 | 运行身份 | 职责 |
| --- | --- | --- |
| `qf_scmd` | root | 守护进程，通过 systemd 管理服务生命周期，监听 UDS socket 接收控制指令 |
| `qf_scmc` | 任意用户 | CLI 客户端，通过 UDS 与 `qf_scmd` 通信，执行服务管理命令 |

**核心能力：**

- 基于 systemd 的服务托管（自动启停、故障恢复、开机自启）
- 支持数据库服务（MariaDB、OpenGauss）的自动初始化和 SQL 脚本执行
- 服务依赖管理（自动按依赖顺序启动服务）
- 一体化升级（服务 + 模型 + Nginx 配置）
- 硬件自检（disk/fan/memory/tpu/network 等 13 项检测）
- 基于 UDS（Unix Domain Socket）的本地安全通信

## 2. 代码目录结构

```
qifeng-scm/
├── src/                          # 源码目录
│   ├── scmd/                     # 守护进程（qf_scmd）
│   │   ├── handlers/             # 各命令处理器（一命令一文件）
│   │   ├── qifeng_scmd.cpp       # 主入口
│   │   ├── scmd_server.cpp       # UDS 服务端
│   │   ├── service_ctl.cpp       # 服务控制生命周期
│   │   └── service_operations.cpp # 跨领域服务编排
│   ├── scmctl/                   # CLI 客户端（qf_scmc）
│   │   ├── qifeng_scmctl.cpp     # 主入口
│   │   ├── cli_parser.cpp        # 命令行解析
│   │   ├── cli_commands.cpp      # 命令实现与自注册
│   │   └── scmctl_client.cpp     # UDS 客户端
│   ├── service_manager/          # 服务管理核心模块
│   │   ├── service_manager.cpp   # 服务安装、卸载、启停
│   │   ├── service_generator.cpp # 生成 systemd 服务单元文件
│   │   ├── file_manager.cpp      # 服务目录、配置文件管理
│   │   ├── dbus_manager.cpp      # systemd DBus 接口
│   │   ├── database_service.cpp  # 数据库服务管理
│   │   ├── nginx_manager.cpp     # Nginx 配置管理
│   │   ├── model_manager.cpp     # 模型文件管理
│   │   ├── upgrade_service.cpp   # 单服务升级
│   │   ├── integrated_upgrade.cpp# 一体化升级编排
│   │   ├── service_utils.cpp     # 共享底层工具
│   │   └── key_recoder.cpp       # 关键操作记录（用于断点恢复）
│   ├── checker/                  # 硬件自检模块
│   │   ├── checkers/             # 各检测器（一检测项一文件）
│   │   ├── core/                 # 检测器注册表与运行器
│   │   └── hw/                   # 硬件访问封装（GPIO/串口/ALSA/DRM）
│   ├── ipc/                      # 进程间通信
│   │   ├── uds.cpp               # Unix Domain Socket 实现
│   │   ├── protocol.cpp          # 通信协议序列化/反序列化
│   │   └── dbus_manager.cpp      # DBus 接口
│   ├── common/                   # 公共模块
│   │   ├── utils/                # 工具函数（按职责拆分：path/file/tar/...）
│   │   ├── config.cpp            # 系统配置加载
│   │   ├── service_config_loader.cpp # 服务配置加载
│   │   └── types.cpp             # 类型定义与转换
│   └── service_tool/             # 服务工具（MariaDB/Nginx 交互）
├── include/                      # 头文件目录（与 src 结构对应）
├── docs/                         # 文档
│   ├── operations.md             # 运维操作手册
│   ├── command_development.md    # 新增命令开发指南
│   └── roadmap.md                # 路线图与未完成特性
├── .config/                      # 参考配置
│   └── scmd.yaml                 # scmd 配置文件（安装时复制到 /etc/qifeng-scm/）
├── debian/                       # deb 打包相关文件
├── third_part/                   # 第三方依赖（qifeng_framework、CLI11）
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
  - `qifeng_framework`（提供日志、HTTP、yaml-cpp、jsoncpp 等）
  - 可选：`libgpiod-dev`（GPIO 访问）、`libasound2-dev`（音频采集）、Sophon BM SDK（TPU/模型推理）

**安装系统依赖（Debian / Ubuntu）：**

```bash
sudo apt update
sudo apt install -y libsystemd-dev
```

### 3.2 使用 build.sh 编译（推荐）

```bash
# Release 编译主项目（默认）
./build.sh

# Debug 编译
./build.sh -d

# 编译所有目标（含测试）
./build.sh all -r

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

### 4.1 scm 服务配置

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
  uds:
    socket_path: /run/qifeng-scm/scmd.sock  # UDS socket 路径
    socket_mode: "0666"                       # socket 权限（任意用户可连接）
  opt_timeout_sec: 10         # 操作超时时间（秒），详见 docs/roadmap.md
  root_dir: /var/lib/qifeng-scm  # 根目录，子目录自动派生
```

**子目录自动派生规则：** 由 `root_dir` 自动派生，无需单独配置：

| 子目录 | 派生规则 | 说明 |
| --- | --- | --- |
| `services` | `root_dir + "/services"` | 服务安装目录 |
| `data` | `root_dir + "/data"` | 数据目录 |
| `backup` | `root_dir + "/backup"` | 备份目录 |
| `log` | `root_dir + "/log"` | 日志目录 |
| `tmp` | `root_dir + "/tmp"` | 临时目录 |

> 如需单独覆盖某个子目录路径，可在配置文件中添加对应的 `service_dir`、`back_dir`、`data_dir`、`temp_dir` 字段，优先级高于 `root_dir` 派生值。日志路径也可通过 `log.path` 单独指定。

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
| --- | --- | --- | --- | --- |
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

- **包名**：`qifeng_scm-0.0.1-Linux.deb`（注意：包名用下划线）
- **位置**：`build/qifeng_scm-0.0.1-Linux.deb`

### 5.3 包内主要内容

| 路径 | 说明 |
| --- | --- |
| `/usr/bin/qf_scmd` | 守护进程可执行文件 |
| `/usr/bin/qf_scmc` | CLI 客户端可执行文件 |
| `/usr/lib/qifeng-scm/*.so*` | 第三方依赖动态库 |
| `/lib/systemd/system/qifeng-scmd.service` | systemd 服务单元 |
| `/etc/qifeng-scm/scmd.yaml` | 参考配置文件 |
| `/etc/ld.so.conf.d/qifeng-scm.conf` | 私有库搜索路径配置 |
| `postinst` / `prerm` / `postrm` | deb 维护脚本 |

## 6. 安装与使用示例

### 6.1 安装 deb 包

```bash
sudo dpkg -i qifeng_scm-0.0.1-Linux.deb

# 如提示依赖缺失，自动修复
sudo apt-get install -f
```

### 6.2 安装后自动行为

安装完成后，`postinst` 脚本会自动执行以下操作：

1. **创建系统目录**：`/var/lib/qifeng-scm/`（含 services/data/backup/log/tmp 子目录）与 `/run/qifeng-scm`（由 systemd RuntimeDirectory 管理）
2. **配置动态链接库**：安装第三方依赖库到 `/usr/lib/qifeng-scm/`，更新 `ldconfig`
3. **设置 systemd 服务**：`systemctl enable qifeng-scmd.service` + `systemctl start qifeng-scmd.service`

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

# 一体化升级（服务 + 模型 + Nginx）
qf_scmc upgrades -n mariadb -d ./upgrade_pkg
```

### 6.5 卸载

```bash
# 普通卸载（保留数据目录和日志）
sudo dpkg -r qifeng-scm

# 完全卸载（清理所有数据和日志）
sudo dpkg --purge qifeng-scm
```

> 完整的卸载清理步骤与残留文件处理，请参阅 [docs/operations.md](docs/operations.md)。

## 7. scmd 自身操作语义

`scmc start` / `scmc stop` / `scmc restart` 在**不指定 `-n` 参数**时，操作目标为 scmd 自身（`qifeng-scmd.service`），与操作普通服务的语义不同。

### 7.1 命令行为对比

| 命令 | 携带 `-n <svc>` | 不携带 `-n` |
| --- | --- | --- |
| `scmc start` | 启动指定服务 | 操作 scmd 自身：直接返回成功（scmd 正在处理请求即说明自身已运行） |
| `scmc stop` | 停止指定服务 | 操作 scmd 自身：通过 systemd DBus `StopUnit("qifeng-scmd.service")` 停止守护进程 |
| `scmc restart` | 重启指定服务 | 操作 scmd 自身：通过 systemd DBus `RestartUnit("qifeng-scmd.service")` 重启守护进程 |
| `scmc kill` | （不接受 `-n`） | 触发 scmd 优雅退出（置 `mRunning=false`，不经 systemd） |

### 7.2 `scmc stop` vs `scmc kill` 的区别

两者都用于停止 scmd 自身，但实现路径不同：

- **`scmc stop`（无参数）**：经 systemd DBus 调用 `StopUnit`，由 systemd 主动停止单元。若 `qifeng-scmd.service` 配置了 `Restart=always/on-failure`，systemd 会立即拉起新实例。适合"希望通过 systemd 正常停止单元"的场景。

- **`scmc kill`**：直接置 `ScmServer` 内部 `mRunning=false`，scmd 主循环优雅退出，**不经过 systemd**。进程退出后由 systemd 根据 Restart 策略决定是否拉起。适合"希望 scmd 立即停止接收新请求并完成手头任务"的场景。

### 7.3 权限要求

- **`scmc start`（无参数）**：无需特殊权限，scmd 自身已在运行即返回成功。
- **`scmc stop` / `scmc restart`（无参数）**：scmd 进程需具备 systemd DBus `StopUnit`/`RestartUnit` 权限。由于 scmd 以 root 运行，通常已具备；若通过 polkit 限制，需配置相应策略。
- **客户端 `scmc`**：通过 UDS 与 scmd 通信，本身不需要 sudo（socket 默认权限 0666）。

### 7.4 彻底停止 scmd

若希望停止 scmd 后不被 systemd 自动拉起：

```bash
# 1. 先禁用开机自启与运行时重启
sudo systemctl disable qifeng-scmd.service

# 2. 再停止 scmd
sudo systemctl stop qifeng-scmd.service
# 或
qf_scmc kill
```

## 8. 开发与调试

### 8.1 开发模式运行

开发阶段可直接运行编译产物，无需安装：

```bash
# 以 root 运行守护进程
sudo ./build/bin/qf_scmd

# 在另一个终端执行客户端命令
./build/bin/qf_scmc list
```

### 8.2 查看日志

```bash
# 守护进程日志
sudo journalctl -u qifeng-scmd.service -f

# 服务日志（以 mariadb 为例）
sudo journalctl -u scmd_mariadb.service -f
```

### 8.3 新增命令开发

新增一个完整的客户端命令 + 服务端处理器，请参阅 [docs/command_development.md](docs/command_development.md)。

系统采用"自注册"机制：

- **客户端**：通过 `REGISTER_CLI_COMMAND(XxxCommand)` 宏自动注册，无需修改 `cli_parser.cpp`；
- **服务端**：通过 `REGISTER_COMMAND_HANDLER(ScmCommand::XXX, XxxHandler)` 宏自动注册，无需修改中央分发逻辑；
- **协议层**：通过 `SCM_DEFINE_REQUEST` 宏声明字段元信息，自动生成 JSON 序列化/反序列化代码，无需手写。

### 8.4 clang-tidy 代码规范

项目通过 [.clang-tidy](.clang-tidy) 配置强制执行代码规范（`WarningsAsErrors: '*'`）。提交前请运行：

```bash
# 需先生成 compile_commands.json
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 扫描单个文件
clang-tidy -p build -header-filter='^/home/hong/code/rm_back/(src|include)/.*' src/path/to/file.cpp
```

关键规则：

- 命名：类名/函数名 CamelCase、局部变量 camelBack、私有成员 `m` 前缀、全局常量 CamelCase 无前缀
- 函数规模：参数 ≤4、行数 ≤50、嵌套 ≤4、认知复杂度 ≤15
- 单行 `if/for/while` 语句必须用 `{}` 包裹
- Rule of Five：定义析构函数的类需显式定义或删除拷贝/移动操作

无法重构的超限函数可用 `NOLINTNEXTLINE` 显式标注。

## 9. 相关文档

- [docs/operations.md](docs/operations.md) — 运维操作手册（systemd 管理、日志查看、卸载清理、故障排查）
- [docs/command_development.md](docs/command_development.md) — 新增命令开发指南（客户端 + 服务端 + 协议层）
- [docs/roadmap.md](docs/roadmap.md) — 路线图与未完成特性

## 10. 许可证

Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
