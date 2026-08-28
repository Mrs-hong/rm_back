//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <cstdint>
#include <fstream>
#include <system_error>

#include "qifeng_framework/common/logger.h"

#include "common/config/meeting_config.h"
#include "common/utils/file_opt.h"
#include "common/utils/symlink_manager.h"

namespace qifeng_ca {

    SymlinkManager &SymlinkManager::GetInstance() {
        static SymlinkManager Instance;
        return Instance;
    }

    bool SymlinkManager::Init(std::string_view symlinkPath, std::string_view targetPath) {
        std::lock_guard<std::mutex> lock(mMutex);

        mSymlinkPath = std::string(symlinkPath);
        mTargetPath = std::string(targetPath);

        // 检测 /data2 是否存在
        std::error_code ec;
        auto data2Path = std::filesystem::path("/data2");
        mData2Available.store(std::filesystem::exists(data2Path, ec) && !ec, std::memory_order_release);
        if (!mData2Available.load(std::memory_order_acquire)) {
            SLOG_INFO << "SymlinkManager: /data2 not found, skip symlink setup";
            mInitialized.store(true, std::memory_order_release);
            return true;
        }

        // 创建目标目录 /data2/qifeng_ca (复用FileOpt统一文件管理接口)
        auto targetAbs = std::filesystem::absolute(std::filesystem::path(mTargetPath));
        if (!FileOpt::CreateDirectory(targetAbs.string())) {
            SLOG_ERROR << "SymlinkManager: create target dir failed, path=" << targetAbs;
            mData2Available.store(false, std::memory_order_release);
            mInitialized.store(true, std::memory_order_release);
            return false;
        }

        // 删除已存在的软连接(可能是无效的)
        auto symlinkAbs = std::filesystem::absolute(std::filesystem::path(mSymlinkPath));
        if (std::filesystem::exists(symlinkAbs, ec)) {
            if (std::filesystem::is_symlink(symlinkAbs, ec)) {
                std::filesystem::remove(symlinkAbs, ec);
                if (ec) {
                    SLOG_ERROR << "SymlinkManager: remove old symlink failed, error=" << ec.message();
                }
            } else if (!ec) {
                // 路径存在但不是软连接, 跳过不处理
                SLOG_WARN << "SymlinkManager: path exists but not a symlink, path=" << symlinkAbs;
            }
        }

        // 创建软连接: data/src -> /data2/qifeng_ca
        if (!std::filesystem::exists(symlinkAbs, ec)) {
            std::filesystem::create_directory_symlink(targetAbs, symlinkAbs, ec);
            if (ec) {
                SLOG_ERROR << "SymlinkManager: create symlink failed, link=" << symlinkAbs << " target=" << targetAbs
                           << " error=" << ec.message();
                mData2Available.store(false, std::memory_order_release);
                mInitialized.store(true, std::memory_order_release);
                return false;
            }
            SLOG_INFO << "SymlinkManager: symlink created, " << symlinkAbs << " -> " << targetAbs;
        }

        mInitialized.store(true, std::memory_order_release);
        SLOG_INFO << "SymlinkManager: initialized, data2Available=" << mData2Available.load(std::memory_order_acquire)
                  << " symlink=" << mSymlinkPath << " target=" << mTargetPath;
        return true;
    }

    bool SymlinkManager::IsData2Available() const {
        return mData2Available.load(std::memory_order_acquire);
    }

