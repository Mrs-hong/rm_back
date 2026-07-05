#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Windows 端 BM1684x 边缘设备一键自检脚本（Python 版）
功能与 run_check.bat 完全一致，使用 paramiko 实现 SSH/SFTP，原生支持密码认证。

依赖：
    - Python 3.7+
    - paramiko（pip install paramiko）

用法：
    python run_check.py
    python run_check.py --ip 192.168.112.47 --user linaro --password linaro
    python run_check.py --skip-install
"""

import argparse
import glob
import json
import os
import sys
import time
from pathlib import Path
from typing import List, Optional, Tuple

try:
    import paramiko
except ImportError:
    print("[ERROR] 未安装 paramiko，请执行: pip install paramiko")
    sys.exit(1)


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
    print(f"[INFO] {msg}")


def log_warn(msg: str) -> None:
    print(f"[WARN] {msg}")


def log_error(msg: str) -> None:
    print(f"[ERROR] {msg}", file=sys.stderr)


def find_deb_package() -> str:
    """在同目录下查找最新的 qifeng-scm_*.deb。"""
    candidates = glob.glob("qifeng-scm_*.deb")
    if not candidates:
        log_error("当前目录未找到 qifeng-scm_*.deb，请将 deb 包与脚本放在同一目录。")
        sys.exit(1)
    candidates.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    return candidates[0]


def create_ssh_client(ip: str, port: int, user: str, password: str) -> paramiko.SSHClient:
    """创建并返回 SSH 连接。"""
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        client.connect(hostname=ip, port=port, username=user, password=password,
                       timeout=10, allow_agent=False, look_for_keys=False)
    except paramiko.AuthenticationException:
        log_error(f"SSH 认证失败: {user}@{ip}，请检查用户名和密码。")
        sys.exit(1)
    except paramiko.SSHException as e:
        log_error(f"SSH 连接失败: {e}")
        sys.exit(1)
    except Exception as e:
        log_error(f"连接设备 {ip}:{port} 失败: {e}")
        sys.exit(1)
    return client


def remote_exec(client: paramiko.SSHClient, command: str,
                sudo_password: Optional[str] = None, timeout: int = 30) -> Tuple[int, str, str]:
    """在远端执行命令，返回 (exit_code, stdout, stderr)。"""
    if sudo_password:
        # 通过 sudo -S 从 stdin 传入密码
        command = f"echo '{sudo_password}' | sudo -S bash -c '{command}'"

    stdin, stdout, stderr = client.exec_command(command, timeout=timeout)
    exit_code = stdout.channel.recv_exit_status()
    out = stdout.read().decode("utf-8", errors="replace")
    err = stderr.read().decode("utf-8", errors="replace")
    return exit_code, out, err


def remote_exec_sudo(client: paramiko.SSHClient, command: str,
                     root_password: str, timeout: int = 30) -> Tuple[int, str, str]:
    """使用 sudo 执行远端命令。"""
    # 使用 -S 选项从 stdin 传入 root 密码
    full_cmd = f"sudo -S bash -c '{command}'"
    stdin, stdout, stderr = client.exec_command(full_cmd, timeout=timeout)
    stdin.write(f"{root_password}\n")
    stdin.flush()
    exit_code = stdout.channel.recv_exit_status()
    out = stdout.read().decode("utf-8", errors="replace")
    err = stderr.read().decode("utf-8", errors="replace")
    return exit_code, out, err


def sftp_upload(client: paramiko.SSHClient, local_path: str, remote_path: str) -> None:
    """通过 SFTP 上传本地文件到远端。"""
    sftp = client.open_sftp()
    try:
        sftp.put(local_path, remote_path)
    finally:
        sftp.close()


def sftp_download(client: paramiko.SSHClient, remote_path: str, local_path: str) -> bool:
    """通过 SFTP 下载远端文件到本地，成功返回 True。"""
    sftp = client.open_sftp()
    try:
        sftp.get(remote_path, local_path)
        return True
    except (IOError, OSError):
        return False
    finally:
        sftp.close()


def wait_for_service(client: paramiko.SSHClient, root_password: str) -> None:
    """等待 qf_scmd 服务进入 active 状态。"""
    log_info("等待 qf_scmd 服务就绪...")
    elapsed = 0
    while elapsed < SERVICE_READY_TIMEOUT_SEC:
        exit_code, out, _ = remote_exec_sudo(
            client, f"systemctl is-active {SERVICE_NAME}", root_password)
        if exit_code == 0 and "active" in out:
            log_info("qf_scmd 服务已就绪。")
            return
        time.sleep(SERVICE_READY_INTERVAL_SEC)
        elapsed += SERVICE_READY_INTERVAL_SEC
    log_error("等待 qf_scmd 服务超时。")
    pull_remote_log(client, root_password)
    sys.exit(1)


def pull_remote_log(client: paramiko.SSHClient, root_password: str, ip: str = "") -> None:
    """拉取远端 qf_scmd 日志到本地 logs/ 目录。"""
    log_dir = Path("logs")
    log_dir.mkdir(exist_ok=True)
    ip_suffix = f"-{ip}" if ip else ""
    local_log = log_dir / f"qf_scmd{ip_suffix}.log"
    try:
        exit_code, out, _ = remote_exec_sudo(
            client, f"journalctl -u {SERVICE_NAME} -n 50 --no-pager", root_password,
            timeout=15)
        with open(local_log, "w", encoding="utf-8") as f:
            f.write(out)
        log_info(f"远端日志已保存到 {local_log}")
    except Exception as e:
        log_warn(f"拉取远端日志失败: {e}")


def print_report_summary(report: dict) -> None:
    """解析并打印自检报告摘要。"""
    overall = report.get("overall", "UNKNOWN")
    summary = report.get("summary", "")
    checks = report.get("checks", {})

    # 整体结果
    if overall == "OK":
        log_info(f"自检结果: 通过 - {summary}")
    else:
        log_error(f"自检结果: 失败 - {summary}")

    # 逐项结果
    print()
    print(f"{'检测项':<20} {'状态':<10} {'耗时(ms)':<10} 说明")
    print("-" * 60)
    for name, detail in checks.items():
        status = detail.get("status", "UNKNOWN")
        elapsed = detail.get("elapsed_ms", 0)
        message = detail.get("message", "")
        print(f"{name:<20} {status:<10} {elapsed:<10} {message}")
    print()


def main() -> int:
    """主入口。"""
    parser = argparse.ArgumentParser(
        description="BM1684x 边缘设备硬件自检（Python + paramiko 版）")
    parser.add_argument("--ip", default=DEFAULT_IP,
                        help=f"设备 IP（默认 {DEFAULT_IP}）")
    parser.add_argument("--user", default=DEFAULT_USER,
                        help=f"SSH 用户名（默认 {DEFAULT_USER}）")
    parser.add_argument("--password",
                        default=os.environ.get("SSH_PASSWORD", DEFAULT_PASSWORD),
                        help="SSH 用户密码（默认 linaro，也可通过环境变量 SSH_PASSWORD 传入）")
    parser.add_argument("--root-password",
                        default=os.environ.get("ROOT_PASSWORD", DEFAULT_ROOT_PASSWORD),
                        help="root 密码（默认 linaro，也可通过环境变量 ROOT_PASSWORD 传入）")
    parser.add_argument("--port", type=int, default=DEFAULT_SSH_PORT,
                        help=f"SSH 端口（默认 {DEFAULT_SSH_PORT}）")
    parser.add_argument("--deb", default=None,
                        help="指定 deb 包路径，默认自动匹配同目录 qifeng-scm_*.deb")
    parser.add_argument("--skip-install", action="store_true",
                        help="跳过安装步骤，直接执行自检（设备已安装 SCM 时使用）")
    args = parser.parse_args()

    log_info(f"目标设备: {args.user}@{args.ip}")

    # 创建 SSH 连接
    log_info("连接设备...")
    client = create_ssh_client(args.ip, args.port, args.user, args.password)

    try:
        log_dir = Path("logs")
        log_dir.mkdir(exist_ok=True)

        if not args.skip_install:
            # 查找 deb 包
            deb_file = args.deb if args.deb else find_deb_package()
            log_info(f"使用 deb 包: {deb_file}")

            # 上传 deb 包
            log_info("上传 deb 包到设备...")
            sftp_upload(client, deb_file, REMOTE_DEB_PATH)

            # 安装 deb 包
            log_info("安装 qifeng-scm...")
            exit_code, out, err = remote_exec_sudo(
                client, f"dpkg -i {REMOTE_DEB_PATH} && apt-get install -f -y",
                args.root_password, timeout=120)
            if exit_code != 0:
                log_error("安装 qifeng-scm 失败。")
                log_error(f"stdout: {out}")
                log_error(f"stderr: {err}")
                pull_remote_log(client, args.root_password, args.ip)
                return 1

            # 等待服务就绪
            wait_for_service(client, args.root_password)
        else:
            log_info("跳过安装步骤（--skip-install）")

        # 确保远端配置目录存在
        remote_exec_sudo(client, f"mkdir -p {REMOTE_CONFIG_DIR}", args.root_password)
        remote_exec_sudo(client, f"mkdir -p {REMOTE_SCRIPTS_DIR}", args.root_password)

        # 上传 selftest.json
        log_info("上传 selftest.json...")
        local_config = Path("config/selftest.json")
        if not local_config.exists():
            log_error(f"本地配置文件不存在: {local_config}")
            return 1
        sftp_upload(client, str(local_config), "/tmp/selftest.json")
        remote_exec_sudo(
            client, f"mv /tmp/selftest.json {REMOTE_CONFIG_PATH} && chmod 644 {REMOTE_CONFIG_PATH}",
            args.root_password)

        # 上传 pcba 检测脚本
        log_info("上传 pcba 检测脚本...")
        local_script = Path("scripts/pcba_check.sh")
        if not local_script.exists():
            log_error(f"本地脚本文件不存在: {local_script}")
            return 1
        sftp_upload(client, str(local_script), "/tmp/pcba_check.sh")
        remote_exec_sudo(
            client, f"mv /tmp/pcba_check.sh {REMOTE_PCBA_SCRIPT} && chmod +x {REMOTE_PCBA_SCRIPT}",
            args.root_password)

        # 执行自检
        log_info("开始执行硬件自检...")
        print("=" * 50)
        # 不使用 sudo 执行 qf_scmc，普通用户即可运行
        exit_code, out, err = remote_exec(
            client, f"qf_scmc check --config {REMOTE_CONFIG_PATH}",
            timeout=600)
        # 打印远端输出
        if out:
            print(out)
        if err:
            print(err, file=sys.stderr)
        print("=" * 50)
        check_rc = exit_code

        # 拉回自检报告
        log_info("拉回自检报告...")
        local_report = log_dir / f"selftest-report-{args.ip}.json"

        # 报告文件可能需要 root 权限读取，先复制到 /tmp
        remote_exec_sudo(
            client, f"cp {REMOTE_REPORT_PATH} /tmp/selftest-report.json && chmod 644 /tmp/selftest-report.json",
            args.root_password)
        report_ok = sftp_download(client, "/tmp/selftest-report.json", str(local_report))

        if report_ok:
            log_info(f"自检报告已保存到 {local_report}")
            # 解析并打印报告摘要
            try:
                with open(local_report, "r", encoding="utf-8") as f:
                    report = json.load(f)
                print_report_summary(report)
            except (json.JSONDecodeError, IOError) as e:
                log_warn(f"解析自检报告失败: {e}")
        else:
            log_warn("拉回自检报告失败，可能自检未生成报告。")

        if check_rc != 0:
            log_error("自检执行失败或存在关键项未通过")
            pull_remote_log(client, args.root_password, args.ip)
            return 1

        log_info("自检完成。")
        return 0

    except Exception as e:
        log_error(f"执行过程中发生异常: {e}")
        pull_remote_log(client, args.root_password, args.ip)
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
