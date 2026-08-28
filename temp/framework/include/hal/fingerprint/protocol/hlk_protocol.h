/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_FINGERPRINT_PROTOCOL_HLK_PROTOCOL_H
#define HAL_FINGERPRINT_PROTOCOL_HLK_PROTOCOL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "hal/fingerprint/protocol/fingerprint_protocol.h"

namespace qifeng {

    /**
     * @brief HLK-FPM383C 指纹模组协议实现
     *
     * 链路层: 帧头(8B) + 应用层数据长度(2B) + 帧头校验和(1B) + 应用层数据
     * 应用层(发送): 校验密码(4B) + 命令(2B) + 数据(NB) + 校验和(1B)
     * 应用层(响应): 校验密码(4B) + 命令(2B) + 错误码(4B) + 数据(NB) + 校验和(1B)
     */
    class HlkProtocol : public IFingerprintProtocol {
    public:
        HlkProtocol() = default;
        explicit HlkProtocol(uint32_t password);

        ~HlkProtocol() override = default;

        HlkProtocol(const HlkProtocol&) = delete;
        HlkProtocol& operator=(const HlkProtocol&) = delete;
        HlkProtocol(HlkProtocol&&) = delete;
        HlkProtocol& operator=(HlkProtocol&&) = delete;

        bool Encode(const Request& req, std::vector<uint8_t>& out) override;
        bool Decode(std::vector<uint8_t>& buffer, Frame& out) override;

    private:
        static uint8_t CalcChecksum(const uint8_t* data, size_t len);
        static size_t FindFrameHeader(const std::vector<uint8_t>& buffer);
        static uint16_t ParseU16(const uint8_t* data);
        static bool CheckHeader(const uint8_t* header, size_t headerLen);
        static bool CheckApp(const uint8_t* appData, uint16_t appDataLen);
        static void ParseAppData(const uint8_t* appData, uint16_t appDataLen, Frame& out);

    private:
        uint32_t mPassword = 0x00000000;

        static constexpr size_t NotFound = static_cast<size_t>(-1);

        // header
        static constexpr size_t FrameHeaderSize = 8;
        static constexpr size_t LenFieldSize = 2;
        static constexpr size_t LinkHeaderSize = FrameHeaderSize + LenFieldSize + 1;
        static constexpr std::array<uint8_t, FrameHeaderSize> FrameHeader = {
            0xF1, 0x1F, 0xE2, 0x2E, 0xB6, 0x6B, 0xA8, 0x8A};

        // app
        static constexpr size_t PasswordSize = 4;
        static constexpr size_t CmdSize = 2;
        static constexpr size_t ErrorCodeSize = 4;
        static constexpr size_t ChecksumSize = 1;
        static constexpr size_t AppHeaderSize = PasswordSize + CmdSize;
        static constexpr size_t MinRecvPayloadSize = AppHeaderSize + ErrorCodeSize + ChecksumSize;
    };

}  // namespace qifeng

#endif  // HAL_FINGERPRINT_PROTOCOL_HLK_PROTOCOL_H