    std::string SymlinkManager::GetSymlinkPath() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mSymlinkPath;
    }

    std::string SymlinkManager::GetTargetPath() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mTargetPath;
    }

    std::string SymlinkManager::ToSymlinkPath(std::string_view originalPath) const {
        // 将 "data/xxx" 转换为 "data/src/xxx"
        // mData2Available/mInitialized为atomic, 用load显式读取
        if (!mData2Available.load(std::memory_order_acquire) || !mInitialized.load(std::memory_order_acquire)) {
            return std::string(originalPath);
        }

        std::string_view prefix("data/");
        if (originalPath.substr(0, prefix.size()) == prefix) {
            return mSymlinkPath + "/" + std::string(originalPath.substr(prefix.size()));
        }
        return std::string(originalPath);
    }

    bool SymlinkManager::IsSymlinkValid() const {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mData2Available.load(std::memory_order_acquire)) {
            return false;
        }

        std::error_code ec;
        auto symlinkAbs = std::filesystem::absolute(std::filesystem::path(mSymlinkPath));
        if (!std::filesystem::exists(symlinkAbs, ec) || ec) {
            return false;
        }
        if (!std::filesystem::is_symlink(symlinkAbs, ec) || ec) {
            return false;
        }
        // 符号链接本身存在, 检查目标是否存在
        auto target = std::filesystem::read_symlink(symlinkAbs, ec);
        if (ec) {
            return false;
        }
        // 如果目标为相对路径, 相对于符号链接所在目录解析
        auto targetAbs = target.is_absolute() ? target : symlinkAbs.parent_path() / target;
        return std::filesystem::exists(targetAbs, ec) && !ec;
    }

    bool SymlinkManager::SymlinkFileExists(std::string_view relativePath) const {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mData2Available.load(std::memory_order_acquire)) {
            return false;
        }

        std::string symlinkPath = ToSymlinkPath(relativePath);
        std::error_code ec;
        return std::filesystem::exists(std::filesystem::path(symlinkPath), ec) && !ec;
    }

    bool SymlinkManager::RemoveSymlinkFile(std::string_view relativePath) const {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mData2Available.load(std::memory_order_acquire)) {
            return false;
        }

        std::string symlinkPath = ToSymlinkPath(relativePath);
        std::error_code ec;
        auto fullPath = std::filesystem::path(symlinkPath);
        if (!std::filesystem::exists(fullPath, ec) || ec) {
            return true;
        }
        return std::filesystem::remove(fullPath, ec);
    }

    static bool FileSizeEqual(const std::string &pathA, const std::string &pathB) {
        std::error_code ec;
        auto sizeA = std::filesystem::file_size(pathA, ec);
        if (ec) {
            return false;
        }
        auto sizeB = std::filesystem::file_size(pathB, ec);
        if (ec) {
            return false;
        }
        return sizeA == sizeB;
    }

    bool SymlinkManager::CopyToSymlinkUnsafe(const std::string &sourcePath, std::string_view relativePath) const {
        if (!mData2Available.load(std::memory_order_acquire)) {
            return false;
        }

        std::string symlinkPath = ToSymlinkPath(relativePath);
        std::error_code ec;
        auto dstPath = std::filesystem::path(symlinkPath);
        if (!FileOpt::CreateDstDirectory(dstPath.string())) {
            return false;
        }
        std::filesystem::copy_file(sourcePath, dstPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            SLOG_ERROR << "SymlinkManager: copy failed, src=" << sourcePath << " dst=" << dstPath
                       << " error=" << ec.message();
            return false;
        }
        if (!FileSizeEqual(sourcePath, dstPath.string())) {
            SLOG_ERROR << "SymlinkManager: size mismatch after copy, src=" << sourcePath << " dst=" << dstPath;
            std::filesystem::remove(dstPath, ec);
            return false;
        }
        return true;
    }

    bool SymlinkManager::CopyToSymlink(const std::string &sourcePath, std::string_view relativePath) const {
        std::lock_guard<std::mutex> lock(mMutex);
        return CopyToSymlinkUnsafe(sourcePath, relativePath);
    }

    std::string SymlinkManager::MigrateToSymlink(const std::string &sourcePath, std::string_view relativePath) const {
        std::lock_guard<std::mutex> lock(mMutex);
        // 调用无锁版避免死锁(当前已持锁)
        if (!CopyToSymlinkUnsafe(sourcePath, relativePath)) {
            return "";
        }

        std::string symlinkPath = ToSymlinkPath(relativePath);
        std::error_code ec;
        auto dstPath = std::filesystem::path(symlinkPath);
        std::filesystem::remove(sourcePath, ec);
        if (ec) {
            SLOG_WARN << "SymlinkManager: remove source failed, path=" << sourcePath << " error=" << ec.message();
        } else {
            SLOG_INFO << "SymlinkManager: source removed after migrate, path=" << sourcePath << " -> " << dstPath;
        }
        return dstPath.string();
    }

    std::string SymlinkManager::MigrateToSymlinkIfSameSize(const std::string &sourcePath,
                                                           std::string_view relativePath) const {
        std::lock_guard<std::mutex> lock(mMutex);
        std::string symlinkPath = ToSymlinkPath(relativePath);
        std::error_code ec;

        if (std::filesystem::exists(symlinkPath, ec) && !ec) {
            if (FileSizeEqual(sourcePath, symlinkPath)) {
                SLOG_INFO << "SymlinkManager: data2 file same size, reuse, path=" << symlinkPath;
            } else {
                std::filesystem::remove(symlinkPath, ec);
                if (ec) {
                    SLOG_ERROR << "SymlinkManager: remove stale data2 file failed, path=" << symlinkPath
                               << " error=" << ec.message();
                    return {};
                }
                if (!CopyToSymlinkUnsafe(sourcePath, relativePath)) {
                    return {};
                }
            }
        } else {
            if (!CopyToSymlinkUnsafe(sourcePath, relativePath)) {
                return {};
            }
        }

        std::filesystem::remove(sourcePath, ec);
        if (ec) {
            SLOG_WARN << "SymlinkManager: remove source failed, path=" << sourcePath << " error=" << ec.message();
        }
        return symlinkPath;
    }

    std::string SymlinkManager::ResolveData2Path(std::string_view originalPath) const {
        // data2不可用或无映射关系时直接返回原路径
        if (!mData2Available.load(std::memory_order_acquire)) {
            return std::string(originalPath);
        }

        // 仅对 data/ 下的路径尝试重定向
        std::string audioPathStr = MeetingConfig::GetInstance().GetAudioPath();
        std::string symlinkAudioPathStr = MeetingConfig::GetInstance().GetSymlinkAudioPath();
        std::string_view prefix(audioPathStr);
        if (originalPath.substr(0, prefix.size()) != prefix) {
            return std::string(originalPath);
        }

        // 计算 data2 映射路径: data/audio/xxx → data/src/audio/xxx
        std::string data2Path = symlinkAudioPathStr + std::string(originalPath.substr(prefix.size()));

        // data2文件存在则优先返回, 否则回退原路径
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(data2Path), ec) && !ec) {
            return data2Path;
        }
        return std::string(originalPath);
    }

}  // namespace qifeng_ca
