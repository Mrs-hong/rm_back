#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Windows 端 BM1684x 边缘设备一键自检脚本（Python 版）
功能与 run_check.bat 完全一致，使用标准库 subprocess 调用 ssh/scp。

依赖：
    - Python 3.7+
    - Windows 10/11 内置 OpenSSH（ssh.exe / scp.exe）

用法：
    python run_check.py
    python run_check.py --ip 192.168.112.47 --user linaro --password linaro
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import List, Optional


# 默认配置
DEFAULT_IP = "192.168.112.47"
DEFAULT_USER = "linaro"
DEFAULT_PASSWORD = "linaro"
DEFAULT_ROOT_PASSWORD = "linaro"
DEFAULT_SSH_PORT = 22
REMOTE_DEB_PATH = "/tmp/qifeng-scm.deb"
REMOTE_CONFIG_DIR = "/etc/qifeng-scm"
REMOTE_CONFIG_PATH = f"{REMOTE_CONFIG_DIR}/selftest.json"
REMOTE_REPORT_PATH = "/var/log/qifeng-scm/selftest-report.json"
REMOTE_SCRIPTS_DIR = "/opt/qifeng-scm/scripts"
REMOTE_PCBA_SCRIPT = f"{REMOTE_SCRIPTS_DIR}/pcba_check.sh"
SERVICE_NAME = "qifeng-scmd"
SERVICE_READY_TIMEOUT_SEC = 60
SERVICE_READY_INTERVAL_SEC = 2


def log_info(msg: str) -> None:
    """打印普通信息。"""
    print(f"[INFO] {msg}")


def log_warn(msg: str) -> None:
    """打印警告信息。"""
    print(f"[WARN] {msg}")


def log_error(msg: str) -> None:
    """打印错误信息。"""
    print(f"[ERROR] {msg}", file=sys.stderr)


def check_requirements() -> None:
    """检查系统是否满足运行依赖。"""
    for exe in ("ssh", "scp"):
        if shutil.which(exe) is None:
            log_error(f"未找到 {exe}.exe，请确认 Windows OpenSSH 客户端已安装并加入 PATH。")
            sys.exit(1)


def find_deb_package() -> str:
    """在同目录下查找最新的 qifeng-scm_*.deb。"""
    candidates = glob.glob("qifeng-scm_*.deb")
    if not candidates:
        log_error("当前目录未找到 qifeng-scm_*.deb，请将 deb 包与脚本放在同一目录。")
        sys.exit(1)
    # 按修改时间取最新
    candidates.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    return candidates[0]


def ssh_base_cmd(user: str, ip: str, port: int) -> List[str]:
    """构造 ssh 基础命令。"""
    return [
        "ssh",
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=NUL",
        "-p", str(port),
        f"{user}@{ip}",
    ]


def scp_base_cmd(user: str, ip: str, port: int) -> List[str]:
    """构造 scp 基础命令。"""
    return [
        "scp",
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=NUL",
        "-P", str(port),
    ]


def run_cmd(cmd: List[str], input_text: Optional[str] = None, capture: bool = False) -> subprocess.CompletedProcess:
    """执行本地命令并返回结果。"""
    if capture:
        return subprocess.run(cmd, input=input_text, text=True, capture_output=True)
    return subprocess.run(cmd, input=input_text, text=True)


def remote_exec(user: str, ip: str, port: int, command: str, password: Optional[str] = None,
                capture: bool = False) -> subprocess.CompletedProcess:
    """在远端执行单条命令。"""
    cmd = ssh_base_cmd(user, ip, port) + [command]
    inp = f"{password}\n" if password else None
    return run_cmd(cmd, input_text=inp, capture=capture)


def scp_upload(user: str, ip: str, port: int, local: str, remote: str) -> None:
    """上传本地文件到远端。"""
    cmd = scp_base_cmd(user, ip, port) + [local, f"{user}@{ip}:{remote}"]
    result = run_cmd(cmd)
    if result.returncode != 0:
        log_error(f"上传 {local} 到 {remote} 失败。")
        sys.exit(1)


def wait_for_service(user: str, ip: str, port: int, root_password: str) -> None:
    """等待 qf_scmd 服务进入 active 状态。"""
    log_info("等待 qf_scmd 服务就绪...")
    elapsed = 0
    while elapsed < SERVICE_READY_TIMEOUT_SEC:
        result = remote_exec(user, ip, port, f"systemctl is-active {SERVICE_NAME}",
                             password=root_password, capture=True)
        if result.returncode == 0 and "active" in result.stdout:
            log_info("qf_scmd 服务已就绪。")
            return
        time.sleep(SERVICE_READY_INTERVAL_SEC)
        elapsed += SERVICE_READY_INTERVAL_SEC
    log_error("等待 qf_scmd 服务超时。")
    pull_remote_log(user, ip, port, root_password)
    sys.exit(1)


