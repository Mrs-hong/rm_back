#!/bin/bash

echo "================================="
echo " 风扇与热状态极简验证"
echo "================================="

# 1. 获取最高温度
max_temp=0
max_type="未知"
for zone in /sys/class/thermal/thermal_zone*; do
    if [ -f "$zone/temp" ]; then
        t=$(cat "$zone/temp" 2>/dev/null)
        if [ "$t" -gt "$max_temp" ] 2>/dev/null; then
            max_temp=$t
            max_type=$(cat "$zone/type" 2>/dev/null)
        fi
    fi
done
temp_c=$(awk "BEGIN {printf \"%.1f\", $max_temp/1000}")

# 2. 获取挡位
cur_state=$(cat /sys/class/thermal/cooling_device0/cur_state 2>/dev/null || echo "N/A")
max_state=$(cat /sys/class/thermal/cooling_device0/max_state 2>/dev/null || echo "N/A")

# 3. 获取 PWM
pwm_val="N/A"
pwm_pct="N/A"
for hwmon in /sys/class/hwmon/hwmon*; do
    if [ -f "$hwmon/name" ] && [ "$(cat $hwmon/name 2>/dev/null)" = "pwmfan" ]; then
        if [ -f "$hwmon/pwm1" ]; then
            pwm_val=$(cat "$hwmon/pwm1" 2>/dev/null)
            pwm_pct=$(awk "BEGIN {printf \"%.1f\", ($pwm_val/255)*100}")
        fi
    fi
done

# 输出状态
echo " [温度] ${temp_c} °C (最高温区: ${max_type})"
echo " [挡位] 第 ${cur_state} 挡 / 最大 ${max_state} 挡"
echo " [PWM]  ${pwm_val} / 255 (占空比 ${pwm_pct}%)"
echo "---------------------------------"

# 给出结论
if [ "$cur_state" = "N/A" ] || [ "$pwm_val" = "N/A" ]; then
    echo " 结论: 失败，无法读取风扇或温度状态。"
elif [ "$cur_state" -eq 0 ] && [ "$pwm_val" -lt 100 ] 2>/dev/null; then
    echo " 结论: 正常，温度适宜，风扇处于低速/待机状态。"
elif [ "$cur_state" -gt 0 ] && [ "$pwm_val" -ge 100 ] 2>/dev/null; then
    echo " 结论: 正常，温度较高，风扇已加速散热。"
else
    echo " 结论: 正常，风扇受内核 thermal 框架自动接管中。"
fi
echo "================================="
