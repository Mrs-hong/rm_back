/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/scmd_types.h"
#include <map>
#include <string>
#include <vector>

namespace qifeng {
    namespace scm {

        /**
         * @brief 配置加载器
         * 负责解析服务配置文件，管理服务注册
         */
        class ConfigLoader {
        public:
            ConfigLoader();
            ~ConfigLoader() = default;
            ConfigLoader(const ConfigLoader &) = delete;
            ConfigLoader &operator=(const ConfigLoader &) = delete;
            ConfigLoader(ConfigLoader &&) = delete;
            ConfigLoader &operator=(ConfigLoader &&) = delete;

            /**
             * @brief 初始化配置加载器
             * @param configDir 配置文件目录
             * @return ResultMsg 初始化结果
             */
            ResultMsg Initialize();

            /**
             * @brief 扫描服务目录，加载所有服务配置
             * @param servicesDir services目录路径
             * @return ResultMsg 扫描结果
             */
            ResultMsg ScanServicesDirectory(const std::string &servicesDir);

            /**
             * @brief 获取所有注册的服务
             * @return std::vector<ServiceDefinition> 服务列表
             */
            std::vector<ServiceDefinition> GetAllServices() const;

            /**
             * @brief 添加新服务
             * @param softwareDir 软件包目录
             * @return ResultMsg 操作结果
             */
            ResultMsg AddService(const std::string &softwareDir);

            /**
             * @brief 重新加载服务配置
             * @param serviceName 服务ID
             * @return ResultMsg 操作结果
             */
            ResultMsg ReloadService(const std::string &serviceName);

            /**
             * @brief 删除服务
             * @param serviceName 服务ID
             * @return ResultMsg 操作结果
             */
            ResultMsg RemoveService(const std::string &serviceName);

            /**
             * @brief 升级服务
             * @param softwareDir 新软件包目录
             * @return ResultMsg 操作结果
             */
            ResultMsg UpgradeService(const std::string &softwareDir);

            /**
             * @brief 根据服务名获取服务定义
             * @param serviceName 服务名称
             * @return ServiceDefinition* 服务定义指针，未找到返回nullptr
             */
            const ServiceDefinition* GetServiceByName(const std::string &serviceName) const;

            /**
             * @brief 根据服务名获取服务定义（可修改）
             * @param serviceName 服务名称
             * @return ServiceDefinition* 服务定义指针，未找到返回nullptr
             */
            ServiceDefinition* GetServiceByName(const std::string &serviceName);

            /**
             * @brief 写入SCMD自己的配置文件
             * @return ResultMsg 写入结果
             */
            ResultMsg WriteSelfConfigFile();

            /**
             * @brief 检查配置加载器是否已初始化
             * @return bool 是否已初始化
             */
            bool IsInitialized() const { return mInitialized; }

            /**
             * @brief 获取服务目录根路径
             * @return std::string 服务目录根路径
             */
            std::string GetServiceRootDir(const std::string &serviceName) const;

            /**
             * @brief 获取scmd自身配置信息
             * @return const ConfigInfo& 配置信息引用
             */
            const ConfigInfo &GetConfigInfo() const { return mConfigInfo; }

        private:
            /**
             * @brief 加载SCMD自己的配置文件
             * @return ResultMsg 加载结果
             */
            ResultMsg LoadSelfConfigFile();

        private:
            std::map<std::string, ServiceDefinition> mServices;  // 注册的服务列表
            ConfigInfo mConfigInfo;                              // scmd自己配置项
            bool mInitialized;                                   // 是否已初始化
        };

    }  // namespace scm
}  // namespace qifeng
