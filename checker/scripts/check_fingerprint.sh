#!/bin/bash
# =============================================================================
# check_fingerprint.sh — 指纹模组自检脚本示例
#
# 这是 ScriptChecker 适配器的示例脚本，展示输出协议：
#   退出码：0=pass  1=fail  2=skipped  3=warning
#   stdout 末行（可选）：JSON 对象 {"message":"...","details":{...}}
#
# 实际部署时，部门同事可替换为真实指纹模组检测逻辑。
# =============================================================================
set -euo pipefail

DEV="${FP_DEVICE:-/dev/ttyS3}"
BAUD="${FP_BAUD:-57600}"

# 1) 检查设备节点是否存在
if [ ! -e "$DEV" ]; then
  echo "fingerprint device not found: $DEV" >&2
  echo '{"message":"device not present","details":{"device":"'"$DEV"'"}}'
  exit 2  # skipped
fi

# 2) 尝试通过 stty 配置串口并发送握手包（示意）
#    实际协议按模组 datasheet 实现
if ! stty -F "$DEV" "$BAUD" raw -echo 2>/dev/null; then
  echo '{"message":"cannot configure serial port","details":{"device":"'"$DEV"'","baud":"'"$BAUD"'"}}'
  exit 3  # warning
fi

# 发送握手包并读取响应（示意，实际需按模组协议）
# 帧头 EF01 + 取模组特征命令
printf '\xef\x01\xff\xff\xff\xff\x01\x00\x03\x01\x00\x05' > "$DEV" 2>/dev/null || true
RESP=$(timeout 2 dd if="$DEV" bs=1 count=12 2>/dev/null | xxd -p || true)

if [ -n "$RESP" ]; then
  # 收到响应，认为模组在线
  echo "fingerprint module responded, resp=$RESP" >&2
  echo '{"message":"fingerprint module alive","details":{"resp":"'"$RESP"'"}}'
  exit 0  # pass
else
  echo "fingerprint module no response" >&2
  echo '{"message":"no response from module","details":{"device":"'"$DEV"'"}}'
  exit 1  # fail
fi
