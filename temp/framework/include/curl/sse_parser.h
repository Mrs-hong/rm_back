/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_CURL_SSE_PARSER_H
#define QIFENG_FRAMEWORK_INCLUDE_CURL_SSE_PARSER_H

#include <functional>
#include <json/json.h>
#include <string>

class SSEParser {
public:
    using EventCallback = std::function<void(const Json::Value& data)>;
    using DoneCallback = std::function<void()>;

    explicit SSEParser(EventCallback on_event, DoneCallback on_done = nullptr)
        : mOnEvent(std::move(on_event)), mOnDone(std::move(on_done)) {
    }

    void Feed(std::string_view chunk) {
        mBuffer.append(chunk);
        size_t pos = 0;
        while ((pos = mBuffer.find("\n\n")) != std::string::npos) {
            std::string event = mBuffer.substr(0, pos);
            mBuffer.erase(0, pos + 2);
            ParseEvent(event);
        }
    }

private:
    void ParseEvent(const std::string& event) {
        std::string data;
        std::istringstream stream(event);
        std::string line;
        while (std::getline(stream, line)) {
            // 以 "data:" 开头
            if (line.rfind("data:", 0) == 0) {
                // 去掉 "data:"
                data += line.substr(5);
                data += '\n';
            }
        }
        if (!data.empty() && data.back() == '\n') {
            // 移除末尾多余换行
            data.pop_back();
        }

        if (data == "[DONE]") {
            if (mOnDone) {
                mOnDone();
            }
            return;
        }
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(data, root)) {
            if (mOnEvent) {
                mOnEvent(root);
            }
        }
    }

    std::string mBuffer;
    EventCallback mOnEvent;
    DoneCallback mOnDone;
};

#endif  // QIFENG_FRAMEWORK_INCLUDE_CURL_SSE_PARSER_H