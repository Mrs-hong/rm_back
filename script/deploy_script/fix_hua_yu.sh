#!/bin/bash
# 1. 创建解除死锁的终极版 service 文件
sudo tee /etc/systemd/system/fix-mysql-perms.service << 'EOF'
[Unit]
Description=Fix MySQL permissions, Mount Data Disk and Start Business Services
RequiresMountsFor=/data
# 在系统启动晚期执行
After=multi-user.target systemd-user-sessions.service network.target
# 【关键修改 1】：移除了 Before=mysql.service，打破依赖循环死锁！

[Service]
Type=oneshot
Environment=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# 【步骤 1】等待 3 秒，修复权限
ExecStart=/bin/sh -c 'sleep 3; if [ -d /data/mysql ]; then /bin/chown -R mysql:mysql /data/mysql; fi'

# 【步骤 2】异步重启 MySQL 服务 (使用 --no-block 避免死锁等待)
# 【关键修改 2】：--no-block 让 systemctl 提交任务后立即返回，不阻塞当前脚本
ExecStart=-/bin/systemctl --no-block restart mysql

# 【步骤 3】停止旧的业务进程
ExecStart=-/bin/sh -c 'qf_scmc stop -n qifeng_ca'

# 【步骤 4】检查并挂载外接磁盘 /dev/sda1 到 /data2
ExecStart=-/bin/sh -c 'mkdir -p /data2; for i in 1 2 3 4 5; do if [ -b /dev/sda1 ]; then break; fi; sleep 1; done; if [ -b /dev/sda1 ]; then if ! mountpoint -q /data2; then /bin/mount /dev/sda1 /data2 || echo "Warning: Failed to mount /dev/sda1"; fi; else echo "Warning: /dev/sda1 not found, skipping mount."; fi'

# 【步骤 5】启动新的业务进程
ExecStart=/bin/sh -c 'qf_scmc start -n qifeng_ca'

RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

# 2. 重新加载 systemd 配置
sudo systemctl daemon-reload

# 3. 重新启用服务
sudo systemctl enable fix-mysql-perms.service
