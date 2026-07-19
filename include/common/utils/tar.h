/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/types.h"

#include <string>

namespace qifeng::scm::utils {
    /**
     * @description: 解压tar包到指定目录
     * @param {string} &tarPath tar包路径
     * @param {string} &extractDir 解压目录路径
     * @return {ResultMsg} 解压结果
     */
    ResultMsg ExtractTar(const std::string &tarPath, const std::string &extractDir);

    /**
     * @description: 压缩目录为tar包
     * @param {string} &dir 目录路径
     * @param {string} &tarPath tar包路径
     * @return {ResultMsg} 压缩结果
     */
    ResultMsg CompressDirToTar(const std::string &dir, const std::string &tarPath);

    /**
     * @description: 验证tar包是否和sha256文件匹配
     * @param {string} &tarPath tar包路径
     * @param {string} &sha256Path sha256文件路径
     * @return {ResultMsg} 验证结果
     */
    ResultMsg VerifyTarWithSha256(const std::string &tarPath, const std::string &sha256Path);

}  // namespace qifeng::scm::utils
