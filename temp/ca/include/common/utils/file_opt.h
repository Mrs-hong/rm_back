//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_UTILS_FILE_OPT_H
#define QIFENG_CA_INCLUDE_COMMON_UTILS_FILE_OPT_H

#include <string>

namespace qifeng_ca {
    class FileOpt {
    public:
        // 根据文件路径创建其父目录(若不存在)
        static bool CreateDstDirectory(const std::string &dstPath);

        // 直接创建目录(若不存在)
        static bool CreateDirectory(const std::string &dirPath);

        // 删除文件
        static bool RemoveFile(const std::string &fileName);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_UTILS_FILE_OPT_H
