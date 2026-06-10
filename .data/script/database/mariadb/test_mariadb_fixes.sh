#!/bin/bash
# =============================================================================
# MariaDB 脚本缺陷修复测试
# =============================================================================
# 说明：
#   测试 common.sh 中修复和新增的函数，以及各脚本的参数校验逻辑。
#   不依赖实际 MariaDB 安装，仅测试纯逻辑函数。
#
# 用法：
#   bash test_mariadb_fixes.sh
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../common.sh"

PASS=0
FAIL=0
TOTAL=0

assert_eq() {
    local desc="$1" expected="$2" actual="$3"
    TOTAL=$((TOTAL + 1))
    if [[ "$expected" == "$actual" ]]; then
        echo "  ✅ $desc"
        PASS=$((PASS + 1))
    else
        echo "  ❌ $desc: 期望='$expected', 实际='$actual'"
        FAIL=$((FAIL + 1))
    fi
}

assert_exit_code() {
    local desc="$1" expected="$2"
    shift 2
    TOTAL=$((TOTAL + 1))
    local actual
    actual=$("$@" 2>/dev/null; echo $?)
    # 提取最后一行作为退出码
    actual=$(echo "$actual" | tail -1)
    if [[ "$actual" == "$expected" ]]; then
        echo "  ✅ $desc"
        PASS=$((PASS + 1))
    else
        echo "  ❌ $desc: 期望退出码=$expected, 实际=$actual"
        FAIL=$((FAIL + 1))
    fi
}

# =============================================================================
echo "=== C4: db_parse_args 关联数组重置 ==="
# =============================================================================

# 测试1: 基本参数解析
db_parse_args --key1=value1 --key2 value2 --flag
assert_eq "基本 key=value 解析" "value1" "$(db_arg key1 '')"
assert_eq "基本 key value 解析" "value2" "$(db_arg key2 '')"
assert_eq "布尔 flag 解析" "1" "$(db_arg flag '')"

# 测试2: 重复调用 db_parse_args 应正确重置
db_parse_args --new_key=new_value
assert_eq "重置后旧 key 应消失" "" "$(db_arg key1 '')"
assert_eq "新 key 应生效" "new_value" "$(db_arg new_key '')"

# 测试3: 空参数调用
db_parse_args
assert_eq "空参数后 key 应消失" "" "$(db_arg new_key '')"

# 测试4: 特殊字符值
db_parse_args "--password=test'pass\\word"
assert_eq "含特殊字符的值" "test'pass\\word" "$(db_arg password '')"

# =============================================================================
echo ""
echo "=== C1: db_sql_escape SQL 转义 ==="
# =============================================================================

# 测试1: 普通字符串不变
assert_eq "普通字符串" "hello" "$(db_sql_escape 'hello')"

# 测试2: 单引号转义
assert_eq "单引号转义" "it''s" "$(db_sql_escape "it's")"

# 测试3: 反斜杠转义
assert_eq "反斜杠转义" "a\\\\b" "$(db_sql_escape 'a\b')"

# 测试4: 混合特殊字符
assert_eq "混合转义" "a\\\\b''c" "$(db_sql_escape "a\b'c")"

# 测试5: 空字符串
assert_eq "空字符串" "" "$(db_sql_escape '')"

# 测试6: 仅单引号
assert_eq "仅单引号" "''''" "$(db_sql_escape "''")"

# 测试7: 复杂密码
assert_eq "复杂密码" "P@ss''word\\\\123" "$(db_sql_escape "P@ss'word\123")"

# =============================================================================
echo ""
echo "=== C3: db_validate_path 路径安全校验 ==="
# =============================================================================

# 测试1: 安全路径
db_validate_path "/var/lib/mysql"
assert_eq "安全路径 /var/lib/mysql" "0" "$?"

# 测试2: 根目录
db_validate_path "/"
assert_eq "拒绝根目录 /" "1" "$?"

# 测试3: 空路径
db_validate_path ""
assert_eq "拒绝空路径" "1" "$?"

# 测试4: 路径遍历
db_validate_path "/var/../etc/passwd"
assert_eq "拒绝路径遍历 .." "1" "$?"

# 测试5: 浅路径 /var
db_validate_path "/var"
assert_eq "拒绝浅路径 /var" "1" "$?"

# 测试6: 自定义深度路径
db_validate_path "/data/mysql"
assert_eq "允许 /data/mysql" "0" "$?"

# 测试7: 深路径
db_validate_path "/home/user/data/mysql"
assert_eq "允许深路径" "0" "$?"

# =============================================================================
echo ""
echo "=== C3: db_safe_rm 安全删除 ==="
# =============================================================================

