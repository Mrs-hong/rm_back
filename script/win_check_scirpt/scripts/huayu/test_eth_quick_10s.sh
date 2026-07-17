#!/bin/bash

# 产线快速网口测试
# 每个方向 10 秒，阈值 800 Mbits/sec，TCP Retr 允许 0

exec /etc/testscript/eth_perf_common.sh QUICK 10 4 800 1 eth0 eth1 192.168.112.47 192.168.112.48 24 0
