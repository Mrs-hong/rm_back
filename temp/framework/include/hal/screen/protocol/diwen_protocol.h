/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_PROTOCOL_DIWEN_PROTOCOL_H
#define HAL_SCREEN_PROTOCOL_DIWEN_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hal/screen/protocol/screen_protocol.h"

namespace qifeng {
    struct DiwenProtocolConfig {};

    /**
     * @brief 迪文串口屏协议实现
     *
     * 帧格式: 帧头(0x5A 0xA5) + 长度(1B) + 命令(1B) + 地址(2B) + 数据
     */
    class DiwenProtocol : public IScreenProtocol {
    public:
        DiwenProtocol() = default;

        ~DiwenProtocol() override = default;

        DiwenProtocol(const DiwenProtocol&) = delete;
        DiwenProtocol& operator=(const DiwenProtocol&) = delete;
        DiwenProtocol(DiwenProtocol&&) = delete;
        DiwenProtocol& operator=(DiwenProtocol&&) = delete;

        bool Encode(const Request& request, std::vector<uint8_t>& out) override;
        bool Decode(std::vector<uint8_t>& buffer, Frame& out) override;

        /**
         * @brief 构建写命令帧（升级复用：数据帧/Flash 写入帧/重启帧均基于此）
         * @param addr 目标地址
         * @param data 数据指针
         * @param dataLen 数据字节数
         * @return 完整的写命令帧
         */
        static std::vector<uint8_t> BuildWriteFrame(uint16_t addr, const uint8_t* data, size_t dataLen);

        /**
         * @brief 构建读命令帧（升级专用：查询 Flash 完成状态）
         * @param addr 读取地址
         * @param readLen 读取长度
         * @return 完整的读命令帧
         */
        static std::vector<uint8_t> BuildReadFrame(uint16_t addr, uint8_t readLen);

    private:
        static size_t FindFrameHeader(const std::vector<uint8_t>& buffer);
        static std::string ConvertUtf8ToGbk(const std::string& utf8Str);

        std::vector<uint8_t> EncodeU16(uint16_t addr, uint16_t value);
        std::vector<uint8_t> EncodeU32(uint16_t addr, uint32_t value);
        std::vector<uint8_t> EncodeI32(uint16_t addr, int32_t value);
        std::vector<uint8_t> EncodeFloat(uint16_t addr, float value);
        std::vector<uint8_t> EncodeDouble(uint16_t addr, double value);
        std::vector<uint8_t> EncodeText(uint16_t addr, const std::string& str, uint16_t maxBytes);
        std::vector<uint8_t> EncodeMultiU16(uint16_t addr, const std::vector<uint16_t>& values);

        static constexpr uint8_t mCmdWrite = 0x82;
        static constexpr uint8_t mCmdRead = 0x83;
        static constexpr uint8_t mHeaderByte0 = 0x5A;
        static constexpr uint8_t mHeaderByte1 = 0xA5;
    };

}  // namespace qifeng

#endif  // HAL_SCREEN_PROTOCOL_DIWEN_PROTOCOL_H
