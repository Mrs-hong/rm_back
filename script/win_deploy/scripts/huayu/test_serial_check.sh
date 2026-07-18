#!/bin/bash
# test_serial_check.sh

TFT_PORT="/dev/ttyS2"
TFT_BAUDRATE="115200"
TFT_RESP_FILE="/tmp/ttyS2_resp.bin"
TFT_EXPECTED_HEX="5aa503824f4b"
TFT_CMD_BYTES='\x5A\xA5\x07\x82\x00\x84\x5A\x01\x00\x01'

FINGER_PORT="/dev/ttyACM1"
FINGER_BAUDRATE="57600"
FINGER_RESP_FILE="/tmp/ttyACM1_resp.bin"
FINGER_EXPECTED_HEX="f11fe22eb66ba88a000b8200000000030300000000fa"
FINGER_CMD_BYTES='\xF1\x1F\xE2\x2E\xB6\x6B\xA8\x8A\x00\x07\x86\x00\x00\x00\x00\x03\x03\xFA'

MCU_PORT="/dev/ttyS1"
MCU_BAUDRATE="115200"
MCU_CMD_BYTES='\xFE\x14\x15\x00\x00\x01\xFE'

test_fixed_response()
{
    local PORT="$1"
    local BAUDRATE="$2"
    local RESP_FILE="$3"
    local READ_COUNT="$4"
    local EXPECTED_HEX="$5"
    local CMD_BYTES="$6"
    local NAME="$7"

    local RX_PID
    local RECV_HEX=""

    rm -f "$RESP_FILE"

    if [ ! -e "$PORT" ]; then
        printf "%-10s: failed\n" "$NAME"
        return 1
    fi

    if ! stty -F "$PORT" "$BAUDRATE" cs8 -parenb -cstopb \
        -ixon -ixoff -crtscts raw -echo 2>/dev/null; then
        printf "%-10s: failed\n" "$NAME"
        return 1
    fi

    # 清理串口中可能残留的旧数据
    timeout 0.2 dd if="$PORT" of=/dev/null bs=1 2>/dev/null

    # 先启动接收
    timeout 3 dd if="$PORT" of="$RESP_FILE" \
        bs=1 count="$READ_COUNT" 2>/dev/null &
    RX_PID=$!

    sleep 0.2

    # 发送检测命令
    if ! printf "%b" "$CMD_BYTES" > "$PORT"; then
        kill "$RX_PID" 2>/dev/null
        wait "$RX_PID" 2>/dev/null
        printf "%-10s: failed\n" "$NAME"
        return 1
    fi

    wait "$RX_PID" 2>/dev/null

    if [ -f "$RESP_FILE" ]; then
        RECV_HEX=$(xxd -p "$RESP_FILE" 2>/dev/null | tr -d '\n\r ')
    fi

    if [ "$RECV_HEX" = "$EXPECTED_HEX" ]; then
        printf "%-10s: OK\n" "$NAME"
        return 0
    fi

    printf "%-10s: failed\n" "$NAME"
    return 1
}

parse_mcu_version()
{
    local HEX="$1"
    local i
    local HEAD
    local CMD
    local LEN_HEX
    local LEN
    local TOTAL_HEX_LEN
    local FRAME
    local PAYLOAD_HEX
    local VERSION

    for ((i=0; i<=${#HEX}-14; i+=2)); do
        HEAD="${HEX:$i:2}"
        CMD="${HEX:$((i+2)):4}"

        [ "$HEAD" = "fe" ] || continue
        [ "$CMD" = "1416" ] || continue

        LEN_HEX="${HEX:$((i+6)):4}"

        [[ "$LEN_HEX" =~ ^[0-9a-fA-F]{4}$ ]] || continue

        LEN=$((16#$LEN_HEX))

        TOTAL_HEX_LEN=$(((1 + 2 + 2 + LEN + 1 + 1) * 2))
        FRAME="${HEX:$i:$TOTAL_HEX_LEN}"

        [ "${#FRAME}" -eq "$TOTAL_HEX_LEN" ] || continue
        [ "${FRAME: -2}" = "fe" ] || continue

        PAYLOAD_HEX="${FRAME:10:$((LEN * 2))}"
        VERSION=$(printf "%s" "$PAYLOAD_HEX" | xxd -r -p 2>/dev/null)

        [ -n "$VERSION" ] || continue

        printf "%s" "$VERSION"
        return 0
    done

    return 1
}

test_mcu()
{
    local HEX
    local VERSION

    if [ ! -e "$MCU_PORT" ]; then
        printf "%-10s: failed\n" "ttyS1"
        return 1
    fi

    if ! stty -F "$MCU_PORT" "$MCU_BAUDRATE" cs8 -parenb -cstopb \
        -ixon -ixoff -crtscts raw -echo 2>/dev/null; then
        printf "%-10s: failed\n" "ttyS1"
        return 1
    fi

    # 清理旧数据
    timeout 0.2 dd if="$MCU_PORT" of=/dev/null bs=1 2>/dev/null

    HEX=$(
        (
            sleep 0.1
            printf "%b" "$MCU_CMD_BYTES" > "$MCU_PORT"
        ) &

        timeout 2 dd if="$MCU_PORT" bs=1 count=128 2>/dev/null |
            xxd -p -c 256 |
            tr -d '\n\r ' |
            tr 'A-F' 'a-f'
    )

    VERSION=$(parse_mcu_version "$HEX")

    if [ -n "$VERSION" ]; then
        printf "%-10s: OK\n" "ttyS1"
        return 0
    fi

    printf "%-10s: failed\n" "ttyS1"
    return 1
}

test_mcu
MCU_RESULT=$?

test_fixed_response \
    "$TFT_PORT" \
    "$TFT_BAUDRATE" \
    "$TFT_RESP_FILE" \
    6 \
    "$TFT_EXPECTED_HEX" \
    "$TFT_CMD_BYTES" \
    "ttyS2"
TFT_RESULT=$?

test_fixed_response \
    "$FINGER_PORT" \
    "$FINGER_BAUDRATE" \
    "$FINGER_RESP_FILE" \
    22 \
    "$FINGER_EXPECTED_HEX" \
    "$FINGER_CMD_BYTES" \
    "ttyACM1"
FINGER_RESULT=$?

if [ "$MCU_RESULT" -eq 0 ] &&
   [ "$TFT_RESULT" -eq 0 ] &&
   [ "$FINGER_RESULT" -eq 0 ]; then
    exit 0
else
    exit 1
fi