#!/bin/bash
# 极简 USB 存储可用性验证脚本 (以"能否使用"为标准)

# 颜色定义：检测 stdout 是否为 TTY，非 TTY 时禁用颜色码避免 ANSI 序列污染管道输出
if [ -t 1 ]; then
    G='\e[32m'; R='\e[31m'; Y='\e[33m'; C='\e[36m'; N='\e[0m'
else
    G=''; R=''; Y=''; C=''; N=''
fi

echo "================================="
echo " USB 存储可用性验证 (挂载+读写)"
echo "================================="

# 获取所有 USB 磁盘名称 (如 sda, sdb)
USB_DISKS=$(lsblk -d -n -o NAME,TRAN 2>/dev/null | awk '$2=="usb" {print $1}')

if [ -z "$USB_DISKS" ]; then
    echo -e " [USB存储] ${Y}无设备${N} (未插入 U盘/移动硬盘)"
    echo "================================="
    exit 0
fi

TOTAL=0
USABLE=0

for dev in $USB_DISKS; do
    TOTAL=$((TOTAL + 1))
    
    # 获取整盘的大小和型号
    INFO=$(lsblk -d -n -o SIZE,MODEL /dev/$dev 2>/dev/null)
    SIZE=$(echo "$INFO" | awk '{print $1}')
    MODEL=$(echo "$INFO" | awk '{$1=""; print substr($0,2)}' | sed 's/^[[:space:]]*//')
    
    # 获取该设备及其所有分区的挂载点 (过滤掉空行)
    MOUNTS=$(lsblk -n -o MOUNTPOINT /dev/$dev 2>/dev/null | grep -v '^$')
    
    if [ -z "$MOUNTS" ]; then
        echo -e " [${R}不可用${N}] /dev/$dev ($SIZE $MODEL)"
        echo -e "           ↳ ${Y}原因: 未挂载 (需手动 mount)${N}"
        continue
    fi
    
    # 遍历该磁盘上的所有挂载点
    while read -r mnt; do
        # 检查挂载选项是 rw (读写) 还是 ro (只读)
        MNT_OPTS=$(findmnt -n -o OPTIONS "$mnt" 2>/dev/null)
        
        if [[ "$MNT_OPTS" == *"ro"* ]] && [[ "$MNT_OPTS" != *"rw"* ]]; then
            echo -e " [${Y}受限${N} ] /dev/$dev 挂载于 $mnt"
            echo -e "           ↳ ${Y}原因: 只读模式 (可能是文件系统损坏或写保护)${N}"
            continue
        fi
        
        # 终极验证：实际尝试写入并删除一个隐藏测试文件
        TEST_FILE="$mnt/.usb_write_test_$$"
        if touch "$TEST_FILE" 2>/dev/null; then
            rm -f "$TEST_FILE"
            echo -e " [${G}可读写${N}] /dev/$dev 挂载于 ${C}$mnt${N} ($SIZE $MODEL)"
            USABLE=$((USABLE + 1))
        else
            echo -e " [${R}不可写${N}] /dev/$dev 挂载于 $mnt"
            echo -e "           ↳ ${R}原因: 权限不足 (当前用户无写入权限)${N}"
        fi
    done <<< "$MOUNTS"
done

echo "---------------------------------"
if [ "$USABLE" -gt 0 ]; then
    echo -e " 结论: ${G}成功${N}，共有 ${USABLE} 个挂载点可正常读写使用。"
else
    echo -e " 结论: ${R}失败${N}，没有可直接使用的 USB 存储空间。"
fi
echo "================================="
