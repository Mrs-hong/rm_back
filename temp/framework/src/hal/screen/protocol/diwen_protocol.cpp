/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <cerrno>
#include <cstring>
#include <iconv.h>
#include <string>

#include "common/logger.h"
#include "hal/screen/protocol/addr_map.h"
#include "hal/screen/protocol/diwen_protocol.h"

namespace qifeng {
    bool DiwenProtocol::Encode(const Request& request, std::vector<uint8_t>& out) {
        auto encoded = std::visit(
            [this, addr = request.address](const auto& value) -> std::vector<uint8_t> {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, uint16_t>) {
                    return EncodeU16(addr, value);
                } else if constexpr (std::is_same_v<T, uint32_t>) {
                    return EncodeU32(addr, value);
                } else if constexpr (std::is_same_v<T, int32_t>) {
                    return EncodeI32(addr, value);
                } else if constexpr (std::is_same_v<T, float>) {
                    return EncodeFloat(addr, value);
                } else if constexpr (std::is_same_v<T, double>) {
                    return EncodeDouble(addr, value);
                } else if constexpr (std::is_same_v<T, Text>) {
                    return EncodeText(addr, value.text, value.maxBytes);
                } else if constexpr (std::is_same_v<T, std::vector<uint16_t>>) {
                    return EncodeMultiU16(addr, value);
                } else {
                    SLOG_ERROR << "Unsupported Value type";
                    return {};
                }
            },
            request.value);

        if (encoded.empty()) {
            return false;
        }

