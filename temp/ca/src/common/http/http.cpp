//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <cstdint>
#include <string_view>

#include "drogon/HttpTypes.h"
#include "json/value.h"
#include "qifeng_ca/common.pb.h"
#include "qifeng_framework/common/logger.h"
#include "workflow/WFTaskFactory.h"

#include "common/audit_action_registry.h"
#include "common/common.h"
#include "common/http/http.h"
#include "common/status.h"
#include "common/utils/symlink_manager.h"
#include "dao_managers/audit_dao_manager.h"
#include "dao_managers/user_dao_manager.h"

namespace qifeng_ca {

    [[maybe_unused]] static Json::Value StringToJson(const std::string &jsonStr) {
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;

        // 解析 JSON 字符串
        std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
        if (!reader->parse(jsonStr.c_str(), jsonStr.c_str() + jsonStr.size(), &root, &errs)) {
            return Json::Value();
        }

        if (!root.isObject()) {
            return root;  // 或者根据需要返回空对象
        }
        if (root.isMember("data") && root["data"].isObject()) {
            // 返回前端不暴露后台细节需要删除 data 对象中的 "@type" 字段
            root["data"].removeMember("@type");
        }
        return root;
    }

    // 含明文密码的接口白名单: 这些接口的请求体不记录到审计日志, 防止密码泄露
    constexpr std::array<std::string_view, 10> PasswordEndpoints = {{
        "/web/user/login",
        "/web/user/updatePwd",
        "/web/user/updatePwdById",
        "/web/user/addUser",
        "/sys/user/adminForgotPassword",
        "/sys/user/adminResetPassword",
        "/web/meeting/saveTransformList",
        "/web/meeting/saveRecordingSummary",
        "/web/note/saveRecordingNote",
        "/sys/wifi/connect",
    }};

    static bool IsPasswordEndpoint(std::string_view path) {
        for (auto endpoint : PasswordEndpoints) {
            if (path == endpoint) {
                return true;
            }
        }
        return false;
    }

    // 捕获请求体数据用于审计日志
    // JSON请求直接保存原始body, multipart请求提取参数字段(不含文件内容)转为JSON
    // 密码相关接口直接返回 [masked], 避免明文密码进入审计日志
    static std::string CaptureRequestData(const drogon::HttpRequestPtr &httpReq) {
        if (IsPasswordEndpoint(httpReq->path())) {
            return "";
        }
        static constexpr size_t MaxRequestDataLen = 4096;
        std::string contentType = httpReq->getHeader("Content-Type");

        // JSON请求: 直接保存原始body
        if (contentType.find("application/json") != std::string::npos) {
            std::string body = std::string(httpReq->body());
            if (body.size() > MaxRequestDataLen) {
                body.resize(MaxRequestDataLen);
            }
            return body;
        }

        // multipart请求: 提取参数字段(不含文件内容)转为JSON
        if (contentType.find("multipart/form-data") != std::string::npos) {
            drogon::MultiPartParser parser;
            if (parser.parse(httpReq) != 0) {
                return "";
            }
            Json::Value root(Json::objectValue);
            for (const auto &kv : parser.getParameters()) {
                root[kv.first] = kv.second;
            }
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            std::string jsonStr = Json::writeString(builder, root);
            if (jsonStr.size() > MaxRequestDataLen) {
                jsonStr.resize(MaxRequestDataLen);
            }
            return jsonStr;
        }

        return "";
    }

    // 防止内联
    // 记录用户操作
    __attribute__((noinline)) static void AuditOpm(const drogon::HttpRequestPtr &httpReq, const Status &status) {
        // 记录审计日志
        try {
            // 通过全局映射表获取操作类型和描述
            OperationDesc opDesc;
            AuditRecordParam param;
            bool ret = ListenActionRegistry::GetInstance().Lookup(httpReq->path(), opDesc);
            // 未设置或者未开启则跳过操作记录
            if (!ret || !opDesc.mIsAudit) {
                SLOG_INFO << "audit  path" << httpReq->path();
                return;
            }

            param.mAction = opDesc.mAction;
            param.mActionDesc = opDesc.mActionDesc;
            uint64_t accountId = httpReq->attributes()->get<uint64_t>("accountId");
            if (accountId == 0 && status.GetExData() != nullptr) {
                accountId = status.GetExData()->mAccountId;
            }
            if (accountId == 0) {
                FLOG_ERROR("accountId is zero");
                return;
            }
            auto user = qifeng_ca::UserDaoManager::GetInstance().GetByAccountId(accountId);

            param.mUserId = accountId;
            param.mUserAccount = user.mAccount;
            param.mUserName = user.mUserName;
            param.mUserGroupId = user.mGroupId;
            param.mApiPath = httpReq->path();
            param.mHttpMethod = httpReq->methodString();
            param.mSuccess = (status.GetCode() == 0);
            param.mErrorMessage = param.mSuccess ? "" : status.GetMsg();
            param.mClientIp = httpReq->peerAddr().toIp();
            param.mRequestData = CaptureRequestData(httpReq);

            SLOG_DEBUG << "Audit record info: accountId=" << param.mUserId << ", account=" << param.mUserAccount
                       << ", group id=" << param.mUserGroupId << ", username=" << param.mUserName
                       << ", path=" << param.mApiPath << ", errs=" << param.mErrorMessage;
            qifeng_ca::AuditDaoManager::GetInstance().Record(param);
        } catch (const std::exception &e) {
            SLOG_WARN << "Audit record failed: " << e.what();
        }
    }

