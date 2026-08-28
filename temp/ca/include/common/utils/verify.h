//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_UTILS_VERIFY_H
#define QIFENG_CA_INCLUDE_COMMON_UTILS_VERIFY_H

#include <string>

#include "common/status.h"

namespace qifeng_ca {

    // 升级包完整性校验工具类
    // 使用 SHA256 校验软件包完整性: 读取客户端上传的 .sha256 清单文件中的期望哈希,
    // 计算软件包实际 SHA256 并比对, 一致则校验通过
    // 升级上传约定包含两个文件: 软件包(*.tar.gz/*.tgz/*.tar)、sha256清单文件(*.sha256)
    class Verify {
    public:
        // 升级包完整性校验: 读取sha256清单文件, 计算软件包SHA256, 比对是否一致
        // packagePath: 软件包文件路径(.tar.gz/.tgz/.tar)
        // sha256Path: sha256清单文件路径(.sha256, 内含期望哈希值, 格式兼容 sha256sum 输出)
        // 返回 Status: code=0 校验通过, code!=0 校验失败(msg描述原因)
        static Status VerifyUpgradePackage(const std::string &packagePath,
                                           const std::string &sha256Path);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_UTILS_VERIFY_H