        out = std::move(encoded);
        return true;
    }

    bool DiwenProtocol::Decode(std::vector<uint8_t>& buffer, Frame& out) {
        if (buffer.size() < 3) {
            return false;
        }

        // 定位帧头，丢弃前导无效字节
        size_t headerPos = FindFrameHeader(buffer);
        if (headerPos == std::string::npos) {
            buffer.clear();
            return false;
        }

        if (headerPos > 0) {
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(headerPos));
        }

        if (buffer.size() < 3) {
            return false;
        }

        // 长度字段 = 命令(1B) + 地址(2B) + 数据，不含帧头和长度字段本身
        uint8_t length = buffer[2];
        size_t frameLen = 2 + 1 + length;

        if (buffer.size() < frameLen) {
            return false;
        }

        out.cmd = buffer[3];
        out.addr = static_cast<uint16_t>((static_cast<uint16_t>(buffer[4]) << 8) | buffer[5]);
        out.data.assign(buffer.begin() + 6, buffer.begin() + static_cast<ptrdiff_t>(frameLen));

        buffer.erase(buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(frameLen));

        return true;
    }

    std::vector<uint8_t> DiwenProtocol::EncodeU16(uint16_t addr, uint16_t value) {
        uint8_t buf[2];
        buf[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
        buf[1] = static_cast<uint8_t>(value & 0xFF);
        return BuildWriteFrame(addr, buf, 2);
    }

    std::vector<uint8_t> DiwenProtocol::EncodeU32(uint16_t addr, uint32_t value) {
        uint8_t buf[4];
        buf[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
        buf[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
        buf[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
        buf[3] = static_cast<uint8_t>(value & 0xFF);
        return BuildWriteFrame(addr, buf, 4);
    }

    std::vector<uint8_t> DiwenProtocol::EncodeI32(uint16_t addr, int32_t value) {
        uint32_t uval = static_cast<uint32_t>(value);
        uint8_t buf[4];
        buf[0] = static_cast<uint8_t>((uval >> 24) & 0xFF);
        buf[1] = static_cast<uint8_t>((uval >> 16) & 0xFF);
        buf[2] = static_cast<uint8_t>((uval >> 8) & 0xFF);
        buf[3] = static_cast<uint8_t>(uval & 0xFF);
        return BuildWriteFrame(addr, buf, 4);
    }

    std::vector<uint8_t> DiwenProtocol::EncodeFloat(uint16_t addr, float value) {
        uint8_t buf[4];
        std::memcpy(buf, &value, 4);
        return BuildWriteFrame(addr, buf, 4);
    }

    std::vector<uint8_t> DiwenProtocol::EncodeDouble(uint16_t addr, double value) {
        uint8_t buf[8];
        std::memcpy(buf, &value, 8);
        return BuildWriteFrame(addr, buf, 8);
    }

    std::vector<uint8_t> DiwenProtocol::EncodeText(uint16_t addr, const std::string& str, uint16_t maxBytes) {
        // maxBytes 是该字段在屏幕工程里的 VP 槽位大小，相邻字段地址紧密排布
        std::string gbk = ConvertUtf8ToGbk(str);
        if (gbk.size() > maxBytes) {
            gbk.resize(maxBytes);
        }
        std::vector<uint8_t> data(gbk.begin(), gbk.end());
        if (data.size() + 2 <= maxBytes) {
            data.push_back(0xFF);
            data.push_back(0xFF);
        } else {
            data.resize(maxBytes, 0x00);
        }
        return BuildWriteFrame(addr, data.data(), data.size());
    }

    std::vector<uint8_t> DiwenProtocol::EncodeMultiU16(uint16_t addr, const std::vector<uint16_t>& values) {
        std::vector<uint8_t> buf(values.size() * 2);
        for (size_t i = 0; i < values.size(); ++i) {
            buf[i * 2] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
            buf[i * 2 + 1] = static_cast<uint8_t>(values[i] & 0xFF);
        }
        return BuildWriteFrame(addr, buf.data(), buf.size());
    }

// GCC 11 对 vector 操作存在 -Wnull-dereference 误报 (Bug 106436)
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

    std::vector<uint8_t> DiwenProtocol::BuildWriteFrame(uint16_t addr, const uint8_t* data, size_t dataLen) {
        // 帧结构: 帧头(2B) + 长度(1B) + 命令(1B) + 地址(2B) + 数据(NB)
        size_t payloadLen = 2 + dataLen;
        size_t frameLen = 2 + 1 + 1 + payloadLen;

        std::vector<uint8_t> frame(frameLen);
        frame[0] = mHeaderByte0;
        frame[1] = mHeaderByte1;
        frame[2] = static_cast<uint8_t>(1 + payloadLen);
        frame[3] = mCmdWrite;
        frame[4] = static_cast<uint8_t>((addr >> 8) & 0xFF);
        frame[5] = static_cast<uint8_t>(addr & 0xFF);
        std::memcpy(frame.data() + 6, data, dataLen);

        return frame;
    }

#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic pop
#endif

    std::vector<uint8_t> DiwenProtocol::BuildReadFrame(uint16_t addr, uint8_t readLen) {
        // 读命令帧: 帧头 + 长度(04) + 0x83 + 地址(2B) + 读长度(1B)
        return {mHeaderByte0,
                mHeaderByte1,
                0x04,
                mCmdRead,
                static_cast<uint8_t>((addr >> 8) & 0xFF),
                static_cast<uint8_t>(addr & 0xFF),
                readLen};
    }

    size_t DiwenProtocol::FindFrameHeader(const std::vector<uint8_t>& buffer) {
        for (size_t i = 0; i + 1 < buffer.size(); ++i) {
            if (buffer[i] == mHeaderByte0 && buffer[i + 1] == mHeaderByte1) {
                return i;
            }
        }
        return std::string::npos;
    }

    std::string DiwenProtocol::ConvertUtf8ToGbk(const std::string& utf8Str) {
        if (utf8Str.empty()) {
            return {};
        }

        iconv_t cd = iconv_open("GBK", "UTF-8");
        if (cd == reinterpret_cast<iconv_t>(-1)) {
            SLOG_ERROR << "iconv_open failed: " << strerror(errno);
            return utf8Str;
        }

        auto inBuf = const_cast<char*>(utf8Str.data());
        size_t inLeft = utf8Str.size();
        // GBK最多每字符2字节，预分配足够空间
        size_t outSize = utf8Str.size() * 2;
        std::string result(outSize, '\0');
        char* outBuf = result.data();
        size_t outLeft = outSize;

        size_t ret = iconv(cd, &inBuf, &inLeft, &outBuf, &outLeft);
        iconv_close(cd);

        if (ret == static_cast<size_t>(-1)) {
            SLOG_WARN << "iconv conversion partial failure, some chars may be dropped";
        }

        result.resize(outSize - outLeft);
        return result;
    }

}  // namespace qifeng
