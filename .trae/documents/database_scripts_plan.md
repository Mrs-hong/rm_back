# 数据库操作脚本实现计划

## 1. 摘要

根据 `.data/script/about.md` 和 `.data/script/database/design.md` 的设计意图，为 **MariaDB** 和 **openGauss** 实现一套完整的数据库运维脚本。脚本需支持在 C++ 程序中调用，具备明确的参数、返回值和详细注释。支持 Ubuntu/Debian (deb) 及 x86_64/aarch64 架构，**不原生支持 Windows**。

## 2. 当前状态分析

- 代码库中仅有设计文档，无现有脚本实现。
- 设计文档初步定义了 7 类脚本：`install.sh`、`init.sh`、`start.sh`、`create_user_and_execute.sh`、`stop.sh`、`restart.sh`、`uninstall.sh`。
- 参数设计尚不完善，需结合数据库实际特性和跨架构需求进行补充。

## 3. 设计规范（通用约定）

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

### 3.2 返回值规范（供 C++ 程序解析）
| 返回值 | 含义 |
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

### 3.3 输出规范
- **标准输出 (stdout)**：正常日志信息，前缀 `[INFO]`
- **标准错误 (stderr)**：错误信息，前缀 `[ERROR]`
- C++ 程序可通过捕获 stdout/stderr 获取执行详情，通过 exit code 判断结果。

### 3.4 公共能力
每个脚本头部包含：
- `check_root()`：检查是否以 root 权限运行（openGauss 的 `init.sh` 等部分操作可能需要非 root，视具体脚本而定）。
- `check_arch()`：检测架构（`x86_64` / `aarch64`），用于选择正确的安装包。
- `check_os()`：检测 OS 类型（`ubuntu` / `debian` / `centos` / `rhel` 等），当前主要适配 Debian 系。
- `log_info()` / `log_error()`：统一日志输出函数。

## 4. MariaDB 脚本设计

### 4.1 install.sh
```bash
# 用途：安装 MariaDB
# 参数：
#   --version=VERSION          目标版本，如 10.6.33（可选，默认使用系统源最新版）
#   --deb_path=PATH            本地 deb 包路径（可选，若系统源无指定版本则使用）
#   --arch=ARCH                强制指定架构 x86_64|aarch64（可选，默认自动检测）
# 逻辑：
#   1. 检查是否已安装，已安装则跳过并返回 0
#   2. 若指定版本，先尝试 apt 安装该版本
#   3. 若 apt 无该版本且提供了 --deb_path，则使用 dpkg -i 安装
#   4. 安装依赖（apt-get install -f）
# 返回：0|2|3|4|6
```

### 4.2 init.sh
```bash
# 用途：初始化数据库实例
# 参数：
#   --admin_password=PWD       管理员(root)密码（必填）
#   --data=DIR                 数据目录（可选，默认 /var/lib/mysql）
#   --port=PORT                监听端口（可选，默认 3306）
#   --ip=IP                    绑定 IP（可选，默认 127.0.0.1）
#   --character_set=SET        字符集（可选，默认 utf8mb4）
# 逻辑：
#   1. 若数据目录为空，执行 mysql_install_db / mariadb-install-db
#   2. 修改配置文件（/etc/mysql/mariadb.conf.d/ 或 /etc/my.cnf.d/）
#   3. 设置 root 密码
#   4. 启动服务并设置为开机自启
# 返回：0|2|3|5|6|7
```

### 4.3 start.sh
```bash
# 用途：启动 MariaDB 服务
# 参数：无
# 逻辑：systemctl start mariadb 或 service mysql start
# 返回：0|3|5
```

### 4.4 stop.sh
```bash
# 用途：停止 MariaDB 服务
# 参数：无
# 逻辑：systemctl stop mariadb
# 返回：0|3|5
```

### 4.5 restart.sh
```bash
# 用途：重启 MariaDB 服务
# 参数：无
# 逻辑：systemctl restart mariadb
# 返回：0|3|5
```

### 4.6 uninstall.sh
```bash
# 用途：卸载 MariaDB
# 参数：
#   --purge                    是否同时删除数据目录和配置文件（可选）
# 逻辑：
#   1. 停止服务
#   2. apt remove / apt purge
#   3. 可选清理数据目录
# 返回：0|3
```

