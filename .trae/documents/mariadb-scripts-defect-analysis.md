# MariaDB 脚本缺陷分析报告

## 分析范围

- `install.sh` — MariaDB 安装脚本
- `init.sh` — MariaDB 初始化脚本
- `uninstall.sh` — MariaDB 卸载脚本
- `common.sh` — 公共函数库（辅助分析）
- `create_user_and_execute.sh` — 创建用户并执行 SQL（辅助分析）

---

## 缺陷清单

### 🔴 严重（Critical）— 可能导致安全漏洞、数据丢失或脚本必现失败

#### C1. SQL 注入 / 命令注入风险（init.sh, create_user_and_execute.sh）

**位置**：
- [init.sh:276-281](file:///home/hong/code/rm_back/.data/script/database/mariadb/init.sh#L276-L281) — `mysql -u root -e "ALTER USER 'root'@'localhost' IDENTIFIED BY '${ADMIN_PASSWORD}';"`
- [create_user_and_execute.sh:78-83](file:///home/hong/code/rm_back/.data/script/database/mariadb/create_user_and_execute.sh#L78-L83) — `CREATE USER IF NOT EXISTS '${USER}'@'%' IDENTIFIED BY '${PASSWORD}';`

**问题**：密码和用户名直接拼接到 SQL 语句中，未做任何转义。如果密码包含单引号 `'`、反斜杠 `\` 或其他 SQL 特殊字符，将导致：
1. SQL 语法错误，脚本失败
2. 潜在的 SQL 注入（虽然此处是 root 执行，风险略低，但仍可导致非预期行为）

**严重性**：🔴 严重 — 含特殊字符的密码会直接导致初始化失败

**建议修复**：使用 `mysql -e` 的参数化方式，或对变量进行 SQL 转义（将 `'` 替换为 `''`，`\` 替换为 `\\`）

---

#### C2. 密码在命令行中暴露（init.sh, create_user_and_execute.sh）

**位置**：
- [init.sh:285](file:///home/hong/code/rm_back/.data/script/database/mariadb/init.sh#L285) — `mysql -u root -p"$ADMIN_PASSWORD"`
- [create_user_and_execute.sh:68](file:///home/hong/code/rm_back/.data/script/database/mariadb/create_user_and_execute.sh#L68) — `MYSQL_OPTS="-u${ADMIN_USER} -p${ADMIN_PASSWORD}"`

**问题**：
1. `-p"$ADMIN_PASSWORD"` 形式会被 `ps aux` 等命令看到完整密码（虽然部分 mysql 客户端会做 argv 混淆，但不可依赖）
2. 密码作为命令行参数传递，会被记录到 shell history、审计日志、/proc/PID/cmdline

**严重性**：🔴 严重 — 密码泄露风险

**建议修复**：使用 `mysql --defaults-extra-file=<(echo -e "[client]\npassword=$ADMIN_PASSWORD")` 或通过环境变量 `MYSQL_PWD` 传递（MariaDB 10.x 支持 `MYSQL_PWD` 环境变量，但会有警告；最佳实践是使用配置文件方式）

---

#### C3. `rm -rf` 无二次确认，且路径可能为空导致灾难性后果（uninstall.sh）

**位置**：
- [uninstall.sh:71](file:///home/hong/code/rm_back/.data/script/database/mariadb/uninstall.sh#L71) — `rm -rf /var/lib/mysql`
- [uninstall.sh:73](file:///home/hong/code/rm_back/.data/script/database/mariadb/uninstall.sh#L73) — `rm -rf /var/log/mysql`
- [init.sh:201](file:///home/hong/code/rm_back/.data/script/database/mariadb/init.sh#L201) — `rm -rf "$DATA_DIR"`（`DATA_DIR` 来自用户输入）

**问题**：
1. `--purge` 模式下直接 `rm -rf` 删除数据目录，无任何二次确认
2. `init.sh` 中 `DATA_DIR` 来自 `--data` 参数，如果用户传入 `--data=/` 或空值（虽然默认值是 `/var/lib/mysql`），将导致灾难性删除
3. `uninstall.sh:88` — `rm -rf /etc/mysql /etc/my.cnf /etc/my.cnf.d` 一次删除多个路径，若其中一个变量为空，不影响但也不安全

**严重性**：🔴 严重 — 数据丢失风险

**建议修复**：
1. 对 `DATA_DIR` 增加安全校验（不能为 `/`、不能为空、必须包含特定路径特征）
2. 在 `rm -rf` 前增加路径长度/特征校验
3. 对 `--purge` 操作增加交互式确认（或至少在日志中明确警告）

---

#### C4. `db_parse_args` 重新初始化关联数组的方式错误（common.sh）

**位置**：[common.sh:122](file:///home/hong/code/rm_back/.data/script/database/common.sh#L122) — `db_args=()`

**问题**：`db_args` 在第 120 行用 `declare -A db_args` 声明为关联数组，但在 `db_parse_args` 中用 `db_args=()` 重新赋值。在某些 Bash 版本中，对关联数组使用 `=()` 赋值可能行为不一致（某些版本会将其重置为普通索引数组），导致后续的字符串键查找失败。

**严重性**：🔴 严重 — 在特定 Bash 版本下参数解析完全失效

**建议修复**：使用 `db_args=()` 之前先 `unset db_args; declare -A db_args`，或直接遍历清空

---

### 🟠 高（High）— 可能导致脚本在特定场景下失败或行为异常

#### H1. 端口检查存在误判风险（common.sh）

**位置**：[common.sh:99-106](file:///home/hong/code/rm_back/.data/script/database/common.sh#L99-L106)

**问题**：
1. `ss -tuln | grep -q ":${port} "` — 末尾空格匹配不严谨，某些 `ss` 输出格式中端口后可能不是空格（如 `:3306\n` 或 `:33060`），导致误匹配或漏匹配
2. 端口 3306 会匹配到 33060 等端口（因为 `:3306` 是 `:33060` 的前缀）
3. fallback 使用 `/dev/tcp/127.0.0.1/` 仅检查 localhost，与 `--ip` 参数可能不一致

**严重性**：🟠 高 — 端口误判导致安装/初始化在端口实际被占用时继续执行，或在端口空闲时误报

**建议修复**：使用更精确的正则，如 `grep -qE ":${port}\s"` 或 `grep -wP ":${port}\b"`

---

#### H2. init.sh 中设置 root 密码的双重尝试逻辑有缺陷（init.sh）

**位置**：[init.sh:276-281](file:///home/hong/code/rm_back/.data/script/database/mariadb/init.sh#L276-L281)

```bash
mysql -u root -e "ALTER USER 'root'@'localhost' IDENTIFIED BY '${ADMIN_PASSWORD}'; FLUSH PRIVILEGES;" 2>/dev/null || {
    mysql -u root -e "ALTER USER 'root'@'localhost' IDENTIFIED BY '${ADMIN_PASSWORD}'; FLUSH PRIVILEGES;" || {
        db_exit_with_error ...
    }
}
```

**问题**：
1. 两次执行的命令完全相同，第一次 `2>/dev/null` 抑制了 stderr，但第一次失败的原因可能是语法错误、连接失败等，重试同样的命令没有意义
2. 注释说"首次安装可能没有密码，使用 unix_socket 认证"，但两次命令都没有使用 `--socket` 或其他认证方式差异
3. 如果第一次是因为密码中特殊字符导致的 SQL 语法错误，第二次同样会失败

**严重性**：🟠 高 — 设置密码可能失败且重试无效

**建议修复**：第一次尝试使用 socket 认证（`mysql -u root --socket=...`），第二次使用其他方式；或统一使用 `mysqladmin` 设置密码

---

#### H3. install.sh 中 dpkg 状态修复可能掩盖真实问题（install.sh）

**位置**：[install.sh:73-74](file:///home/hong/code/rm_back/.data/script/database/mariadb/install.sh#L73-L74)

```bash
dpkg --configure -a 2>/dev/null || true
apt-get install -f -y 2>/dev/null || true
```

**问题**：`2>/dev/null || true` 同时抑制了 stderr 输出和错误返回值，如果 dpkg 状态确实损坏且修复失败，用户完全看不到任何错误信息，后续安装可能在一个不一致的状态下继续。

**严重性**：🟠 高 — 静默失败导致后续操作在损坏状态下进行

**建议修复**：至少记录修复操作的输出到日志，失败时发出警告而非静默忽略

---

#### H4. 数据迁移逻辑中 `cp -a` 可能覆盖已有数据（init.sh）

**位置**：[init.sh:171](file:///home/hong/code/rm_back/.data/script/database/mariadb/init.sh#L171)

```bash
cp -a "$DEFAULT_DATA_DIR" "$DATA_DIR"
```

**问题**：
1. 如果 `$DATA_DIR` 已存在（之前 `rmdir` 失败因为目录非空），`cp -a` 会将源目录复制为目标目录的子目录，即 `$DATA_DIR/mysql/`，而非将内容复制到 `$DATA_DIR/`
2. 第 170 行 `rmdir "$DATA_DIR" 2>/dev/null || true` 仅在目录为空时成功，如果目录非空则静默失败

**严重性**：🟠 高 — 数据迁移后目录结构错误，MariaDB 无法启动

**建议修复**：使用 `rsync -a` 或 `cp -a "$DEFAULT_DATA_DIR"/. "$DATA_DIR"/` 确保复制内容而非目录本身

---

#### H5. uninstall.sh 的 `--purge` 不清理自定义数据目录（uninstall.sh）

**位置**：[uninstall.sh:68-90](file:///home/hong/code/rm_back/.data/script/database/mariadb/uninstall.sh#L68-L90)

**问题**：`--purge` 只清理 `/var/lib/mysql`（默认数据目录），但如果 init.sh 使用了 `--data=/custom/path` 指定了自定义数据目录，卸载时不会清理该目录。同时，init.sh 创建的 systemd override 文件中记录了自定义路径，但 uninstall.sh 的清理逻辑是硬编码的。

**严重性**：🟠 高 — 自定义数据目录残留，占用磁盘空间且可能包含敏感数据

**建议修复**：读取 `99-custom.cnf` 中的 `datadir` 配置，或在 init.sh 中记录配置到状态文件，卸载时据此清理

---

### 🟡 中（Medium）— 影响可用性或可维护性，但不直接导致数据丢失

#### M1. `db_check_port_in_use` 返回值语义反直觉（common.sh）

**位置**：[common.sh:97-107](file:///home/hong/code/rm_back/.data/script/database/common.sh#L97-L107)

**问题**：函数返回 0 表示端口被占用，返回 1 表示未占用。这与 Unix 惯例（0=成功/真，非0=失败/假）在 `if` 语境中容易混淆。调用处 `if db_check_port_in_use "$PORT"` 语义是"如果端口被占用"，虽然逻辑正确但阅读时需要额外思考。

**严重性**：🟡 中 — 可读性问题，容易在后续维护中引入 bug

---

#### M2. init.sh 中 `my.cnf` 修复逻辑可能破坏已有配置（init.sh）

**位置**：[init.sh:98-118](file:////hong/code/rm_back/.data/script/database/mariadb/init.sh#L98-L118)

**问题**：
1. 当 `NEEDS_FIX=true` 时，直接 `cat > "$MY_CNF"` 覆写 `/etc/mysql/my.cnf`，会丢失原有配置中的其他内容（如性能调优参数、其他 `!includedir` 指令）
2. 判断条件 `! grep -q "mariadb.conf.d" "$MY_CNF"` 过于严格，某些系统可能使用 `!includedir /etc/mysql/mariadb.conf.d/` 末尾带斜杠，或使用 `!include` 单文件包含

**严重性**：🟡 中 — 可能丢失用户自定义的 my.cnf 配置

---

#### M3. install.sh 中版本匹配不精确（install.sh）

**位置**：[install.sh:100](file:///home/hong/code/rm_back/.data/script/database/mariadb/install.sh#L100)

```bash
if apt-cache madison mariadb-server 2>/dev/null | grep -q "$VERSION"; then
```

**问题**：`grep -q "$VERSION"` 是子串匹配，如果用户指定 `--version=10.6`，可能匹配到 `10.6.1`、`10.6.12` 等多个版本，但后续 `apt-get install -y "mariadb-server=$VERSION"` 会因版本不精确而失败。

**严重性**：🟡 中 — 版本安装可能失败

**建议修复**：使用更精确的匹配，如 `grep -qE "^mariadb-server\s+\|${VERSION}\|"`

---

#### M4. create_user_and_execute.sh 中 SQL 文件执行无事务保护（create_user_and_execute.sh）

**位置**：[create_user_and_execute.sh:97-103](file:///home/hong/code/rm_back/.data/script/database/mariadb/create_user_and_execute.sh#L97-L103)

**问题**：逐个执行 SQL 文件，如果第 N 个文件执行失败，前 N-1 个文件的效果已提交，无法回滚。没有提供恢复或重试机制。

**严重性**：🟡 中 — 部分执行导致数据库状态不一致

---

#### M5. init.sh 中服务启动后的等待时间硬编码（init.sh）

**位置**：[init.sh:272](file:///home/hong/code/rm_back/.data/script/database/mariadb/init.sh#L272) — `sleep 2`

**问题**：硬编码等待 2 秒，在慢速机器或高负载环境下可能不够，导致后续设置密码时连接失败。同时 [init.sh:257](file:///home/hong/code/rm_back/.data/script/database/mariadb/init.sh#L257) 中 `systemctl start` 失败后 `sleep 3`，两处等待策略不一致。

**严重性**：🟡 中 — 在特定环境下脚本不稳定

**建议修复**：使用循环+超时机制等待服务就绪，如 `mysqladmin ping` 重试

---

### 🟢 低（Low）— 代码质量、最佳实践问题

#### L1. `db_log_warn` 输出到 stdout 而非 stderr（common.sh）

**位置**：[common.sh:35-37](file:////home/hong/code/rm_back/.data/script/database/common.sh#L35-L37)

**问题**：`db_log_warn` 输出到 stdout，而 `db_log_error` 输出到 stderr。警告信息应与错误信息一样输出到 stderr，便于日志分离。

---

#### L2. install.sh 中架构参数未实际使用（install.sh）

**位置**：[install.sh:34](file:///home/hong/code/rm_back/.data/script/database/mariadb/install.sh#L34) — `ARCH=$(db_arg "arch" "")`

**问题**：`ARCH` 变量被检测和打印，但后续安装逻辑中从未使用。无论是 apt 安装还是 deb 安装，都没有根据架构做任何区分。

---

#### L3. uninstall.sh 中 `dpkg -S` 检查可能不可靠（uninstall.sh）

**位置**：[uninstall.sh:87](file:///home/hong/code/rm_back/.data/script/database/mariadb/uninstall.sh#L87)

**问题**：`dpkg -S /etc/mysql` 检查目录是否由某个包管理。但 `dpkg -S` 对目录的匹配依赖于包的文件列表中是否包含该目录，某些包可能不注册目录本身，导致误判。

---

#### L4. init.sh 中 `chown` 失败仅打印警告但继续执行（init.sh）

**位置**：[init.sh:82](file:///home/hong/code/rm_back/.data/script/database/mariadb/init.sh#L82)、[init.sh:174](file:///home/hong/code/rm_back/.data/script/database/mariadb/init.sh#L174)、[init.sh:206](file:///home/hong/code/rm_back/.data/script/database/mariadb/init.sh#L206)

**问题**：多处 `chown -R mysql:mysql` 失败后仅打印警告继续执行，但 MariaDB 服务以 mysql 用户运行时可能因权限不足无法读写数据目录，导致服务启动失败。这在容器环境中尤其常见。

---

## 严重性汇总

| 级别 | 数量 | 编号 |
|------|------|------|
| 🔴 严重 | 4 | C1, C2, C3, C4 |
| 🟠 高 | 5 | H1, H2, H3, H4, H5 |
| 🟡 中 | 5 | M1, M2, M3, M4, M5 |
| 🟢 低 | 4 | L1, L2, L3, L4 |
| **合计** | **18** | |

## 优先修复建议

1. **立即修复**：C1（SQL 注入）、C3（rm -rf 安全）、C4（关联数组重置）
2. **尽快修复**：C2（密码暴露）、H2（密码设置重试逻辑）、H4（数据迁移路径问题）
3. **计划修复**：H1（端口检查）、H3（静默失败）、H5（自定义目录清理）、M 级别问题
4. **择机优化**：L 级别问题
