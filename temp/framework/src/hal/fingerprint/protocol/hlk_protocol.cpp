/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "common/logger.h"
#include "hal/fingerprint/protocol/hlk_protocol.h"

namespace qifeng {

    HlkProtocol::HlkProtocol(uint32_t password) : mPassword(password) {
    }

    bool HlkProtocol::Encode(const Request& req, std::vector<uint8_t>& out) {
        size_t appDataLen = AppHeaderSize + req.data.size() + ChecksumSize;

        out.clear();
        out.reserve(LinkHeaderSize + appDataLen);

        // 帧头
        out.insert(out.end(), FrameHeader.begin(), FrameHeader.end());

        // 应用数据长度
        out.push_back(static_cast<uint8_t>((appDataLen >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(appDataLen & 0xFF));

        // 帧头校验和
        uint8_t headerChecksum = CalcChecksum(out.data(), FrameHeaderSize + LenFieldSize);
        out.push_back(headerChecksum);

        size_t appStart = out.size();

        // 校验密码
        out.push_back(static_cast<uint8_t>((mPassword >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((mPassword >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((mPassword >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(mPassword & 0xFF));

        // 命令
        out.push_back(static_cast<uint8_t>((req.cmd >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(req.cmd & 0xFF));

        // 数据
        out.insert(out.end(), req.data.begin(), req.data.end());

        // 校验和
        uint8_t appChecksum = CalcChecksum(out.data() + appStart, out.size() - appStart);
        out.push_back(appChecksum);

        return true;
    }

    bool HlkProtocol::Decode(std::vector<uint8_t>& buffer, Frame& out) {
        // 查找帧头
        size_t headerPos = FindFrameHeader(buffer);
        if (headerPos == NotFound) {
            buffer.clear();
            return false;
        }
        if (headerPos > 0) {
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(headerPos));
        }
        if (buffer.size() < LinkHeaderSize) {
            return false;
        }
        
        // 校验帧头校验和
        if (!CheckHeader(buffer.data(), FrameHeaderSize + LenFieldSize)) {
            SLOG_WARN << "HlkProtocol: header checksum mismatch";
            buffer.erase(buffer.begin());
            return false;
        }

        // 应用数据
        uint16_t appDataLen = ParseU16(buffer.data() + FrameHeaderSize);
        size_t totalFrameLen = LinkHeaderSize + appDataLen;
        if (buffer.size() < totalFrameLen) {
            return false;
        }

        if (appDataLen < MinRecvPayloadSize) {
            SLOG_WARN << "HlkProtocol: app data too short";
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(totalFrameLen));
            return false;
        }

        const uint8_t* appData = buffer.data() + LinkHeaderSize;
        if (!CheckApp(appData, appDataLen)) {
            std::ostringstream raw;
            for (size_t i = 0; i < totalFrameLen; ++i) {
                raw << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                    << static_cast<int>(buffer[i]);
                if (i + 1 < totalFrameLen) {
                    raw << ' ';
                }
            }
            SLOG_WARN << "HlkProtocol: app checksum mismatch, appDataLen=" << appDataLen
                      << " expected=0x" << std::hex << static_cast<int>(CalcChecksum(appData, appDataLen - 1))
                      << " got=0x" << static_cast<int>(appData[appDataLen - 1])
                      << " raw=" << raw.str();
            // 跳过帧头首字节
            buffer.erase(buffer.begin());
            return false;
        }

        ParseAppData(appData, appDataLen, out);

        buffer.erase(buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(totalFrameLen));
        return true;
    }

    bool HlkProtocol::CheckHeader(const uint8_t* header, size_t headerLen) {
        uint8_t expected = CalcChecksum(header, headerLen);
        return header[headerLen] == expected;
    }

    bool HlkProtocol::CheckApp(const uint8_t* appData, uint16_t appDataLen) {
        uint8_t expected = CalcChecksum(appData, appDataLen - 1);
        return appData[appDataLen - 1] == expected;
    }

    void HlkProtocol::ParseAppData(const uint8_t* appData, uint16_t appDataLen, Frame& out) {
        out.password = (static_cast<uint32_t>(appData[0]) << 24) |
                       (static_cast<uint32_t>(appData[1]) << 16) |
                       (static_cast<uint32_t>(appData[2]) << 8) |
                       static_cast<uint32_t>(appData[3]);

        out.cmd = ParseU16(appData + 4);

        // 应用层数据: password(4B) + cmd(2B) + errorCode(4B) + 业务数据(NB) + checksum(1B)
        size_t payloadLen = appDataLen - AppHeaderSize - ChecksumSize;
        const uint8_t* payload = appData + AppHeaderSize;

        if (payloadLen >= ErrorCodeSize) {
            out.errorCode = (static_cast<uint32_t>(payload[0]) << 24) |
                            (static_cast<uint32_t>(payload[1]) << 16) |
                            (static_cast<uint32_t>(payload[2]) << 8) |
                            static_cast<uint32_t>(payload[3]);

            size_t bizLen = payloadLen - ErrorCodeSize;
            if (bizLen > 0) {
                out.data.assign(payload + ErrorCodeSize, payload + ErrorCodeSize + bizLen);
            } else {
                out.data.clear();
            }
        } else {
            out.errorCode = 0;
            out.data.clear();
        }
    }

    uint16_t HlkProtocol::ParseU16(const uint8_t* data) {
        return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) | static_cast<uint16_t>(data[1]));
    }

    uint8_t HlkProtocol::CalcChecksum(const uint8_t* data, size_t len) {
        uint8_t sum = 0;
        for (size_t i = 0; i < len; ++i) {
            sum += data[i];
        }
        return static_cast<uint8_t>(static_cast<uint8_t>(~sum) + 1U);
    }

    size_t HlkProtocol::FindFrameHeader(const std::vector<uint8_t>& buffer) {
        if (buffer.size() < FrameHeaderSize) {
            return NotFound;
        }

        for (size_t i = 0; i + FrameHeaderSize <= buffer.size(); ++i) {
            if (std::equal(FrameHeader.begin(), FrameHeader.end(), buffer.begin() + static_cast<ptrdiff_t>(i))) {
                return i;
            }
        }
        return NotFound;
    }

}  // namespace qifeng