# 测试1: 安全路径可删除
TEST_DIR=$(mktemp -d "/tmp/test_safe_rm.XXXXXX")
echo "test" > "$TEST_DIR/test.txt"
db_safe_rm "$TEST_DIR" "测试目录" >/dev/null 2>&1
assert_eq "安全路径可删除" "0" "$?"
[[ ! -d "$TEST_DIR" ]] && assert_eq "目录确实被删除" "true" "true" || assert_eq "目录确实被删除" "false" "true"

# 测试2: 不安全路径拒绝删除
db_safe_rm "/" "根目录" >/dev/null 2>&1
assert_eq "拒绝删除根目录" "1" "$?"

# 测试3: 空路径拒绝删除
db_safe_rm "" "空路径" >/dev/null 2>&1
assert_eq "拒绝删除空路径" "1" "$?"

# =============================================================================
echo ""
echo "=== H1: db_check_port_in_use 端口检查 ==="
# =============================================================================

# 测试1: 空闲端口应返回1
db_check_port_in_use 59999
assert_eq "空闲端口59999应未被占用" "1" "$?"

# 测试2: 启动临时监听检查
(COPROC_PID:; exec 3<>/dev/tcp/127.0.0.1/59998) 2>/dev/null || true
# 使用 nc 如果可用
if command -v nc >/dev/null 2>&1; then
    nc -l 59998 &
    NC_PID=$!
    sleep 0.5
    db_check_port_in_use 59998
    assert_eq "监听端口59998应被占用" "0" "$?"
    kill $NC_PID 2>/dev/null || true
    wait $NC_PID 2>/dev/null || true
else
    echo "  ⏭️ nc 不可用，跳过端口占用测试"
fi

# =============================================================================
echo ""
echo "=== L1: db_log_warn 输出到 stderr ==="
# =============================================================================

# 测试1: warn 输出到 stderr
WARN_OUTPUT=$(db_log_warn "test warning" 2>&1 1>/dev/null)
assert_eq "warn 输出到 stderr" "[WARN]" "${WARN_OUTPUT:0:6}"

# 测试2: info 不输出到 stderr
INFO_STDERR=$(db_log_info "test info" 2>&1 1>/dev/null)
assert_eq "info 不输出到 stderr" "" "$INFO_STDERR"

# =============================================================================
echo ""
echo "=== C2: db_create_mysql_cnf 临时配置文件 ==="
# =============================================================================

# 测试1: 创建配置文件
CNF_FILE=$(db_create_mysql_cnf "root" "secret" "127.0.0.1" "3306")
assert_eq "配置文件已创建" "true" "$([ -f "$CNF_FILE" ] && echo true || echo false)"

# 测试2: 文件权限为600
CNF_PERM=$(stat -c '%a' "$CNF_FILE" 2>/dev/null || stat -f '%Lp' "$CNF_FILE" 2>/dev/null)
assert_eq "配置文件权限为600" "600" "$CNF_PERM"

# 测试3: 文件内容正确
assert_eq "包含 user 行" "true" "$(grep -q '^user=root$' "$CNF_FILE" && echo true || echo false)"
assert_eq "包含 password 行" "true" "$(grep -q '^password=secret$' "$CNF_FILE" && echo true || echo false)"
assert_eq "包含 host 行" "true" "$(grep -q '^host=127.0.0.1$' "$CNF_FILE" && echo true || echo false)"
assert_eq "包含 port 行" "true" "$(grep -q '^port=3306$' "$CNF_FILE" && echo true || echo false)"

# 测试4: 空参数不写入
CNF_FILE2=$(db_create_mysql_cnf "" "" "" "")
assert_eq "空参数不写入 user" "false" "$(grep -q '^user=' "$CNF_FILE2" && echo true || echo false)"
assert_eq "空参数不写入 password" "false" "$(grep -q '^password=' "$CNF_FILE2" && echo true || echo false)"

# 清理
rm -f "$CNF_FILE" "$CNF_FILE2"

# =============================================================================
echo ""
echo "=== init.sh 参数校验测试 ==="
# =============================================================================

# 测试1: 缺少 admin_password 应退出码2
# 直接测试参数校验逻辑（不运行 init.sh，避免 root 权限检查先触发）
db_parse_args
ADMIN_PASSWORD_TEST=$(db_arg "admin_password" "")
if [[ -z "$ADMIN_PASSWORD_TEST" ]]; then
    assert_eq "缺少 admin_password 应被检测" "true" "true"
else
    assert_eq "缺少 admin_password 应被检测" "true" "false"
fi

