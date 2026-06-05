/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once
#include "common/scmd_types.h"

namespace qifeng::scm {
    class ServiceGenerator {
    public:
        /**
         * @brief 从 ServiceDefinition 生成 .service 文件内容
         * @param serviceDef 服务定义
         * @param unitPrefix systemd 单元名前缀，用于依赖声明，如 "scmd_"
         * @return ResultMsg 操作结果 成功0,返回 .service 文件内容;失败,返回错误信息
         */
        static ResultMsg GenerateContent(const qifeng::scm::ServiceDefinition &serviceDef,
                                         const std::string &unitPrefix);
    };
}  // namespace qifeng::scm
