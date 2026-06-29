#!/usr/bin/env bash
# BM1684-selftest 安装脚本：部署二进制、配置、systemd 单元并使能。
set -euo pipefail

PREFIX="${1:-/usr/local}"
ETC_DIR="/etc/bm1684-selftest"
LOG_DIR="/var/log/bm1684-selftest"

echo "[install] 部署到 PREFIX=${PREFIX}"
install -d "${PREFIX}/bin" "${ETC_DIR}" "${LOG_DIR}"

# 拷贝二进制（若已编译）
if [[ -x "${PWD}/build/bm1684-selftest" ]]; then
  install -m 0755 "${PWD}/build/bm1684-selftest" "${PREFIX}/bin/bm1684-selftest"
else
  echo "[install] 警告：未找到 build/bm1684-selftest，跳过二进制部署" >&2
fi

# 拷贝配置
install -m 0644 "${PWD}/config/selftest.json" "${ETC_DIR}/selftest.json"

# 拷贝 systemd 单元
install -m 0644 "${PWD}/scripts/bm1684-selftest.service" /etc/systemd/system/

systemctl daemon-reload
systemctl enable bm1684-selftest.service
echo "[install] 完成：systemctl enable bm1684-selftest.service"
