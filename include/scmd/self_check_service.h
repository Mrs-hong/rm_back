/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/json_load.h"

#include <string>

namespace qifeng::scm {
    class ConfigLoader;

    /**
     * @brief 开机自检服务
     * @details 从 ScmServer 抽出的职责：读取自检配置、加载 selftest.json、
     *          运行 CheckerRunner、根据 fail_action 决定是否阻止启动。
     *          应在 ScmServer.Start() 之前调用，确保设备就绪后再进入服务循环。
     */
    class SelfCheckService {
    public:
        /**
         * @brief 构造函数
         * @param configLoader 配置加载器（用于读取自检开关、路径、fail_action）
         */
        explicit SelfCheckService(const ConfigLoader& configLoader);

        /**
         * @brief 执行开机自检
         * @details 读取自检配置文件并运行所有检查项，输出结果摘要。
         * @return true 自检通过或仅告警，可以继续启动；false 自检失败且 fail_action=halt，应中止启动
         */
        bool Run();

    private:
        const ConfigLoader& mConfigLoader;
        JsonLoad mJsonLoader;            // JSON 加载器，加载 selftest.json 供 CheckerRunner 使用
        std::string mSelfTestConfigPath;  // 自检配置文件路径
    };

}  // namespace qifeng::scm
