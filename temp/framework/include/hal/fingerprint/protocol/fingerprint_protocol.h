/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_FINGERPRINT_PROTOCOL_FINGERPRINT_PROTOCOL_H
#define HAL_FINGERPRINT_PROTOCOL_FINGERPRINT_PROTOCOL_H

#include <cstdint>
#include <vector>

namespace qifeng {
    struct Request {
        uint16_t cmd = 0;
        std::vector<uint8_t> data;
    };

    struct Frame {
        uint16_t cmd = 0;
        uint32_t password = 0;
        uint32_t errorCode = 0;
        std::vector<uint8_t> data;
    };

    /**
     * @brief 指纹模组协议接口，定义编解码契约
     */
    class IFingerprintProtocol {
    public:
        IFingerprintProtocol() = default;
        virtual ~IFingerprintProtocol() = default;
        IFingerprintProtocol(const IFingerprintProtocol&) = delete;
        IFingerprintProtocol& operator=(const IFingerprintProtocol&) = delete;
        IFingerprintProtocol(IFingerprintProtocol&&) = delete;
        IFingerprintProtocol& operator=(IFingerprintProtocol&&) = delete;

        /**
         * @brief 将请求帧编码为 UART 字节流
         * @param frame 请求帧（命令码 + 数据）
         * @param out 输出字节流（含链路层帧头）
         * @return 编码是否成功
         */
        virtual bool Encode(const Request& req, std::vector<uint8_t>& out) = 0;

        /**
         * @brief 从缓冲区解码一帧应答
         * @param buffer 输入缓冲区，已消费的数据会被移除
         * @param out 输出应答帧
         * @return 是否成功解码一帧
         */
        virtual bool Decode(std::vector<uint8_t>& buffer, Frame& out) = 0;
    };

}  // namespace qifeng

#endif  // HAL_FINGERPRINT_PROTOCOL_FINGERPRINT_PROTOCOL_H
