/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include "common/types.h"
#include "ipc/data_def.h"

namespace qifeng::scm {

    /**
     * @brief 命令行参数解析器
     * @details 使用CLI11解析命令行参数，根据ipc/commd.md定义命令，
     * 若符合则生成ScmRequest，否则通过ResultMsg返回错误信息
     */
    class CliPaser {
    public:
        CliPaser() = default;
        ~CliPaser() = default;

        CliPaser(const CliPaser &) = delete;
        CliPaser &operator=(const CliPaser &) = delete;
        CliPaser(CliPaser &&) = delete;
        CliPaser &operator=(CliPaser &&) = delete;

        /**
         * @brief 解析命令行参数
         * @param argc 参数个数
         * @param argv 参数数组
         * @return ResultMsg
         *         code=0: 解析成功，通过 GetRequest() 获取请求
         *         code=1: 需要输出 msg 后退出（--help/--version/无子命令）
         *         code=-1: 解析错误，msg 包含错误信息
         */
        ResultMsg Parse(int argc, char** argv);

        /**
         * @brief 获取解析后的请求
         * @return const ScmRequest& 请求引用
         */
        const ScmRequest &GetRequest() const;

    private:
        ScmRequest mRequest;
    };

}  // namespace qifeng::scm
