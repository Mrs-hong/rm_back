/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <optional>
#include <string>

#include "common/status.h"

namespace qifeng_ca {

    /**
     * @brief 单个升级组件的元数据信息
     * @details 对应 metadata.json 中每个组件条目, 指定组件版本号与组件包路径。
     *          dir 为相对解压根目录的路径, 值即为组件包本身(目录/文件均可):
     *          - 目录形态(screen/qifeng_ca/model/nginx): 指向组件目录
     *          - 文件形态(qifeng_scm 的 .deb): 直接指向 .deb 文件
     *          "." 表示组件包直接在根目录下。
     */
    struct ComponentInfo {
        std::string version;  // 组件版本号
        std::string dir;      // 组件包路径(相对解压根目录, 可为目录或文件)
    };

    /**
     * @brief 升级包元数据(对应 metadata.json)
     * @details 设计文档规定 metadata.json 是升级包内必须存在的元数据文件, 升级流程先解析它
     *          以获取升级包版本号和各组件的精确路径定位, 替代目录名模式匹配。
     *
     * metadata.json 格式:
     * {
     *   "version": "1.0.7",
     *   "upgrade_time": "2026-07-31",
     *   "screen":     {"version": "1.0", "dir": "screen_firmware"},
     *   "qifeng_ca":  {"version": "1.0", "dir": "qifeng_ca"},
     *   "qifeng_scm": {"version": "1.0", "dir": "qifeng_scm_1.0.0.deb"},
     *   "model":      {"version": "1.0", "dir": "model_data"},
     *   "nginx":      {"version": "1.0", "dir": "nginx_config"}
     * }
     *
     * 各组件字段均为可选: 存在即表示升级包包含该组件, 不存在则不含。
     */
    struct PackageMetadata {
        std::string version;      // 升级包版本号
        std::string upgradeTime;  // 升级包生成时间

        std::optional<ComponentInfo> screen;     // 屏幕固件
        std::optional<ComponentInfo> qifengCa;   // CA 软件包
        std::optional<ComponentInfo> qifengScm;  // SCM deb 包
        std::optional<ComponentInfo> model;      // 模型文件
        std::optional<ComponentInfo> nginx;      // nginx 配置及前端页面

        /**
         * @brief 是否包含任意可升级组件
         * @return true 若至少一个组件字段有值
         */
        bool HasAnyComponent() const;

        /**
         * @brief 从 JSON 文件解析元数据
         * @param path metadata.json 文件路径
         * @param out  解析结果输出
         * @return Status 成功表示解析成功, 失败含具体原因
         */
        static Status ParseFromFile(const std::string &path, PackageMetadata &out);
    };

}  // namespace qifeng_ca
