#include <filesystem>

#include "qifeng_framework/common/logger.h"

#include "common/utils/file_opt.h"

namespace qifeng_ca {
    bool FileOpt::CreateDstDirectory(const std::string &dstPath) {
        std::filesystem::path dstDir = std::filesystem::path(dstPath).parent_path();
        if (dstDir.empty() || std::filesystem::exists(dstDir)) {
            return true;
        }

        std::error_code ec;
        std::filesystem::create_directories(dstDir, ec);
        if (ec) {
            SLOG_ERROR << "FileOpt: create dst dir failed, ec=" << ec.message();
            return false;
        }
        return true;
    }

    bool FileOpt::CreateDirectory(const std::string &dirPath) {
        if (dirPath.empty()) {
            return false;
        }

        std::error_code ec;
        if (std::filesystem::exists(dirPath, ec)) {
            return true;
        }
        std::filesystem::create_directories(dirPath, ec);
        if (ec) {
            SLOG_ERROR << "FileOpt: create directory failed, path=" << dirPath << " ec=" << ec.message();
            return false;
        }
        return true;
    }

    bool FileOpt::RemoveFile(const std::string &fileName) {
        if (fileName.empty()) {
            return false;
        }

        if (!std::filesystem::exists(fileName)) {
            return false;
        }
        std::filesystem::remove(fileName);
        SLOG_INFO << "RemoveFile removed, file: " << fileName;
        return true;
    }
}  // namespace qifeng_ca
