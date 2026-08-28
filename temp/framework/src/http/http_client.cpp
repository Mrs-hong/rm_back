/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <fstream>
#include <sstream>
#include <workflow/HttpMessage.h>
#include <workflow/HttpUtil.h>
#include <workflow/WFGlobal.h>
#include <workflow/WFTaskFactory.h>

#include "common/logger.h"
#include "http/http_client.h"

namespace qifeng {

    void HttpClient::UploadFile(const std::string& url, const std::string& file_path,
                                const std::unordered_map<std::string, std::string>& headers, HttpCallback cb) {
        // 构造multipart/form-data请求
        try {
            const std::string boundary = HttpClient::DefaultBoundary;
            const std::string contentType = std::string("multipart/form-data; boundary=") + boundary;
            std::string fileContent;
            if (!ReadFileContent(file_path, fileContent)) {
                SLOG_ERROR << "Failed to read file for upload: " << file_path;
                HttpResponse rsp;
                rsp.mStatusCode = 0;
                rsp.mBody = "Failed to read file: " + file_path;
                cb(rsp);
                return;
            }
            const std::string filename = file_path.substr(file_path.find_last_of("/\\") + 1);
            std::string body;
            body += "--" + boundary + "\r\n";
            body += "Content-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\n";
            body += "Content-Type: application/octet-stream\r\n\r\n";
            body += fileContent + "\r\n";
            body += "--" + boundary + "--\r\n";
            HttpRequest req;
            req.mUrl = url;
            req.mMethod = HttpMethod::POST;
            req.mBody = body;
            req.mHeaders = headers;
            req.mHeaders["Content-Type"] = contentType;
            AsyncRequest(req, cb);
        } catch (const std::exception& e) {
            SLOG_ERROR << "Exception in UploadFile: " << e.what();
            HttpResponse rsp;
            rsp.mStatusCode = 0;
            rsp.mBody = std::string("Exception in UploadFile: ") + e.what();
            cb(rsp);
        } catch (...) {
            SLOG_ERROR << "Unknown exception in UploadFile";
            HttpResponse rsp;
            rsp.mStatusCode = 0;
            rsp.mBody = "Unknown exception in UploadFile";
            cb(rsp);
        }
    }
    // 拆分：读取文件内容，失败返回false，成功fileContent赋值
    bool HttpClient::ReadFileContent(const std::string& file_path, std::string& fileContent) {
        try {
            std::ifstream file(file_path, std::ios::binary);
            if (!file.is_open()) {
                SLOG_ERROR << "Failed to open file: " << file_path;
                return false;
            }
            std::ostringstream oss;
            oss << file.rdbuf();
            if (file.bad()) {
                SLOG_ERROR << "Error reading file: " << file_path;
                return false;
            }
            fileContent = oss.str();
            return true;
        } catch (const std::exception& e) {
            SLOG_ERROR << "Exception in ReadFileContent: " << e.what();
            return false;
        } catch (...) {
            SLOG_ERROR << "Unknown exception in ReadFileContent";
            return false;
        }
    }

    void HttpClient::DownloadFile(const std::string& url, const std::string& save_path,
                                  const std::unordered_map<std::string, std::string>& headers, HttpCallback cb) {
        HttpRequest req;
        req.mUrl = url;
        req.mMethod = HttpMethod::GET;
        req.mHeaders = headers;
        AsyncRequest(req, [save_path, cb](const HttpResponse& rsp) {
            if (rsp.mStatusCode >= 200 && rsp.mStatusCode < 300) {
                if (!WriteFileContent(save_path, rsp.mBody)) {
                    SLOG_ERROR << "Failed to write response body to file: " << save_path;
                }
            } else {
                SLOG_ERROR << "Failed to download file, HTTP status code: " << rsp.mStatusCode;
            }
            cb(rsp);
        });
    }

    // 拆分：写入文件内容，失败返回false，成功返回true
    bool HttpClient::WriteFileContent(const std::string& file_path, const std::string& content) {
        try {
            std::ofstream ofs(file_path, std::ios::binary);
            if (!ofs.is_open()) {
                SLOG_ERROR << "Failed to open file for writing: " << file_path;
                return false;
            }
            ofs << content;
            if (ofs.bad()) {
                SLOG_ERROR << "Error writing to file: " << file_path;
                return false;
            }
            ofs.close();
            return true;
        } catch (const std::exception& e) {
            SLOG_ERROR << "Exception in WriteFileContent: " << e.what();
            return false;
        } catch (...) {
            SLOG_ERROR << "Unknown exception in WriteFileContent";
            return false;
        }
    }

}  // namespace qifeng
