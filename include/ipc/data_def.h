/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <json/json.h>
#include <string>

#include "common/utils/json_utils.h"

namespace qifeng::scm {

    /**
     * @brief scmctl命令类型枚举
     * 定义scmctl支持的所有命令类型，用于scmctl和scmd之间的通信
     */
    enum class ScmCommand {
        VERSION = 0,  // --version / -v 查看版本
        INSTALL,      // install --name [--tar_dir] 安装服务
        START,        // start --name [--version] 启动服务
        STOP,         // stop --name 停止服务
        RESTART,      // restart --name 重启服务
        RESTART_ALL,  // restart -a 重启全部
        UPGRADE,      // upgrade --name --tar_dir 升级服务
        LIST,         // list 查看所有服务
        INFO,         // info --name 查看服务详情
        LOG,          // log --type -n 查看操作日志
        UNINSTALL,    // uninstall --name 卸载服务
        RELOAD,       // reload --name/-a 重载服务配置
        KILL          // kill 使scmd优雅退出
    };

    /**
     * @brief 命令类型转字符串（用于日志和调试）
     */
    const char* ScmCommandToString(ScmCommand cmd);

    /**
     * @brief scmctl请求结构体
     * 定义scmctl发送给scmd的请求数据
     * 使用json_utils.h宏实现JSON序列化/反序列化
     */
    struct ScmRequest {
        ScmCommand command {ScmCommand::VERSION};
        std::string serviceName;  // 可选，服务名称
        std::string version;      // 可选，服务版本
        std::string tarDir;       // 可选，install/upgrade时指定tar包目录
        int logLevel {0};         // 可选，log命令的日志级别
        int logCount {0};         // 可选，log命令的日志条数

        BEGIN_JSON_PARSER
        ADD_MEMBER(command)
        ADD_MEMBER(serviceName)
        ADD_MEMBER(version)
        ADD_MEMBER(tarDir)
        ADD_MEMBER(logLevel)
        ADD_MEMBER(logCount)
        END_JSON_PARSER

        /**
         * @brief const版本的JSON序列化
         * 宏生成的toJson()是非const方法，此处提供const版本用于const对象序列化
         */
        Json::Value ToJsonConst() const {
            ScmRequest &mutableThis = const_cast<ScmRequest &>(*this);  // NOLINT (readability-const-cast)
            // 宏全的const_cast，因为ScmRequest的toJson()方法是const的
            return mutableThis.toJson();
        }
    };

    /**
     * @brief scmd响应结构体
     * 定义scmd返回给scmctl的响应数据
     * 使用json_utils.h宏实现JSON序列化/反序列化
     */
    struct ScmResponse {
        int code {0};         // 0:成功, -1:失败, 1:警告
        std::string message;  // 结果消息
        Json::Value data;     // 灵活的数据载荷（服务列表、运行时信息等）

        BEGIN_JSON_PARSER
        ADD_MEMBER(code)
        ADD_MEMBER(message)
        ADD_MEMBER(data)
        END_JSON_PARSER

        /**
         * @brief const版本的JSON序列化
         * 宏生成的toJson()是非const方法，此处提供const版本用于const对象序列化
         */
        Json::Value ToJsonConst() const {
            ScmResponse &mutableThis = const_cast<ScmResponse &>(*this);  // NOLINT (readability-const-cast)
            // 宏全的const_cast，因为ScmResponse的toJson()方法是const的
            return mutableThis.toJson();
        }
    };

}  // namespace qifeng::scm