### 4.7 create_user_and_execute.sh
```bash
# 用途：创建数据库用户并执行 SQL 脚本
# 参数：
#   --admin_user=USER          管理员用户名（默认 root）
#   --admin_password=PWD       管理员密码（必填）
#   --user=USER                待创建的用户名（必填）
#   --password=PWD             待创建用户的密码（必填）
#   --sql_dir=DIR              SQL 脚本所在目录（可选，若提供则顺序执行所有 .sql 文件）
#   --host=HOST                连接地址（默认 127.0.0.1）
#   --port=PORT                连接端口（默认 3306）
# 逻辑：
#   1. 使用管理员账号连接
#   2. 创建用户并授权
#   3. 若提供 sql_dir，逐条执行目录下 .sql 文件
# 返回：0|2|8
```

## 5. openGauss 脚本设计

openGauss 与 MariaDB 在架构上有显著差异：
- 安装介质通常为 tar.gz（极简版/轻量版），非 deb/rpm。
- 初始化时需要创建独立的操作系统用户（如 `omm`）。
- 服务管理使用 `gs_ctl` 而非 systemctl（极简版场景）。

因此参数允许差异，保持脚本名和整体风格一致。

### 5.1 install.sh
```bash
# 用途：安装 openGauss
# 参数：
#   --version=VERSION          目标版本，如 5.0.0（可选）
#   --tar_path=PATH            本地 tar.gz 包路径（可选）
#   --arch=ARCH                强制指定架构（可选，默认自动检测）
#   --install_path=DIR         安装目录（可选，默认 /opt/opengauss）
# 逻辑：
#   1. 创建 opengauss 用户组和用户（如 omm）
#   2. 若提供 tar_path，解压到 install_path
#   3. 若无 tar_path，尝试从官方仓库下载对应架构版本（Ubuntu/Debian 兼容性处理）
#   4. 设置目录权限和环境变量
# 返回：0|2|3|4|6
```

### 5.2 init.sh
```bash
# 用途：初始化 openGauss 数据库实例
# 参数：
#   --admin_password=PWD       管理员密码（必填，对应 omm 用户或初始用户）
#   --data=DIR                 数据目录（可选，默认 /var/lib/opengauss/data）
#   --port=PORT                监听端口（可选，默认 5432）
#   --ip=IP                    监听 IP（可选，默认 127.0.0.1）
#   --user=OS_USER             运行 openGauss 的操作系统用户（可选，默认 omm）
# 逻辑：
#   1. 以指定 OS 用户执行 gs_initdb
#   2. 修改 postgresql.conf 和 pg_hba.conf
#   3. 启动实例
# 返回：0|2|3|5|6|7
```

### 5.3 start.sh
```bash
# 用途：启动 openGauss 服务
# 参数：
#   --data=DIR                 数据目录（可选，默认 /var/lib/opengauss/data）
#   --user=OS_USER             运行用户（默认 omm）
# 逻辑：切换到指定用户，执行 gs_ctl start
# 返回：0|3|5
```

### 5.4 stop.sh
```bash
# 用途：停止 openGauss 服务
# 参数：
#   --data=DIR                 数据目录（可选）
#   --user=OS_USER             运行用户（默认 omm）
#   --mode=MODE                停止模式：smart|fast|immediate（可选，默认 fast）
# 逻辑：gs_ctl stop
# 返回：0|3|5
```

### 5.5 restart.sh
```bash
# 用途：重启 openGauss 服务
# 参数：同 start.sh + stop.sh
# 逻辑：先 stop 后 start
# 返回：0|3|5
```

### 5.6 uninstall.sh
```bash
# 用途：卸载 openGauss
# 参数：
#   --purge                    是否同时删除数据目录、安装目录和操作系统用户
#   --install_path=DIR         安装目录（默认 /opt/opengauss）
#   --user=OS_USER             操作系统用户（默认 omm）
# 逻辑：
#   1. 停止服务
#   2. 删除安装目录
#   3. 可选删除数据目录和 OS 用户
# 返回：0|3
```

### 5.7 create_user_and_execute.sh
```bash
# 用途：创建数据库用户并执行 SQL 脚本
# 参数：
#   --admin_user=USER          管理员用户名（默认 omm 或 gaussdb）
#   --admin_password=PWD       管理员密码（必填）
#   --user=USER                待创建的数据库用户名（必填）
#   --password=PWD             待创建用户的密码（必填）
#   --sql_dir=DIR              SQL 脚本目录（可选）
#   --host=HOST                连接地址（默认 127.0.0.1）
#   --port=PORT                连接端口（默认 5432）
#   --database=DB              连接的数据库（默认 postgres）
# 逻辑：
#   1. 使用 gsql 连接并创建用户
#   2. 授权
#   3. 执行 sql_dir 下 .sql 文件
# 返回：0|2|8
```