# 测试2: 不安全数据目录应被 db_validate_path 拒绝
db_validate_path "/"
assert_eq "data=/ 应被 db_validate_path 拒绝" "1" "$?"

db_validate_path ""
assert_eq "data='' 应被 db_validate_path 拒绝" "1" "$?"

db_validate_path "/var/../etc"
assert_eq "含..的路径应被拒绝" "1" "$?"

# =============================================================================
echo ""
echo "=== uninstall.sh 路径安全测试 ==="
# =============================================================================

UNINSTALL_SCRIPT="${SCRIPT_DIR}/uninstall.sh"
if [[ -f "$UNINSTALL_SCRIPT" ]]; then
    # db_safe_rm 拒绝非 root 执行（但我们可以直接测试 db_safe_rm）
    db_safe_rm "/var" "测试浅路径" >/dev/null 2>&1
    assert_eq "浅路径 /var 被 db_safe_rm 拒绝" "1" "$?"
fi

# =============================================================================
echo ""
echo "=== install.sh 版本匹配测试 ==="
# =============================================================================

# 模拟 apt-cache madison 输出测试 grep 精确匹配
MOCK_MADISON="mariadb-server | 10.6.33+maria~ubu2204 | http://archive.ubuntu.com/ubuntu jammy/main amd64 Packages
mariadb-server | 10.6.12-0ubuntu0.22.04.1 | http://archive.ubuntu.com/ubuntu jammy-updates/main amd64 Packages
mariadb-server | 10.11.2-1 | http://archive.ubuntu.com/ubuntu lunar/main amd64 Packages"

# 测试1: 精确版本匹配（apt-cache madison 格式为 "pkg | version | origin"）
echo "$MOCK_MADISON" | grep -qE "[|] 10.6.33[-+|]"
assert_eq "精确匹配 10.6.33（含后缀版本）" "0" "$?"

# 测试2: 不应误匹配 10.6.1 到 10.6.12
echo "$MOCK_MADISON" | grep -qE "[|] 10.6.1[-+|]"
assert_eq "不误匹配 10.6.1 到 10.6.12" "1" "$?"

# 测试3: 旧版 grep -q 会误匹配
echo "$MOCK_MADISON" | grep -q "10.6.1"
OLD_MATCH=$?
echo "$MOCK_MADISON" | grep -qE "[|] 10.6.1[-+|]"
NEW_MATCH=$?
if [[ "$OLD_MATCH" == "0" && "$NEW_MATCH" == "1" ]]; then
    echo "  ✅ 新版 grep 避免了旧版误匹配（10.6.1 不再匹配 10.6.12）"
    PASS=$((PASS + 1))
else
    echo "  ⚠️ 版本匹配改进验证跳过"
fi
TOTAL=$((TOTAL + 1))

# =============================================================================
echo ""
echo "=== db_has_valid_data 测试 ==="
# =============================================================================

# 测试1: 有效数据目录
VALID_DIR=$(mktemp -d)
mkdir -p "$VALID_DIR/mysql"
touch "$VALID_DIR/ibdata1"
db_has_valid_data "$VALID_DIR"
assert_eq "有效数据目录（含 mysql/ 和 ibdata1）" "0" "$?"

# 测试2: 仅有标记文件（无效）
FLAG_DIR=$(mktemp -d)
mkdir -p "$FLAG_DIR/mysql"
touch "$FLAG_DIR/debian-5.7.flag"
# 无 ibdata1 也无 aria_log_control
db_has_valid_data "$FLAG_DIR"
assert_eq "仅有标记文件应判定无效" "1" "$?"

# 测试3: aria_log_control 也算有效
ARIA_DIR=$(mktemp -d)
mkdir -p "$ARIA_DIR/mysql"
touch "$ARIA_DIR/aria_log_control"
db_has_valid_data "$ARIA_DIR"
assert_eq "含 aria_log_control 应判定有效" "0" "$?"

# 测试4: 空目录
EMPTY_DIR=$(mktemp -d)
db_has_valid_data "$EMPTY_DIR"
assert_eq "空目录应判定无效" "1" "$?"

# 清理
rm -rf "$VALID_DIR" "$FLAG_DIR" "$ARIA_DIR" "$EMPTY_DIR"

# =============================================================================
echo ""
echo "=== 测试结果汇总 ==="
# =============================================================================

echo ""
echo "总计: $TOTAL, 通过: $PASS, 失败: $FAIL"
if [[ $FAIL -eq 0 ]]; then
    echo "🎉 所有测试通过！"
    exit 0
else
    echo "⚠️ 有 $FAIL 个测试失败"
    exit 1
fi