    static std::string EncodeContentDispositionFilename(const std::string &fileName) {
        std::ostringstream oss;
        oss << std::uppercase << std::hex;
        for (unsigned char ch : fileName) {
            if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '-' ||
                ch == '_' || ch == '.' || ch == '~') {
                oss << static_cast<char>(ch);
                continue;
            }
            oss << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
        return oss.str();
    }

    static std::string BuildInternalAudioRedirectUri(const std::string &filePath) {
        // 解析软链获取真实绝对路径, 避免 nginx 需要跟随多层软链(data/src -> /data2/qifeng_ca)
        std::error_code ec;
        std::filesystem::path realPath = std::filesystem::canonical(filePath, ec);
        if (ec) {
            SLOG_ERROR << "BuildInternalAudioRedirectUri: canonical failed, path=" << filePath
                       << ", err=" << ec.message();
            return "";
        }
        std::string real = realPath.generic_string();

        // 路径白名单: 仅允许 data2 目标目录和工作目录下 data 目录的真实路径
        // 防止通过构造 file_url 让 nginx 读取任意文件(如 /etc/passwd)
        const std::string &data2Target = SymlinkManager::GetInstance().GetTargetPath();
        std::string dataAbs = std::filesystem::absolute(std::filesystem::path("data"), ec).generic_string();
        if (ec) {
            SLOG_ERROR << "BuildInternalAudioRedirectUri: resolve data abs failed, err=" << ec.message();
            return "";
        }

        auto startsWith = [](const std::string &path, const std::string &prefix) {
            std::string fullPrefix = prefix.back() == '/' ? prefix : prefix + "/";
            return path.size() > fullPrefix.size() && path.compare(0, fullPrefix.size(), fullPrefix) == 0;
        };

        if (!startsWith(real, data2Target) && !startsWith(real, dataAbs)) {
            SLOG_ERROR << "BuildInternalAudioRedirectUri: path not in whitelist, real=" << real;
            return "";
        }

        return "/internal-audio" + real;
    }

    drogon::HttpResponsePtr BmsConvertHttpResp(const drogon::HttpRequestPtr &httpReq, const Status &status,
                                               const google::protobuf::Message* respValue) {
        {
            // 【审计】记录操作, 非关键路径
            // AuditOpm(httpReq, status);
            auto* getAuditWork = WFTaskFactory::create_go_task(WorkflowTakeName::AuditTask.data(),
                                                               [httpReq, status]() { AuditOpm(httpReq, status); });
            getAuditWork->start();
        }
        qifeng_ca::HttpResponse httpResp;

        int code = status.GetCode();
        httpResp.set_code(code);

        httpResp.set_message(code == 0 && status.GetMsg().empty() ? "成功" : status.GetMsg());

        if (respValue != nullptr) {
            httpResp.mutable_data()->PackFrom(*respValue);
        }

        std::string json;
        google::protobuf::util::JsonPrintOptions options;
        options.always_print_fields_with_no_presence = true;  // 强制输出所有空字段
        auto pbStatus = google::protobuf::util::MessageToJsonString(httpResp, &json);
        if (!pbStatus.ok()) {
            SLOG_ERROR << "BmsConvertHttpResp MessageToJsonString failed, " << pbStatus.ToString();
            auto respPtr = drogon::HttpResponse::newHttpResponse();
            respPtr->setStatusCode(drogon::k500InternalServerError);
            return respPtr;
        }

        Json::Value body = StringToJson(json);
        auto respPtr = drogon::HttpResponse::newHttpJsonResponse(body);
        respPtr->setStatusCode(code == 0 ? drogon::k200OK : drogon::k400BadRequest);
        return respPtr;
    }

    drogon::HttpResponsePtr BmsConvertXAccelRedirectResp(const drogon::HttpRequestPtr &httpReq, const Status &status,
                                                         const google::protobuf::Message* respValue) {
        if (status.GetCode() != 0) {
            return BmsConvertHttpResp(httpReq, status, respValue);
        }

        auto* downloadResp = dynamic_cast<const qifeng_ca::RecordDownloadResponse*>(respValue);
        if (downloadResp == nullptr || downloadResp->file_url().empty()) {
            SLOG_ERROR << "BmsConvertXAccelRedirectResp: missing download file path";
            return BmsConvertHttpResp(httpReq, Status {-1, "下载文件路径为空"}, nullptr);
        }

        const std::string &filePath = downloadResp->file_url();
        std::string internalRedirectUri = BuildInternalAudioRedirectUri(filePath);
        if (internalRedirectUri.empty()) {
            SLOG_ERROR << "BmsConvertXAccelRedirectResp: invalid redirect uri, path=" << filePath;
            return BmsConvertHttpResp(httpReq, Status {-1, "下载文件路径非法"}, nullptr);
        }

        std::string fileName = std::filesystem::path(filePath).filename().string();
        {
            auto* getAuditWork = WFTaskFactory::create_go_task(WorkflowTakeName::AuditTask.data(),
                                                               [httpReq, status]() { AuditOpm(httpReq, status); });
            getAuditWork->start();
        }

        auto respPtr = drogon::HttpResponse::newHttpResponse();
        respPtr->setStatusCode(drogon::k200OK);
        respPtr->setContentTypeCode(drogon::CT_APPLICATION_OCTET_STREAM);
        respPtr->addHeader("X-Accel-Redirect", internalRedirectUri);
        respPtr->addHeader("Content-Disposition",
                           "attachment; filename*=UTF-8''" + EncodeContentDispositionFilename(fileName));
        return respPtr;
    }

}  // namespace qifeng_ca
