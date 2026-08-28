/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_CLIENT_H
#define QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_CLIENT_H

#include <functional>
#include <string>
#include <unordered_map>
#include <workflow/WFFacilities.h>
#include <workflow/WFHttpServer.h>

#include "http.h"

// 封装HTTP请求/响应、客户端、服务端等通用能力
// 依赖workflow库

namespace qifeng {

    // HTTP异步请求回调类型
    using HttpCallback = std::function<void(const HttpResponse& rsp)>;

    class HttpClient : public Http {
        // 写入文件内容，失败返回false，成功返回true
        static bool WriteFileContent(const std::string& file_path, const std::string& content);
        // 读取文件内容，失败返回false，成功fileContent赋值
        static bool ReadFileContent(const std::string& file_path, std::string& fileContent);

    public:
        // 文件上传（异步，回调）
        // @param url       上传目标URL
        // @param file_path 本地文件路径
        // @param headers   额外请求头
        // @param cb        响应回调
        static void UploadFile(const std::string& url, const std::string& file_path,
                               const std::unordered_map<std::string, std::string>& headers, HttpCallback cb);

        // 文件下载（异步，回调，下载到本地文件）
        // @param url       下载目标URL
        // @param save_path 本地保存路径
        // @param headers   额外请求头
        // @param cb        响应回调
        static void DownloadFile(const std::string& url, const std::string& save_path,
                                 const std::unordered_map<std::string, std::string>& headers, HttpCallback cb);
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_CLIENT_H