def pull_remote_log(user: str, ip: str, port: int, root_password: str) -> None:
    """拉取远端 qf_scmd 日志到本地 logs/ 目录。"""
    log_dir = Path("logs")
    log_dir.mkdir(exist_ok=True)
    local_log = log_dir / f"qf_scmd-{ip}.log"
    cmd = ssh_base_cmd(user, ip, port) + [
        f"journalctl -u {SERVICE_NAME} -n 50 --no-pager"
    ]
    try:
        with open(local_log, "w", encoding="utf-8") as f:
            subprocess.run(cmd, input=f"{root_password}\n", text=True, stdout=f, stderr=subprocess.STDOUT)
        log_info(f"远端日志已保存到 {local_log}")
    except Exception as e:
        log_warn(f"拉取远端日志失败: {e}")


def main() -> int:
    """主入口。"""
    parser = argparse.ArgumentParser(description="BM1684x 边缘设备硬件自检")
    parser.add_argument("--ip", default=DEFAULT_IP, help=f"设备 IP（默认 {DEFAULT_IP}）")
    parser.add_argument("--user", default=DEFAULT_USER, help=f"SSH 用户名（默认 {DEFAULT_USER}）")
    parser.add_argument("--password", default=os.environ.get("SSH_PASSWORD", DEFAULT_PASSWORD),
                        help="SSH 用户密码（默认 linaro，也可通过环境变量 SSH_PASSWORD 传入）")
    parser.add_argument("--root-password", default=os.environ.get("ROOT_PASSWORD", DEFAULT_ROOT_PASSWORD),
                        help="root 密码（默认 linaro，也可通过环境变量 ROOT_PASSWORD 传入）")
    parser.add_argument("--port", type=int, default=DEFAULT_SSH_PORT, help=f"SSH 端口（默认 {DEFAULT_SSH_PORT}）")
    parser.add_argument("--deb", default=None, help="指定 deb 包路径，默认自动匹配同目录 qifeng-scm_*.deb")
    args = parser.parse_args()

    check_requirements()

    log_info(f"目标设备: {args.user}@{args.ip}")

    deb_file = args.deb if args.deb else find_deb_package()
    log_info(f"使用 deb 包: {deb_file}")

    log_dir = Path("logs")
    log_dir.mkdir(exist_ok=True)

    # 上传 deb 包
    log_info("上传 deb 包到设备...")
    scp_upload(args.user, args.ip, args.port, deb_file, REMOTE_DEB_PATH)

    # 安装 deb 包
    log_info("安装 qifeng-scm...")
    install_cmd = f"sudo -S dpkg -i {REMOTE_DEB_PATH} && sudo -S apt-get install -f -y"
    result = remote_exec(args.user, args.ip, args.port, install_cmd, password=args.root_password)
    if result.returncode != 0:
        log_error("安装 qifeng-scm 失败。")
        pull_remote_log(args.user, args.ip, args.port, args.root_password)
        return 1

    # 等待服务就绪
    wait_for_service(args.user, args.ip, args.port, args.root_password)

    # 确保远端配置目录存在
    remote_exec(args.user, args.ip, args.port,
                f"sudo -S mkdir -p {REMOTE_CONFIG_DIR}", password=args.root_password, capture=True)

    # 上传 selftest.json
    log_info("上传 selftest.json...")
    scp_upload(args.user, args.ip, args.port, "config/selftest.json", REMOTE_CONFIG_PATH)
    remote_exec(args.user, args.ip, args.port,
                f"sudo -S chmod 644 {REMOTE_CONFIG_PATH}", password=args.root_password, capture=True)

    # 上传 pcba 检测脚本
    log_info("上传 pcba 检测脚本...")
    remote_exec(args.user, args.ip, args.port,
                f"sudo -S mkdir -p {REMOTE_SCRIPTS_DIR}", password=args.root_password, capture=True)
    scp_upload(args.user, args.ip, args.port, "scripts/pcba_check.sh", "/tmp/pcba_check.sh")
    remote_exec(args.user, args.ip, args.port,
                f"sudo -S mv /tmp/pcba_check.sh {REMOTE_PCBA_SCRIPT} && sudo -S chmod +x {REMOTE_PCBA_SCRIPT}",
                password=args.root_password, capture=True)

    # 执行自检
    log_info("开始执行硬件自检...")
    result = remote_exec(args.user, args.ip, args.port,
                         f"qf_scmc check --config {REMOTE_CONFIG_PATH}")
    check_rc = result.returncode

    # 拉回报告
    log_info("拉回自检报告...")
    local_report = log_dir / f"selftest-report-{args.ip}.json"
    cmd = scp_base_cmd(args.user, args.ip, args.port) + [
        f"{args.user}@{args.ip}:{REMOTE_REPORT_PATH}",
        str(local_report),
    ]
    report_result = run_cmd(cmd, capture=True)
    if report_result.returncode != 0:
        log_warn("拉回自检报告失败，可能自检未生成报告。")

    if check_rc != 0:
        log_error("自检执行失败或存在关键项未通过，详情请查看上方输出或 local_report。")
        pull_remote_log(args.user, args.ip, args.port, args.root_password)
        return 1

    log_info(f"自检完成，结果已保存到 {local_report}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
