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
        /**
         * @brief 本帧发送后到下一帧发送前的强制间隔(毫秒)，0 表示无需间隔
         *
         * 用于屏端要求节流的控制类指令：0xB0 触控开关连续下发时要求每条间隔
         * 20ms（【开发指南】5.2 节 / 产品协议"连续下发多条时每条间隔 20ms"），
         * 否则屏端 OS 核可能来不及逐条处理而漏指令。
         * 仅同一批次内的帧间生效（由引擎在发送循环中执行）。
         */
        uint16_t gapMs = 20;

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
     * @brief 迪文协议编解码配置
     *
     * 放在协议接口头而非具体协议实现头：上层（ScreenEngine 的 DisplayConfig）
     * 需要按值持有协议配置，避免公共接口反向依赖具体协议实现。
     * 当前迪文 DGUS II 帧格式无运行期可调项，保留占位以隔离实现。
     */
    struct DiwenProtocolConfig {};

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
