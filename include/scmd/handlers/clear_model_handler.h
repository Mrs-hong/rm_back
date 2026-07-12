/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief CLEAR_MODEL 命令请求参数
     */
    struct ClearModelRequest {
        std::string modelName;  // 模型名（model_dir 下的文件或目录名）
    };

    template <>
    struct RequestCommand<ClearModelRequest> { static constexpr ScmCommand value = ScmCommand::CLEAR_MODEL; };

    inline Json::Value ToJson(const ClearModelRequest& req) {
        Json::Value root;
        root["modelName"] = req.modelName;
        return root;
    }

    /**
     * @brief CLEAR_MODEL 命令处理器
     * @details 清除（删除）模型：临时重命名为 .back 以便验证回退，验证通过后删除模型，失败则回退。
     */
    class ClearModelHandler : public ICommandHandler {
    public:
        explicit ClearModelHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
