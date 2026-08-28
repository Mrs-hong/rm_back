/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <fstream>
#include <sys/types.h>
#include <vector>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"

#include "common/atomic_write_file.h"
#include "common/config/device_config.h"

namespace qifeng_ca {
    bool AtomicFileWriter::Fsync(int fd, mode_t mode) {
        // 保证数据从内核缓冲区落到磁盘
        if (fsync(fd) < 0) {
            SLOG_ERROR << "fsync failed: " << strerror(errno);
            close(fd);
            return false;
        }

        if (fchmod(fd, mode) < 0) {
            SLOG_ERROR << "fchmod failed: " << strerror(errno);
            close(fd);
            return false;
        }

        close(fd);
        return true;
    }

    // 写入：同目录临时文件 + fsync + rename
    bool AtomicFileWriter::WriteAtomic(const std::string &targetPath, const std::string &content, mode_t mode) {
        std::string tempPath = targetPath + ".tmp.XXXXXX";
        std::vector<char> tempBuf(tempPath.begin(), tempPath.end());
        tempBuf.push_back('\0');

        int fd = mkstemp(tempBuf.data());
        if (fd < 0) {
            SLOG_ERROR << "mkstemp failed: " << strerror(errno);
            return false;
        }

        std::string tempFile(tempBuf.data());
        ScopeExit cleanup([&tempFile]() {
            if (!tempFile.empty()) {
                std::remove(tempFile.c_str());
            }
        });

        const char* ptr = content.data();
        size_t remaining = content.size();
        while (remaining > 0) {
            ssize_t written = write(fd, ptr, remaining);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                SLOG_ERROR << "write failed: " << strerror(errno);
                close(fd);
                return false;
            }
            ptr += written;
            remaining -= written;
        }

        if (!Fsync(fd, mode)) {
            return false;
        }

        // rename() 是原子操作
        if (std::rename(tempFile.c_str(), targetPath.c_str()) < 0) {
            SLOG_ERROR << "rename failed: " << strerror(errno);
            return false;
        }

        tempFile.clear();
        return true;
    }

    // 读取完整个文件（只能用于配置等小文件）
    Status AtomicFileWriter::ReadFile(const std::string_view srcPath, std::string &outStr) {
        std::ifstream ifs(srcPath.data(), std::ios::binary);
        if (!ifs) {
            return {1, "文件不存在"};
        }

        // 获取文件大小
        ifs.seekg(0, std::ios::end);
        std::streamsize fileSize = ifs.tellg();
        ifs.seekg(0, std::ios::beg);

        if (fileSize == -1) {
            return {1, "文件读取失败"};
        }
        if (static_cast<int>(fileSize) > DeviceConfig::GetInstance().GetConfigFileMaxSize()) {
            return {1, "文件过大"};
        }

        std::ostringstream oss;
        oss << ifs.rdbuf();
        outStr = std::move(oss.str());
        return {};
    }

    bool AtomicFileWriter::Backup(const std::string &srcPath) {
        std::string outStr;
        if (ReadFile(srcPath, outStr).GetCode() != 0) {
            return false;
        }
        return WriteAtomic(srcPath + ".bak", outStr);
    }

    bool AtomicFileWriter::Restore(const std::string &srcPath) {
        std::string bakPath = srcPath + ".bak";
        struct stat st {};
        if (stat(bakPath.c_str(), &st) < 0) {
            std::remove(srcPath.c_str());
            return true;
        }
        return std::rename(bakPath.c_str(), srcPath.c_str()) == 0;
    }

}  // namespace qifeng_ca
