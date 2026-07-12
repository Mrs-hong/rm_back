/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <optional>
#include <string>

namespace qifeng::scm {

    /**
     * @brief scmctl命令类型枚举
     * @details 定义scmctl支持的所有命令类型，用于scmctl和scmd之间的通信
     */
    enum class ScmCommand {
        VERSION = 0,    // --version / -v 查看版本
        INSTALL,        // install --name [--tar_dir] 安装服务
        START,          // start --name [--version] 启动服务
        STOP,           // stop --name 停止服务
        STOP_ALL,       // stop -a 停止全部服务
        RESTART,        // restart --name 重启服务
        RESTART_ALL,    // restart -a 重启全部
        UPGRADE,        // upgrade --name --tar_dir 升级服务
        LIST,           // list 查看所有服务
        INFO,           // info --name 查看服务详情
        LOG,            // log --type -n 查看操作日志
        UNINSTALL,      // uninstall --name 卸载服务
        UNINSTALL_ALL,  // uninstall -a 卸载全部已装服务（保留 scmd 自身）
        RELOAD,         // reload --name 重载服务配置
        RELOAD_ALL,     // reload -a 重载全部服务配置
        KILL,           // kill 使scmd优雅退出
        SLOG,           // slog --name [--count] 查看服务 journal 日志
        CHECK,          // check 设备自检
        INIT_NGINX,     // init_nginx -n <name> -d <path> 独立配置服务的 nginx
        RESET_NGINX,    // reset_nginx -n <name> 重置服务的 nginx 配置
        ADD_MODEL,      // add_model <path> 安装/升级模型文件
        CLEAR_MODEL,    // clear_model -n <name> 清除模型
        UPGRADES        // upgrades -n <name> 使用服务内部预置升级包升级并写入结果文件
    };

    /**
     * @brief 命令类型转字符串（用于日志和调试）
     * @param cmd 命令枚举值
     * @return 对应的字符串名称
     */
    const char* ScmCommandToString(ScmCommand cmd);

    /**
     * @brief 字符串转命令类型
     * @param str 命令名字符串
     * @return 成功返回对应枚举值，失败返回 std::nullopt
     */
    std::optional<ScmCommand> StringToScmCommand(const std::string& str);

}  // namespace qifeng::scm
