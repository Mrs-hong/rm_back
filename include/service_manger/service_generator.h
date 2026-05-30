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
         * @return ResultMsg 操作结果 成功0,返回 .service 文件内容;失败,返回错误信息
         */
        static ResultMsg GenerateContent(const qifeng::scm::ServiceDefinition &serviceDef);
    };
}  // namespace qifeng::scm
