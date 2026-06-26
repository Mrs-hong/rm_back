# 数据库操作脚本实现计划

## 1. 摘要

基于 `.data/script/about.md` 和 `.data/script/database/design.md` 的设计意图，为 MariaDB 和 openGauss 实现一套完整的 Bash 操作脚本。脚本需满足：跨平台（Ubuntu/Debian、Windows WSL）、跨架构（x86_64、aarch64）、可被 C++ 程序调用、参数与返回值明确、注释详尽。

## 2. 当前状态分析

- 代码库为空，仅有设计文档。
- 设计文档提出了 7 类脚本：`install.sh`、`init.sh`、`start.sh`、`stop.sh`、`restart.sh`、`uninstall.sh`、`create_user_and_execute.sh`。
- 现有参数设计仅为示例，需根据实际数据库特性完善。
- 用户要求支持 Ubuntu/Debian 及 Windows（通过 WSL），架构涵盖 x86 与 ARM。

## 3. 设计决策与规范

### 3.1 目录结构

```
.data/script/database/
├── mariadb/
│   ├── install.sh
│   ├── init.sh
│   ├── start.sh
│   ├── stop.sh
│   ├── restart.sh
│   ├── uninstall.sh
│   └── create_user_and_execute.sh
└── opengauss/
    ├── install.sh
    ├── init.sh
    ├── start.sh
    ├── stop.sh
    ├── restart.sh
    ├── uninstall.sh
    └── create_user_and_execute.sh
```

### 3.2 统一规范（所有脚本遵循）

| 项目 | 规范 |
|------|------|
| 语言 | Bash，Shebang `#!/bin/bash` |
| 参数解析 | `getopt` 长参数风格，如 `--version=10.6.33` |
| 返回值 | 0=成功；1=参数错误；2=权限不足（非root）；3=依赖/环境不满足；4=安装失败；5=初始化失败；6=服务启动失败；7=服务停止失败；8=用户/SQL执行失败；9=卸载失败；10=不支持的系统或架构 |
| 输出格式 | `[INFO]`、`[WARN]`、`[ERROR]` 前缀，便于 C++ 程序通过管道捕获和解析 |
| 平台检测 | 函数 `detect_os()` 和 `detect_arch()`，区分 Ubuntu/Debian/WSL/其他，区分 x86_64/aarch64 |
| 注释 | 每个函数、每个主要步骤必须有中文注释说明目的和逻辑 |

### 3.3 C++ 调用约定

脚本设计为可被 C++ 通过 `popen`、`system` 或 `exec` 家族调用。C++ 程序通过：
- **返回值** 判断操作结果；
- **标准输出/标准错误** 获取详细日志；
- **明确参数** 传递配置，无需交互式输入。

## 4. 脚本详细设计

### 4.1 MariaDB 脚本（Ubuntu/Debian）

#### `mariadb/install.sh`

**完善后的参数：**

| 参数 | 必填 | 说明 |
|------|------|------|
| `--version=VERSION` | 是 | 目标版本，如 `10.6.33` |
| `--deb-path=PATH` | 否 | 本地 deb 包路径，无则尝试 apt 源 |
| `--data-dir=PATH` | 否 | 数据目录，默认 `/var/lib/mysql` |
| `--port=PORT` | 否 | 监听端口，默认 `3306` |
| `--bind-address=IP` | 否 | 绑定地址，默认 `127.0.0.1` |

**逻辑：**
1. 检查 root 权限。
2. 检测 OS（仅允许 Ubuntu/Debian/WSL）和架构（amd64/arm64）。
3. 检查系统中是否已存在 MariaDB/MySQL。
4. 若存在且版本匹配，跳过安装并返回 0；若版本不匹配，提示先卸载。
5. 无本地 deb 路径时，更新 apt 源并尝试 `apt-get install mariadb-server=VERSION`。
6. 有本地 deb 路径时，使用 `dpkg -i` 安装，并自动修复依赖 `apt-get install -f`。
7. 安装成功后设置数据目录权限。

#### `mariadb/init.sh`

**完善后的参数：**

| 参数 | 必填 | 说明 |
|------|------|------|
| `--admin-password=PWD` | 是 | root 管理员密码 |
| `--data-dir=PATH` | 否 | 数据目录，默认 `/var/lib/mysql` |
| `--port=PORT` | 否 | 端口，默认 `3306` |
| `--bind-address=IP` | 否 | 绑定地址，默认 `127.0.0.1` |
| `--character-set=CHARSET` | 否 | 字符集，默认 `utf8mb4` |

**逻辑：**
1. 检查 root 权限。
2