## 6. 实现步骤

1. 创建目录结构 `.data/script/database/mariadb/` 和 `.data/script/database/opengauss/`。
2. 编写公共函数库 `.data/script/database/common.sh`（日志、参数解析、架构检测、root 检测等）。
3. 逐个实现 MariaDB 的 7 个脚本。
4. 逐个实现 openGauss 的 7 个脚本。
5. 为所有脚本添加可执行权限 (`chmod +x`)。
6. 编写测试命令文档（由于涉及 sudo，由用户在本地执行并反馈结果）。

## 7. 测试方案

由于安装/卸载数据库需要 sudo 权限且会影响系统环境，测试采用**命令行参数校验 + 轻量级沙盒**策略：

### 7.1 参数与语法测试（无需 sudo，可自动执行）
```bash
# MariaDB 参数帮助测试
bash .data/script/database/mariadb/install.sh --help
bash .data/script/database/mariadb/init.sh --help
bash .data/script/database/mariadb/create_user_and_execute.sh --help

# openGauss 参数帮助测试
bash .data/script/database/opengauss/install.sh --help
bash .data/script/database/opengauss/init.sh --help
```

### 7.2 功能测试（需要 sudo，由用户手动执行）
```bash
# ---------- MariaDB 测试 ----------
# 1. 安装（使用系统源）
sudo bash .data/script/database/mariadb/install.sh

# 2. 初始化
sudo bash .data/script/database/mariadb/init.sh --admin_password=Test@123456 --port=3307

# 3. 状态检查与启停
sudo bash .data/script/database/mariadb/start.sh
sudo bash .data/script/database/mariadb/restart.sh
sudo bash .data/script/database/mariadb/stop.sh

# 4. 创建用户并执行 SQL（需先启动）
sudo bash .data/script/database/mariadb/start.sh
sudo bash .data/script/database/mariadb/create_user_and_execute.sh \
  --admin_password=Test@123456 --user=testuser --password=Test@123 \
  --sql_dir=/path/to/sql

# 5. 卸载
sudo bash .data/script/database/mariadb/uninstall.sh --purge

# ---------- openGauss 测试 ----------
# 1. 安装（若本地无 tar 包，脚本会提示下载或报错）
sudo bash .data/script/database/opengauss/install.sh --install_path=/opt/opengauss

# 2. 初始化
sudo bash .data/script/database/opengauss/init.sh \
  --admin_password=Test@123456 --port=5433 --user=omm

# 3. 启停
sudo bash .data/script/database/opengauss/start.sh --user=omm
sudo bash .data/script/database/opengauss/restart.sh --user=omm
sudo bash .data/script/database/opengauss/stop.sh --user=omm

# 4. 创建用户并执行 SQL
sudo bash .data/script/database/opengauss/create_user_and_execute.sh \
  --admin_password=Test@123456 --user=testuser --password=Test@123 \
  --sql_dir=/path/to/sql --port=5433

# 5. 卸载
sudo bash .data/script/database/opengauss/uninstall.sh --purge --user=omm
```

### 7.3 用户反馈后修复
用户执行上述测试命令后，将实际输出反馈给我，我会根据报错和环境差异修复脚本，直至测试通过。

## 8. 假设与决策

- **假设 1**：Ubuntu/Debian 系统已配置好网络，可访问 apt 源或互联网（以下载 openGauss 包）。
- **假设 2**：测试环境允许安装和卸载数据库，或用户会使用容器/虚拟机隔离。
- **假设 3**：C++ 调用方式为 `system()` 或 `popen()` 执行脚本，通过 exit code 和 stdout/stderr 获取结果。
- **决策 1**：提取公共函数到 `common.sh`，由各个脚本 `source` 引入，避免重复代码。
- **决策 2**：openGauss 采用轻量版/极简版 tar.gz 安装逻辑，而非企业版 rpm，以适配 Debian 系。
- **决策 3**：所有脚本内置 `--help` 参数，输出完整用法说明，便于 C++ 程序集成时查阅。
