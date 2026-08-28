/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_COMMON_ATOMIC_WRITE_FILE_H
#define QIFENG_CA_INCLUDE_COMMON_ATOMIC_WRITE_FILE_H

#include <string>
#include <string_view>
#include <sys/types.h>  // mode_t

#include "common/status.h"

namespace qifeng_ca {

    class AtomicFileWriter {
    public:
        static bool Fsync(int fd, mode_t mode);

        // 写入：同目录临时文件 + fsync + rename
        static bool WriteAtomic(const std::string &targetPath, const std::string &content, mode_t mode = 0644);

        // 读取完整个文件（只能用于配置等小文件）
        static Status ReadFile(const std::string_view srcPath, std::string &outStr);

        static bool Backup(const std::string &srcPath);

        static bool Restore(const std::string &srcPath);
    };
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_ATOMIC_WRITE_FILE_H
