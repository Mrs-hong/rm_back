/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief ADD_MODEL 命令请求参数
     */
    struct AddModelRequest {
        std::string srcPath;  // 模型源路径（目录或 tar/tar.gz 包）
    };

    template <>
    struct RequestCommand<AddModelRequest> { static constexpr ScmCommand value = ScmCommand::ADD_MODEL; };

    inline Json::Value ToJson(const AddModelRequest& req) {
        Json::Value root;
        root["srcPath"] = req.srcPath;
        return root;
    }

    /**
     * @brief ADD_MODEL 命令处理器
     * @details 安装/升级模型文件：将指定目录或 tar 包中的模型安装到 scmd.yaml 配置的
     *          model_dir 下，自动停止依赖模型的服务并验证运行状态，失败时回退。
     */
    class AddModelHandler : public ICommandHandler {
    public:
        explicit AddModelHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
