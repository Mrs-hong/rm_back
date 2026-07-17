#!/bin/bash
# ==========================================================================
# SCM 安装前置依赖包检查脚本
#
# 检查 qifeng-scm 运行所需的全部依赖包是否已安装，仅报告不安装。
# 被 pcba_check.sh 在安装 SCM 之前调用，用于提前发现缺失依赖。
#
# 退出码：0=全部已安装，1=有缺失（pcba_check.sh 仍继续，由 apt-get install -f 兜底）
# ==========================================================================

set -o pipefail

# ===================== 颜色定义 =====================
# 检测 stdout 是否为 TTY：非 TTY（如 plink 管道/重定向）时禁用颜色码，
# 避免 ANSI 转义序列污染输出
if [ -t 1 ]; then
    G='\e[32m'; R='\e[31m'; Y='\e[33m'; N='\e[0m'
else
    G=''; R=''; Y=''; N=''
fi

# ===================== 依赖包列表 =====================
# qifeng-scm 运行所需的系统级依赖包
REQUIRED_PACKAGES=(
    mariadb-server
    nginx
    dnsmasq
    protobuf-compiler
    uuid
    binutils
    libpq
    libcurl4-openssl
    default-libmysqlclient
    libgpiod
    libasound2
    libsystemd
    zlib1g
)

# ===================== 结果统计 =====================
INSTALLED_COUNT=0
MISSING_COUNT=0
MISSING_PACKAGES=""

# ===================== 主流程 =====================
echo "================================="
echo " SCM 依赖包检查"
echo "================================="

# 遍历检查每个依赖包
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    # 使用 dpkg -l 检查包状态，'^ii' 表示已正确安装
    if dpkg -l "$pkg" 2>/dev/null | grep -q '^ii'; then
        echo -e " [${G}已安装${N}] $pkg"
        INSTALLED_COUNT=$((INSTALLED_COUNT + 1))
    else
        echo -e " [${R}缺失  ${N}] $pkg"
        MISSING_COUNT=$((MISSING_COUNT + 1))
        MISSING_PACKAGES="${MISSING_PACKAGES}${pkg} "
    fi
done

# ===================== 汇总结果 =====================
echo "---------------------------------"
TOTAL=${#REQUIRED_PACKAGES[@]}
if [ "$MISSING_COUNT" -eq 0 ]; then
    echo -e " 结论: ${G}成功${N}，全部 ${TOTAL} 个依赖包已安装"
    echo "================================="
    exit 0
else
    echo -e " 结论: ${R}失败${N}，缺失 ${MISSING_COUNT}/${TOTAL} 个依赖包"
    echo -e " 缺失包: ${MISSING_PACKAGES}"
    echo -e " ${Y}提示${N}: 安装 SCM 时 apt-get install -f 将自动安装缺失依赖"
    echo "================================="
    exit 1
fi
