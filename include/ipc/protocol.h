/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>
#include <vector>

#include "ipc/data_def.h"

namespace qifeng::scm {

    /**
     * @brief 通信协议类
     * 用于解析scmctl和scmd之间传输数据流协议
     * - 定义数据流的格式：JSON + 结束符（\n）
     * - 用于scmctl和scmd之间进程通信、且命令行交互场景下，
     *   所以使用JSON+结束符（\n）作为数据流的分隔符
     * - json库是在third_party目录qifeng-framwork中json_utils.h和下面的jsoncpp库
     */
    class ControlProtocol {
    public:
        ControlProtocol() = default;
        ~ControlProtocol() = default;

        ControlProtocol(const ControlProtocol &) = delete;
        ControlProtocol &operator=(const ControlProtocol &) = delete;
        ControlProtocol(ControlProtocol &&) = delete;
        ControlProtocol &operator=(ControlProtocol &&) = delete;

        /**
         * @brief 编码请求为JSON字符串 + '\n'
         * @param request 请求结构体
         * @return 编码后的字符串（含\n结束符）
         */
        static std::string EncodeRequest(const ScmRequest &request);

        /**
         * @brief 编码响应为JSON字符串 + '\n'
         * @param response 响应结构体
         * @return 编码后的字符串（含\n结束符）
         */
        static std::string EncodeResponse(const ScmResponse &response);

        /**
         * @brief 解码请求：从JSON字符串解析为ScmRequest
         * @param jsonStr JSON字符串（不含\n）
         * @param request 输出参数，解析后的请求
         * @return 是否解析成功
         */
        static bool DecodeRequest(const std::string &jsonStr, ScmRequest &request);

        /**
         * @brief 解码响应：从JSON字符串解析为ScmResponse
         * @param jsonStr JSON字符串（不含\n）
         * @param response 输出参数，解析后的响应
         * @return 是否解析成功
         */
        static bool DecodeResponse(const std::string &jsonStr, ScmResponse &response);

        /**
         * @brief 从缓冲区中提取完整的消息（以'\n'分隔）
         * 处理流式数据中的粘包/拆包问题
         * @param buffer 数据缓冲区，调用后保留未完成的数据
         * @return 提取出的完整消息列表（每条消息不含\n）
         */
        static std::vector<std::string> ExtractMessages(std::string &buffer);
    };

}  // namespace qifeng::scm
