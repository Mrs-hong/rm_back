//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_HTTP_H
#define QIFENG_CA_INCLUDE_COMMON_HTTP_H

#include <cstdint>
#include <string>

#include "drogon/HttpRequest.h"
#include "drogon/HttpResponse.h"
#include "google/protobuf/util/json_util.h"
#include "qifeng_ca/hotword.pb.h"
#include "qifeng_ca/meeting.pb.h"
#include "qifeng_ca/upgrade.pb.h"
#include "qifeng_framework/http/http_register.h"

#include "common/audit_action_registry.h"
#include "common/status.h"

namespace qifeng_ca {

    template <typename Message>
    Status BmsPreAccountIdReq(const drogon::HttpRequestPtr &httpReq, Message &req) {
        uint64_t accountId = httpReq->attributes()->get<uint64_t>("accountId");
        req.set_account_id(accountId);
        return {};
    }

    // 上传录音文件前置处理: 解析multipart请求, 校验所有文件, 保存到临时目录
    // 将文件路径列表(file_paths)和accountId写入req, 供后续业务层使用
    Status BmsPreUploadFileReq(const drogon::HttpRequestPtr &httpReq, AddRecordingRequest &req);

    // 导入热词文件前置处理: 解析multipart请求, 校验Excel文件格式, 保存到临时目录
    // 将文件路径列表(file_paths)和accountId写入req, 供后续业务层使用
    Status BmsPreUploadHotwordFileReq(const drogon::HttpRequestPtr &httpReq, HotwordImportRequest &req);

    // 重新生成会议纪要前置处理: 解析multipart请求, 读取可选的docx模板文件转为文本,
    // 读取议题列表(topics), 连同accountId写入req, 供后续业务层使用
    Status BmsPreRefreshSummaryReq(const drogon::HttpRequestPtr &httpReq, RefreshSummaryRequest &req);

    // 服务升级前置处理: 解析multipart请求, 校验并保存 软件包(tar.gz/.tgz/.tar) 与 sha256清单文件(.sha256) 到升级目录
    // 将各文件路径和accountId写入req, 供后续业务层使用
    Status BmsPreUploadUpgradeReq(const drogon::HttpRequestPtr &httpReq, UpgradeRequest &req);

    drogon::HttpResponsePtr BmsConvertHttpResp(const drogon::HttpRequestPtr &httpReq, const Status &status,
                                               const google::protobuf::Message* respValue);

    drogon::HttpResponsePtr BmsConvertXAccelRedirectResp(const drogon::HttpRequestPtr &httpReq, const Status &status,
                                                         const google::protobuf::Message* respValue);
}  // namespace qifeng_ca

// drogon htpp 抽象层，使用BmsConvertHttpResp作为默认的 AfterConvertResp 处理函数
#define QIFENG_CA_METHOD_LIST_BEGIN(Controller) QIFENG_METHOD_LIST_BEGIN(Controller)

// desc: 操作描述（如 "用户登录"），同时注册到全局审计映射表
// 注意：下面的 xxx_ADD 接口如果设置空desc就表示该接口不做审计！！！！

// 提供给无过滤能力接口的宏定义（主要是: login）
#define QIFENG_CA_METHOD_ADD_NO_FILTER(desc, method, path, ...)                   \
    qifeng_ca::ListenActionRegistry::GetInstance().Register(path, #method, desc); \
    QIFENG_METHOD_AFTERRESP_ADD(method, qifeng_ca::BmsConvertHttpResp, path, ##__VA_ARGS__)

#define QIFENG_CA_METHOD_PREREQ_ADD_NO_FILTER(desc, method, preReqFunc, path, ...) \
    qifeng_ca::ListenActionRegistry::GetInstance().Register(path, #method, desc);  \
    QIFENG_METHOD_PREREQ_AFTERRESP_ADD(method, preReqFunc, qifeng_ca::BmsConvertHttpResp, path, ##__VA_ARGS__)

// 默认都加上Filter接口能力
#define QIFENG_CA_METHOD_ADD(desc, method, path, ...)                             \
    qifeng_ca::ListenActionRegistry::GetInstance().Register(path, #method, desc); \
    QIFENG_METHOD_AFTERRESP_ADD(method, qifeng_ca::BmsConvertHttpResp, path, ##__VA_ARGS__, "JwtFilter", "CARbacFilter")

#define QIFENG_CA_METHOD_PREREQ_ADD(desc, method, preReqFunc, path, ...)                                       \
    qifeng_ca::ListenActionRegistry::GetInstance().Register(path, #method, desc);                              \
    QIFENG_METHOD_PREREQ_AFTERRESP_ADD(method, preReqFunc, qifeng_ca::BmsConvertHttpResp, path, ##__VA_ARGS__, \
                                       "JwtFilter", "CARbacFilter")

#define QIFENG_CA_METHOD_AFTERRESP_ADD(desc, method, AfterConvertRespFunc, path, ...) \
    qifeng_ca::ListenActionRegistry::GetInstance().Register(path, #method, desc);     \
    QIFENG_METHOD_AFTERRESP_ADD(method, AfterConvertRespFunc, path, ##__VA_ARGS__, "JwtFilter", "CARbacFilter")

#define QIFENG_CA_METHOD_PREREQ_AFTERRESP_ADD(desc, method, preReqFunc, AfterConvertRespFunc, path, ...)           \
    qifeng_ca::ListenActionRegistry::GetInstance().Register(path, #method, desc);                                  \
    QIFENG_METHOD_PREREQ_AFTERRESP_ADD(method, preReqFunc, AfterConvertRespFunc, path, ##__VA_ARGS__, "JwtFilter", \
                                       "CARbacFilter")

#define QIFENG_CA_METHOD_LIST_END QIFENG_METHOD_LIST_END

#define QIFENG_CA_HTTP_METHOD_REGISTRY(Controller) QIFENG_HTTP_METHOD_REGISTRY(Controller)

#define QIFENG_CA_HTTP_FILTER_REGISTRY(FilterClass) QIFENG_HTTP_FILTER_REGISTRY(FilterClass)

#endif  // QIFENG_CA_INCLUDE_COMMON_HTTP_H
