/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <json/json.h>
#include <optional>
#include <string>
#include <vector>

#include "common/utils/json_utils.h"
#include "ipc/scm_command.h"

namespace qifeng::scm {

    /**
     * @brief 请求结构体到 ScmCommand 枚举的映射 trait
     * @details 各 handler .h 中对此 trait 进行特化，将请求结构体类型映射到对应的 ScmCommand。
     *          新增命令时只需在 handler .h 中添加一个特化，无需维护 if-constexpr 链。
     *          遗漏特化会在编译期报错（Incomplete type），安全可靠。
     */
    template <typename T> struct RequestCommand;

    /**
     * @brief scmctl 通用请求信封
     * @details 消除 std::variant，改为 {command, params} 通用信封。
     *          各 handler 通过自己的 FromJson(request.params) 解析参数。
     *          客户端通过 MakeRequest(XxxRequest{...}) 构造请求。
     */
    struct ScmRequest {
        ScmCommand command;    // 命令类型
        Json::Value params;    // 命令参数（由各 handler 自行解析）

        /**
         * @brief 将请求序列化为 Json::Value
         * @return JSON 对象，格式：{command: int, params: object}
         */
        Json::Value ToJson() const;

        /**
         * @brief 从 Json::Value 反序列化为请求
         * @param root JSON 对象
         * @return 成功返回 ScmRequest，失败返回 std::nullopt
         */
        static std::optional<ScmRequest> FromJson(const Json::Value& root);
    };

    /**
     * @brief scmd 响应结构体
     * @details 定义 scmd 返回给 scmctl 的响应数据。
     *          使用 json_utils.h 宏实现 JSON 序列化/反序列化。
     */
    struct ScmResponse {
        int code {0};         // 0:成功, -1:失败, 1:警告
        std::string message;  // 结果消息
        Json::Value data;     // 灵活的数据载荷（服务列表、运行时信息等）

        BEGIN_JSON_PARSER
        ADD_MEMBER(code)
        ADD_MEMBER(message)
        ADD_MEMBER(data)
        END_JSON_PARSER

        /**
         * @brief const版本的JSON序列化
         * @details 宏生成的 toJson() 是非const方法，此处通过 const_cast 提供 const 版本
         */
        Json::Value ToJsonConst() const {
            ScmResponse& mutableThis = const_cast<ScmResponse&>(*this);  // NOLINT(readability-const-cast)
            return mutableThis.toJson();
        }
    };

    /**
     * @brief 客户端构造请求的模板辅助函数
     * @tparam T 请求结构体类型（需已特化 RequestCommand<T> 并声明 ToJson(const T&)）
     * @param req 请求结构体实例
     * @return 构造好的 ScmRequest 信封
     * @details 各 handler .h 中声明 Json::Value ToJson(const XxxRequest&)，
     *          通过参数依赖查找（ADL）在此模板中找到对应重载。
     */
    template <typename T>
    ScmRequest MakeRequest(const T& req) {
        ScmRequest request;
        request.command = RequestCommand<T>::value;
        request.params = ToJson(req);
        return request;
    }

    /**
     * @brief 通信协议类
     * @details 用于解析 scmctl 和 scmd 之间传输数据流协议。
     *          - 定义数据流的格式：JSON + 结束符（\n）
     *          - 用于 scmctl 和 scmd 之间进程通信、且命令行交互场景下，
     *            所以使用 JSON + 结束符（\n）作为数据流的分隔符
     */
    class ControlProtocol {
    public:
        ControlProtocol() = default;
        ~ControlProtocol() = default;

        ControlProtocol(const ControlProtocol&) = delete;
        ControlProtocol& operator=(const ControlProtocol&) = delete;
        ControlProtocol(ControlProtocol&&) = delete;
        ControlProtocol& operator=(ControlProtocol&&) = delete;

        /**
         * @brief 编码请求为JSON字符串 + '\n'
         * @param request 请求结构体
         * @return 编码后的字符串（含\n结束符）
         */
        static std::string EncodeRequest(const ScmRequest& request);

        /**
         * @brief 编码响应为JSON字符串 + '\n'
         * @param response 响应结构体
         * @return 编码后的字符串（含\n结束符）
         */
        static std::string EncodeResponse(const ScmResponse& response);

        /**
         * @brief 解码请求：从JSON字符串解析为ScmRequest
         * @param jsonStr JSON字符串（不含\n）
         * @param request 输出参数，解析后的请求
         * @return 是否解析成功
         */
        static bool DecodeRequest(const std::string& jsonStr, ScmRequest& request);

        /**
         * @brief 解码响应：从JSON字符串解析为ScmResponse
         * @param jsonStr JSON字符串（不含\n）
         * @param response 输出参数，解析后的响应
         * @return 是否解析成功
         */
        static bool DecodeResponse(const std::string& jsonStr, ScmResponse& response);

        /**
         * @brief 从缓冲区中提取完整的消息（以'\n'分隔）
         * @details 处理流式数据中的粘包/拆包问题
         * @param buffer 数据缓冲区，调用后保留未完成的数据
         * @return 提取出的完整消息列表（每条消息不含\n）
         */
        static std::vector<std::string> ExtractMessages(std::string& buffer);
    };

}  // namespace qifeng::scm
