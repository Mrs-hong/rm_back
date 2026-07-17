# qifeng-scm 部署脚本说明文档

## 目录

- [一、快速开始（直接使用方式）](#一快速开始直接使用方式)
- [二、目录结构概览](#二目录结构概览)
- [三、各模块详解](#三各模块详解)
  - [3.1 总入口：setup.sh](#31-总入口setupsh)
  - [3.2 系统库安装：sys_lib/install.sh](#32-系统库安装sys_libinstallsh)
  - [3.3 数据库部署：database/](#33-数据库部署database)
  - [3.4 Nginx 部署：nginx/](#34-nginx-部署nginx)
  - [3.5 SCM 及服务安装：scm_and_soft/](#35-scm-及服务安装scm_and_soft)
- [四、模块调整方式](#四模块调整方式)
  - [4.1 调整系统依赖库](#41-调整系统依赖库)
  - [4.2 切换数据库类型（MariaDB ↔ openGauss）](#42-切换数据库类型mariadb--opengauss)
  - [4.3 调整数据库配置](#43-调整数据库配置)
  - [4.4 调整 Nginx 配置](#44-调整-nginx-配置)
  - [4.5 调整前端资源](#45-调整前端资源)
  - [4.6 调整 SCM 版本和服务列表](#46-调整-scm-版本和服务列表)
- [五、常见问题与检查清单](#五常见问题与检查清单)

---

## 一、快速开始（直接使用方式）

### 1.0 安装包下载 

- [qifeng-scm 安装包](https://github.com/yourusername/qifeng-scm/releases)

### 1.1 一键部署（推荐）

将整个 `script` 目录拷贝到目标服务器，以 root 权限执行总入口脚本：

```bash
sudo bash script/setup.sh
```

该脚本会**按顺序自动执行**以下 6 个步骤，任一步骤失败则终止：

| 步骤 | 说明 |
|------|------|
| 1/7 | 安装系统依赖库（openssl、zlib、systemd 等） |
| 2/7 | 卸载旧版MariadDB |
| 3/7 | 安装并初始化数据库（默认 MariaDB） |
| 4/7 | 卸载旧版 Nginx |
| 5/7 | 安装 Nginx |
| 6/7 | 部署 Nginx 配置并启动服务 |
| 7/7 | 安装 SCM deb 包及配套服务 |

### 1.2 分步部署

如果需要单独执行某个模块，可以进入对应子目录手动运行：

```bash
# 仅安装系统库
sudo bash script/sys_lib/install.sh

# 仅部署数据库（默认 MariaDB）
sudo bash script/database/install.sh

# 仅安装 Nginx
sudo bash script/nginx/install_nginx.sh

# 仅部署 Nginx 配置
sudo bash script/nginx/deploy_config.sh

# 仅安装 SCM 及服务
sudo bash script/scm_and_soft/install.sh
```

### 1.3 环境要求

- **操作系统**：Ubuntu 18.04+ / Debian 10+ 
- **架构**：x86_64 / aarch64
- **权限**：所有脚本均需 **root 权限** 运行（使用 `sudo`）
- **网络**：需要能访问系统软件源（apt/yum/dnf）

---

## 二、目录结构概览

```
script/
├── setup.sh                       # 一键部署总入口
├── sys_lib/
│   └── install.sh                 # 系统依赖库安装脚本
├── database/
│   ├── install.sh                 # 数据库一键部署（默认 MariaDB）
│   ├── common.sh                  # 公共函数库（日志、参数解析、工具函数）
│   ├── mariadb/
│   │   ├── config.txt             # MariaDB 配置文件
│   │   ├── install.sh             # MariaDB 安装脚本
│   │   ├── init.sh                # MariaDB 初始化脚本
│   │   └── uninstall.sh           # MariaDB 卸载脚本
│   └── opengauss/
│       ├── install.sh             # openGauss 安装脚本
│       ├── init.sh                # openGauss 初始化脚本
│       └── uninstall.sh           # openGauss 卸载脚本
├── nginx/
│   ├── install_nginx.sh           # Nginx 安装脚本
│   ├── uninstall_nginx.sh         # Nginx 卸载脚本
│   ├── deploy_config.sh           # Nginx 配置部署脚本
│   ├── nginx/
│   │   └── conf.d/
│   │       ├── default.conf       # Nginx 站点配置模板（含前端静态文件 + API 代理）
│   │       └── http_ca.conf       # 后端 API 代理配置（/web/、/sys/、/uploaded_files/）
│   └── frontend/                  # 前端静态资源（Vue SPA 构建产物）
│       ├── index.html
│       └── assets/
└── scm_and_soft/
    ├── install.sh                 # SCM 及配套服务安装脚本
    ├── install_order.txt          # 服务安装顺序配置
    ├── qifeng_scm-0.0.1-Linux.deb # SCM 核心 deb 安装包
    └── get_table_row.tar.gz       # 示例服务包
```

---

## 三、各模块详解

### 3.1 总入口：setup.sh

**作用**：一键部署的总调度脚本，按固定顺序调用各子模块。

**执行流程**：
1. 安装系统依赖库 → `sys_lib/install.sh`
2. 安装并初始化数据库 → `database/install.sh`
3. 卸载旧 Nginx → `nginx/uninstall_nginx.sh`（自动确认）
4. 安装 Nginx → `nginx/install_nginx.sh`
5. 部署 Nginx 配置 → `nginx/deploy_config.sh`
6. 安装 SCM 及服务 → `scm_and_soft/install.sh`

**特性**：
- 使用 `set -euo pipefail` 确保任何步骤失败立即终止
- 设置 `DEBIAN_FRONTEND=noninteractive` 避免交互式提示
- 带时间戳的结构化日志输出

---

### 3.2 系统库安装：sys_lib/install.sh

**作用**：安装 qifeng-scm 编译和运行所需的系统级依赖库。

**自动检测操作系统**，支持两类发行版：

| 发行版系列 | 包管理器 | 安装的包 |
|-----------|---------|---------|
| Debian/Ubuntu | apt-get | libssl-dev, zlib1g-dev, libsystemd-dev, libcurl4-openssl-dev, libjson-c-dev, libsqlite3-dev, libgtest-dev, cmake, build-essential, pkg-config |
| CentOS/RHEL | yum/dnf | openssl-devel, zlib-devel, systemd-devel, libcurl-devel, json-c-devel, sqlite-devel, gtest-devel, cmake, gcc-c++, make, pkgconfig |

**特性**：
- 幂等设计：已安装的包自动跳过
- RHEL 系列自动安装 EPEL 仓库（gtest-devel 等包需要）
- 逐个安装并报告失败详情

---

### 3.3 数据库部署：database/

#### 3.3.1 公共函数库：database/common.sh

**作用**：为 MariaDB 和 openGauss 脚本提供统一的公共能力，包括：

| 功能 | 说明 |
|------|------|
| 日志输出 | `db_log_info` / `db_log_error` / `db_log_warn`（带时间戳） |
| 参数解析 | `db_parse_args` / `db_arg`（支持 `--key=value` 和 `--key value`） |
| 权限检查 | `db_check_root` |
| 系统检测 | `db_check_arch`（架构检测）、`db_check_os`（系统检测） |
| 端口检测 | `db_check_port_in_use` |
| 安全操作 | `db_validate_path`（路径安全校验）、`db_safe_rm`（安全删除） |
| 数据库工具 | `db_wait_for_mysql`（等待服务就绪）、`db_create_mysql_cnf`（安全传递密码）、`db_sql_escape`（SQL 转义） |
| 用户切换 | `db_run_as_user`（以指定用户执行命令） |

**返回值定义**（供 C++ 程序解析）：

| 退出码 | 含义 |
|--------|------|
| 0 | 成功 |
| 1 | 通用错误 |
| 2 | 参数错误或缺失 |
| 3 | 权限不足（非 root） |
| 4 | 依赖缺失/安装包不存在 |
| 5 | 服务已运行/未运行（状态冲突） |
| 6 | 端口被占用 |
| 7 | 初始化失败 |
| 8 | 数据库连接失败 |

#### 3.3.2 数据库总入口：database/install.sh

**作用**：读取 `mariadb/config.txt` 配置，依次执行卸载 → 安装 → 初始化，完成 MariaDB 的完整部署。

**执行流程**：
1. 读取 `mariadb/config.txt` 获取配置（admin_password 为必填项）
2. 执行 `mariadb/uninstall.sh --purge`（彻底清理旧环境）
3. 执行 `mariadb/install.sh`（安装 MariaDB）
4. 执行 `mariadb/init.sh`（初始化数据库实例）

#### 3.3.3 MariaDB 子模块

| 脚本 | 作用 | 关键参数 |
|------|------|---------|
| `mariadb/install.sh` | 安装 MariaDB 服务端和客户端 | `--version`（指定版本）、`--deb_path`（本地 deb 包路径）、`--arch`（强制架构） |
| `mariadb/init.sh` | 初始化数据库实例 | `--admin_password`（必填）、`--data`（数据目录）、`--port`（端口）、`--ip`（绑定 IP）、`--character_set`（字符集）、`--force`（强制重新初始化） |
| `mariadb/uninstall.sh` | 卸载 MariaDB | `--purge`（同时删除数据和配置） |
| `mariadb/config.txt` | 配置文件 | 见下方配置说明 |

#### 3.3.4 openGauss 子模块

| 脚本 | 作用 | 关键参数 |
|------|------|---------|
| `opengauss/install.sh` | 安装 openGauss 数据库 | `--tar_path`（本地 tar.gz 包）、`--install_path`（安装目录）、`--user`（运行用户） |
| `opengauss/init.sh` | 初始化数据库实例 | `--admin_password`（必填）、`--data`（数据目录）、`--port`（端口）、`--ip`（绑定 IP） |
| `opengauss/uninstall.sh` | 卸载 openGauss | `--purge`（同时删除数据和用户） |

---

### 3.4 Nginx 部署：nginx/

#### 3.4.1 install_nginx.sh

**作用**：检测系统类型并安装 Nginx。

**支持系统**：Ubuntu/Debian（apt）、CentOS/RHEL/Rocky/AlmaLinux/Fedora（yum/dnf）

**特性**：幂等设计，已安装则跳过。

#### 3.4.2 uninstall_nginx.sh

**作用**：完全卸载 Nginx，清理配置与残留。

**特性**：
- 交互式确认（防止误操作），`setup.sh` 中通过管道自动传入 `y`
- 备份原始配置到 `/root/nginx-conf-backup.*`
- 清理日志（`/var/log/nginx`）和缓存（`/var/cache/nginx`）

#### 3.4.3 deploy_config.sh

**作用**：部署 Nginx 配置文件并启动服务。

**执行流程**：
1. 检查 Nginx 是否已安装
2. 检查前端资源是否存在
3. 备份原有 `default.conf`
4. 将配置模板中的 `__TEST_NGINX_ROOT__` 替换为实际路径，部署到 `/etc/nginx/conf.d/test_nginx.conf`
5. 部署后端 API 代理配置到 `/etc/nginx/snippets/http_ca.conf`
6. 修复目录权限（确保 Nginx worker 可访问前端文件）
7. 移除默认站点避免端口冲突
8. 测试配置并启动/重启 Nginx

#### 3.4.4 Nginx 配置说明

**default.conf**（站点配置模板）：
- 监听 80 端口
- 提供前端 SPA 静态文件服务（支持 Vue Router history 模式）
- 包含 Windows 网络连接测试端点（`/connecttest.txt`、`/ncsi.txt`）
- 包含麒麟/UOS/Android 网络认证端点（`/generate_204`）
- 支持大文件上传（`client_max_body_size 10G`）
- 关闭代理缓冲（适配流式上传）

**http_ca.conf**（后端 API 代理）：
- `/web/*` → 代理到 `127.0.0.1:8080`（用户业务接口，需 JWT 认证）
- `/sys/*` → 代理到 `127.0.0.1:8080`（系统管理接口）
- `/uploaded_files/*` → 代理到 `127.0.0.1:8080`（文件上传/下载）

#### 3.4.5 frontend/（前端静态资源）

Vue SPA 构建产物，包含 `index.html` 和 `assets/` 目录。页面标题为"智会宝"。

---

### 3.5 SCM 及服务安装：scm_and_soft/

#### 3.5.1 install.sh

**作用**：安装 SCM 核心 deb 包，并按配置安装配套服务。

**执行流程**：
1. 查找并安装 `qifeng_scm-*.deb`（先卸载旧版保留数据）
2. 修复依赖（`apt-get install -f`）
3. 验证 `qf_scmc` 命令可用
4. 按 `install_order.txt` 逐项安装服务
5. 根据配置决定是否自动启动服务
6. 列出所有服务状态

#### 3.5.2 install_order.txt

**格式**：每行一个服务，空格分隔三列

```
# 软件服务名      软件包名                 是否启动
get_table_row     get_table_row.tar.gz     true
```

- 第 1 列：服务名称（传给 `qf_scmc install -n`）
- 第 2 列：软件包文件名（相对于 `scm_and_soft/` 目录）
- 第 3 列：是否自动启动（`true` / `false`）
- 以 `#` 开头的行为注释，空行忽略

---

## 四、模块调整方式

### 4.1 调整系统依赖库

编辑 [sys_lib/install.sh](sys_lib/install.sh)，修改对应发行版的包列表：

**Debian/Ubuntu 系列**（第 70-81 行）：
```bash
local packages=(
    libssl-dev          # OpenSSL 开发库
    zlib1g-dev          # zlib 压缩库
    libsystemd-dev      # systemd 开发库
    libcurl4-openssl-dev # libcurl（OpenSSL 后端）
    libjson-c-dev       # JSON-C 库
    libsqlite3-dev      # SQLite3 开发库
    libgtest-dev        # Google Test 框架
    cmake               # CMake 构建工具
    build-essential     # 编译工具链（gcc/g++/make）
    pkg-config          # pkg-config 工具
)
```

**CentOS/RHEL 系列**（第 138-150 行）：
```bash
local packages=(
    openssl-devel       # OpenSSL 开发库
    zlib-devel          # zlib 压缩库
    systemd-devel       # systemd 开发库
    libcurl-devel       # libcurl
    json-c-devel        # JSON-C 库
    sqlite-devel        # SQLite3 开发库
    gtest-devel         # Google Test 框架
    cmake               # CMake 构建工具
    gcc-c++             # C++ 编译器
    make                # make 工具
    pkgconfig           # pkg-config 工具
)
```

> **调整方法**：在对应数组中增删包名即可。注意 Debian 和 RHEL 系列的包名可能不同（如 `libssl-dev` vs `openssl-devel`）。

### 4.2 切换数据库类型（MariaDB ↔ openGauss）

当前 `setup.sh` 和 `database/install.sh` 默认使用 **MariaDB**。如需切换到 **openGauss**，需修改两个文件：

#### 方法一：修改 setup.sh（推荐）

编辑 [setup.sh](setup.sh) 第 75 行，将数据库安装步骤替换为 openGauss 的安装命令：

```bash
# 原代码（MariaDB）：
run_step 2 "安装并初始化数据库" "bash '${SCRIPT_DIR}/database/install.sh'"

# 替换为 openGauss（需先准备 tar.gz 包放在 database/opengauss/ 目录下）：
run_step 2 "安装并初始化 openGauss" "bash '${SCRIPT_DIR}/database/opengauss/install.sh' --tar_path='${SCRIPT_DIR}/database/opengauss/opengauss-5.0.0.tar.gz' && bash '${SCRIPT_DIR}/database/opengauss/init.sh' --admin_password='YourPassword123'"
```

#### 方法二：修改 database/install.sh

编辑 [database/install.sh](database/install.sh)，将 MariaDB 的调用替换为 openGauss 的调用。

> **注意**：openGauss 需要提前准备对应架构的 tar.gz 安装包，可从 [openGauss 官网](https://opengauss.org/zh/download/) 下载。

### 4.3 调整数据库配置

#### MariaDB 配置

编辑 [database/mariadb/config.txt](database/mariadb/config.txt)：

```ini
admin_password = Test@123       # 管理员(root)密码（必填）、这个值应当从scm项目中获得
data = /data/test/mysql         # 数据目录（可选，默认 /var/lib/mysql）
force = true                    # 是否强制重新初始化（可选）
port = 3306                     # 监听端口（可选，默认 3306）
ip = 127.0.0.1                  # 绑定 IP（可选，默认 127.0.0.1）
character_set = utf8mb4         # 字符集（可选，默认 utf8mb4）
```

> 除 `admin_password` 外，其他字段均为可选。不填则使用默认值。

#### openGauss 配置

openGauss 通过命令行参数配置，无独立配置文件。常用参数：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--admin_password` | 必填 | 管理员密码 |
| `--data` | `/var/lib/opengauss/data` | 数据目录 |
| `--port` | `5432` | 监听端口 |
| `--ip` | `127.0.0.1` | 绑定 IP |
| `--install_path` | `/opt/opengauss` | 安装目录 |
| `--user` | `omm` | 运行用户 |

### 4.4 调整 Nginx 配置

#### 修改监听端口

编辑 [nginx/nginx/conf.d/default.conf](nginx/nginx/conf.d/default.conf) 第 2-3 行：

```nginx
listen       80 default_server;       # 修改为需要的端口
listen  [::]:80 default_server;       # IPv6 对应端口
```

#### 修改后端代理地址

编辑 [nginx/nginx/conf.d/http_ca.conf](nginx/nginx/conf.d/http_ca.conf)，修改各 `location` 块中的 `proxy_pass` 地址：

```nginx
location /web/ {
    proxy_pass http://127.0.0.1:8080;   # 修改为实际后端地址
    ...
}
```

#### 修改前端资源路径

前端资源位于 [nginx/frontend/](nginx/frontend/) 目录。替换前端时：
1. 将新的构建产物放入 `frontend/` 目录
2. 确保 `index.html` 存在
3. 重新执行 `deploy_config.sh` 或重启 Nginx

#### 调整上传大小限制

编辑 [nginx/nginx/conf.d/default.conf](nginx/nginx/conf.d/default.conf)：

```nginx
client_max_body_size 10G;        # 修改为需要的大小
```

### 4.5 调整前端资源

替换 `nginx/frontend/` 目录下的内容即可：

```bash
# 清空旧前端
rm -rf script/nginx/frontend/*

# 放入新前端构建产物
cp -r /path/to/new/dist/* script/nginx/frontend/

# 只修改静态文件、重启
sudo systemctl restart nginx

# 修改conf.d的配置
sudo bash script/nginx/deploy_config.sh
```

### 4.6 调整 SCM 版本和服务列表

#### 更新 SCM deb 包

将新的 deb 包放入 `scm_and_soft/` 目录，替换旧的 `qifeng_scm-*.deb`：

```bash
# 删除旧包
rm script/scm_and_soft/qifeng_scm-*.deb

# 放入新包
cp /path/to/qifeng_scm-0.0.2-Linux.deb script/scm_and_soft/
```

#### 调整服务安装列表

编辑 [scm_and_soft/install_order.txt](scm_and_soft/install_order.txt)：

```ini
# 格式：服务名  包文件名  是否自动启动
service_a    service_a.tar.gz    true
service_b    service_b.tar.gz    false    # 仅安装不启动
# service_c    service_c.tar.gz    true    # 注释掉不需要的服务
```

用于控制服务安装顺序和是否自动启动。
每新增一个服务，需将对应的 `.tar.gz` 包放入 `scm_and_soft/` 目录。

---

## 五、常见问题与检查清单

### 部署前检查

| 检查项 | 命令/方法 |
|--------|----------|
| 操作系统版本 | `cat /etc/os-release` |
| 系统架构 | `uname -m` |
| 磁盘空间 | `df -h`（建议 `/` 和 `/data` 至少 10GB 空闲） |
| 内存 | `free -h`（建议至少 2GB） |
| 网络连通性 | `ping -c 1 mirrors.aliyun.com` 或对应软件源 |
| 端口占用 | `ss -tlnp \| grep -E ':(80\|3306\|5432\|8080)'` |

### 部署后验证

| 验证项 | 命令 |
|--------|------|
| Nginx 状态 | `systemctl status nginx` 或 `nginx -t` |
| MariaDB 状态 | `systemctl status mariadb` 或 `mysql -u root -p -e "SELECT 1;"` |
| openGauss 状态 | `gs_ctl status -D /var/lib/opengauss/data` |
| SCM 命令 | `qf_scmc list` |
| Web 页面 | 浏览器访问 `http://<服务器IP>/` |

### 常见错误处理

| 错误现象 | 可能原因 | 解决方法 |
|---------|---------|---------|
| `apt-get update` 失败 | 网络不通或软件源配置问题 | 检查网络，更换软件源镜像 |
| 端口被占用 | 已有服务在运行 | `ss -tlnp` 查看占用进程，停止或更换端口 |
| Nginx 403 Forbidden | 目录权限不足 | 重新执行 `deploy_config.sh` 修复权限 |
| MariaDB 启动失败 | 数据目录权限问题或 AppArmor 限制 | 检查 `journalctl -u mariadb` 日志 |
| `qf_scmc: command not found` | SCM deb 包安装失败 | 检查 `dpkg -l \| grep qifeng` |
| openGauss 初始化失败 | 缺少 libaio 等依赖 | `apt-get install -y libaio1 libncurses5` |

### 日志位置

| 组件 | 日志路径 |
|------|---------|
| Nginx 访问日志 | `/var/log/nginx/access.log` |
| Nginx 错误日志 | `/var/log/nginx/error.log` |
| MariaDB 错误日志 | `/var/log/mysql/error.log` 或数据目录下的 `*.err` |
| openGauss 日志 | `$DATA_DIR/log/opengauss.log` |
| 部署脚本日志 | 标准输出（stdout），带 `[INFO]`/`[ERROR]` 时间戳 |
