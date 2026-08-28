/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_PROTOCOL_SCREEN_PROTOCOL_H
#define HAL_SCREEN_PROTOCOL_SCREEN_PROTOCOL_H

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace qifeng {
    /**
     * @brief 文本值：UTF-8 输入，内部转 GBK，截断到 maxBytes 后追加 0xFFFF 结束符
     *
     * 屏幕固件遇 0xFFFF 即停止渲染且不纳入 VP 地址计数，故内容+FFFF 不会溢出相邻字段。
     * 缩短文本时 FFFF 之后的残留字节不会被显示，无需固定长度填充覆盖。
     */
    struct Text {
        std::string text;
        uint16_t maxBytes = 0;
        bool operator==(const Text& o) const {
            return text == o.text && maxBytes == o.maxBytes;
        }
        bool operator!=(const Text& o) const {
            return !operator==(o);
        }
    };

    /**
     * @brief 寄存器值类型，覆盖屏幕协议支持的所有数据类型
     */
    using Value = std::variant<uint16_t, uint32_t, int32_t, float, double, Text, std::vector<uint16_t>>;

    struct Request {
        uint16_t address = 0;
        Value value;

        Request() = default;
        Request(uint16_t addr, Value val) : address(addr), value(std::move(val)) {
        }
    };

    struct Frame {
        uint8_t cmd = 0;
        uint16_t addr = 0;
        std::vector<uint8_t> data;
    };

    /**
     * @brief 屏幕协议接口，定义编解码契约
     */
    class IScreenProtocol {
    public:
        IScreenProtocol() = default;
        virtual ~IScreenProtocol() = default;
        IScreenProtocol(const IScreenProtocol&) = delete;
        IScreenProtocol& operator=(const IScreenProtocol&) = delete;
        IScreenProtocol(IScreenProtocol&&) = delete;
        IScreenProtocol& operator=(IScreenProtocol&&) = delete;

        /**
         * @brief 将请求编码为二进制帧
         * @param request 请求结构
         * @param out 输出帧数据
         * @return 编码是否成功
         */
        virtual bool Encode(const Request& request, std::vector<uint8_t>& out) = 0;

        /**
         * @brief 从缓冲区解码一帧数据
         * @param buffer 输入缓冲区，已消费的数据会被移除
         * @param out 输出帧结构
         * @return 是否成功解码一帧
         */
        virtual bool Decode(std::vector<uint8_t>& buffer, Frame& out) = 0;
    };

}  // namespace qifeng

#endif  // HAL_SCREEN_PROTOCOL_SCREEN_PROTOCOL_H
