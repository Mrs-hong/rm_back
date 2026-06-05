/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmctl/scmctl_client.h"

#include "ipc/protocol.h"
#include "ipc/uds.h"

#include <vector>

namespace qifeng::scm {

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ScmResponse ScmCtlClient::SendRequest(const ScmRequest &request, const std::string &socketPath) {
        ScmResponse response;
        response.code = -1;
        response.message = "Unknown error";

        // 创建UDS客户端
        UdsWrapper udsClient(UdsMode::CLIENT, socketPath);
        if (!udsClient.Initialize()) {
            response.message = "Failed to initialize UDS client. Is scmd running?";
            return response;
        }

        // 连接到scmd服务端
        if (!udsClient.Connect()) {
            response.message = "Failed to connect to scmd. Is scmd running at " + socketPath + "?";
            return response;
        }

        // 编码并发送请求
        std::string encoded = ControlProtocol::EncodeRequest(request);
        ssize_t sent = udsClient.Send(udsClient.GetSocketFd(), encoded);
        if (sent < 0) {
            response.message = "Failed to send request to scmd";
            return response;
        }

        // 接收响应数据
        std::string buffer;
        constexpr size_t bufSize = 4096;
        std::vector<char> recvBuf(bufSize);

        while (true) {
            ssize_t bytesRead = udsClient.Receive(udsClient.GetSocketFd(), recvBuf.data(), bufSize);
            if (bytesRead < 0) {
                response.message = "Failed to receive response from scmd";
                return response;
            }
            if (bytesRead == 0) {
                // 服务端关闭连接
                break;
            }
            buffer.append(recvBuf.data(), static_cast<size_t>(bytesRead));

            // 尝试从缓冲区提取完整消息
            auto messages = ControlProtocol::ExtractMessages(buffer);
            if (!messages.empty()) {
                // 取第一条完整消息作为响应
                if (!ControlProtocol::DecodeResponse(messages[0], response)) {
                    response.code = -1;
                    response.message = "Failed to decode response from scmd";
                    return response;
                }
                break;
            }
        }
        return response;
    }

    // 将 Json::Value 转为字符串，支持字符串/整数/无符号/浮点/bool
    static std::string JsonValueToString(const Json::Value &value) {
        if (value.isString()) {
            return value.asString();
        }
        if (value.isInt64()) {
            return std::to_string(value.asInt64());
        }
        if (value.isUInt64()) {
            return std::to_string(value.asUInt64());
        }
        if (value.isInt() || value.isUInt()) {
            return std::to_string(value.asInt());
        }
        if (value.isDouble()) {
            return std::to_string(value.asDouble());
        }
        if (value.isBool()) {
            return value.asBool() ? "true" : "false";
        }
        if (value.isNull()) {
            return "";
        }
        return value.toStyledString();
    }

    // 对 Json::Object 格式化为简洁键值对，跳过空值
    static std::string FormatJsonObject(const Json::Value &obj) {
        std::string output;
        for (const auto &key : obj.getMemberNames()) {
            const auto &value = obj[key];
            std::string strValue = JsonValueToString(value);
            if (strValue.empty()) {
                continue;  // 跳过空值
            }
            output += key + ": " + strValue + "\n";
        }
        return output;
    }

    // 对 Json::Array 格式化为简洁表格（适用于 list 命令）
    static std::string FormatJsonArray(const Json::Value &arr) {
        if (arr.empty()) {
            return "";
        }

        std::ostringstream oss;
        // 表头
        oss << std::left << std::setw(16) << "NAME"
            << std::setw(11) << "VERSION"
            << "AUTO_START" << "\n";
        oss << std::string(42, '-') << "\n";
        for (const auto &item : arr) {
            std::string name = item.isMember("serviceName") ? item["serviceName"].asString() : "";
            std::string version = item.isMember("version") ? item["version"].asString() : "";
            std::string autoStart = (item.isMember("isAutoStart") && item["isAutoStart"].asBool()) ? "true" : "false";

            oss << std::left << std::setw(16) << name
                << std::setw(11) << version
                << autoStart << "\n";
        }
        return oss.str();
    }

    // 根据 data 类型统一格式化
    static std::string FormatResponseData(const Json::Value &data) {
        if (data.isObject()) {
            return FormatJsonObject(data);
        }
        if (data.isArray()) {
            return FormatJsonArray(data);
        }
        return data.toStyledString() + "\n";
    }

    ResultMsg ScmCtlClient::FormatOutput(const ScmResponse &response) {
        std::string output;
        if (response.code == 0) {
            output = "[OK] " + response.message + "\n";
            if (!response.data.isNull()) {
                output += FormatResponseData(response.data);
            }
        } else if (response.code == 1) {
            output = "[WARN] " + response.message + "\n";
            if (!response.data.isNull()) {
                output += FormatResponseData(response.data);
            }
        } else {
            output = "[FAIL] " + response.message + "\n";
        }
        return ResultMsg(response.code, output);
    }

}  // namespace qifeng::scm
