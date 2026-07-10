/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once
#include "common/types.h"
#include <string>
namespace qifeng::scm::tool {
    /**
     * @brief Nginx class
     *
     * @details 与系统服务nginx交互的类、使用cmd命令执行
     */
    class Nginx {
    public:
        /**
         * @brief 检测系统是否已安装 nginx
         *
         * @return true 已安装；false 未安装
         */
        bool IsInstalled() const;
        /**
         * @brief 检测 nginx 是否正在运行
         *
         * @return true 正在运行；false 未运行
         */
        bool IsRunning() const;
        /**
         * @brief 使用指定配置启动 nginx（独立配置模式）
         *
         * @param confPath 配置文件路径
         * @return ResultMsg 操作结果
         */
        ResultMsg Start(const std::string &confPath) const;
        /**
         * @brief 使用系统默认配置启动 nginx
         * @details 不指定 -c 参数，使用 /etc/nginx/nginx.conf 作为默认配置
         * @return ResultMsg 操作结果
         */
        ResultMsg StartSystem() const;
        /**
         * @brief 停止 nginx 服务
         *
         * @return ResultMsg 操作结果
         */
        ResultMsg Stop() const;
        /**
         * @brief 重载 nginx 配置
         *
         * @return ResultMsg 操作结果
         */
        ResultMsg Reload() const;
        /**
         * @brief 测试配置文件语法是否正确
         *
         * @param confPath 配置文件路径
         * @return ResultMsg:{0,成功}, {-1,错误信息}
         */
        ResultMsg TestConfig(const std::string &confPath) const;
        /**
         * @brief 测试系统默认配置语法是否正确
         * @details 不指定 -c 参数，测试 /etc/nginx/nginx.conf
         * @return ResultMsg 操作结果
         */
        ResultMsg TestSystemConfig() const;
        /**
         * @brief 获取 nginx 版本号
         *
         * @return ResultMsg:{0,版本号}, {-1,错误信息}
         */
        ResultMsg GetVersion() const;
    };
}  // namespace qifeng::scm::tool
