//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_UTILS_SYMLINK_MANAGER_H
#define QIFENG_CA_INCLUDE_COMMON_UTILS_SYMLINK_MANAGER_H

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace qifeng_ca {

    // 管理 /data2 SSD 磁盘检测与 data/src -> /data2/qifeng_ca 软连接
    // 提供软连接的创建、检测、文件读写等封装操作
    // 作为统一文件管理模块(FileOpt + SymlinkManager)的一部分
    class SymlinkManager {
    public:
        static SymlinkManager &GetInstance();

        // 初始化: 检测 /data2 是否存在, 若存在则创建软连接
        // symlinkPath: data/src (相对路径, 相对于进程工作目录)
        // targetPath: /data2/qifeng_ca
        bool Init(std::string_view symlinkPath = "data/src", std::string_view targetPath = "/data2/qifeng_ca");

        // 检测 /data2 磁盘是否存在
        bool IsData2Available() const;

        // 获取软连接完整路径
        std::string GetSymlinkPath() const;

        // 获取目标完整路径
        std::string GetTargetPath() const;

        // 将原始路径(data下路径)转换为软连接路径
        // 如: data/audio/{aid}/{aid}.wav -> data/src/audio/{aid}/{aid}.wav
        std::string ToSymlinkPath(std::string_view originalPath) const;

        // 检测软连接是否存在且有效
        bool IsSymlinkValid() const;

        // 检测软连接下指定文件是否存在
        // relativePath: 相对 data/ 目录的路径, 如 "data/audio/{accountId}/{audioId}.wav"
        bool SymlinkFileExists(std::string_view relativePath) const;

        // 删除软连接下的文件
        // relativePath: 相对 data/ 目录的路径, 如 "data/audio/{accountId}/{audioId}.wav"
        bool RemoveSymlinkFile(std::string_view relativePath) const;

        // 拷贝文件到软连接目录(保持目录结构)
        // sourcePath: 源文件绝对路径
        // relativePath: 相对 data/ 目录的路径, 如 "data/audio/{accountId}/{audioId}.wav"
        bool CopyToSymlink(const std::string &sourcePath, std::string_view relativePath) const;

        // 将data下文件迁移至软连接: copy + verify + remove source + update db
        // 返回目标文件路径
        // sourcePath: 源文件绝对路径
        // relativePath: 相对 data/ 目录的路径, 如 "data/audio/{accountId}/{audioId}.wav"
        std::string MigrateToSymlink(const std::string &sourcePath, std::string_view relativePath) const;

        // 将data下文件迁移至软连接, 优先复用data2中大小一致的文件
        // 若data2文件已存在且与data文件大小一致, 直接复用; 否则删除data2后重新复制
        // sourcePath: 源文件绝对路径
        // relativePath: 相对 data/ 目录的路径, 如 "data/audio/{accountId}/{audioId}.wav"
        std::string MigrateToSymlinkIfSameSize(const std::string &sourcePath, std::string_view relativePath) const;

        // 路径重定向: 若data2可用且映射文件存在则优先返回data2路径, 否则返回默认路径
        // 用于文件访问入口, 封装"优先data2, data2不存在则回退data"的选择策略
        // 如: "data/audio/{aid}/{aid}.wav" → "data/src/audio/{aid}/{aid}.wav" (data2文件存在时)
        std::string ResolveData2Path(std::string_view originalPath) const;

    private:
        SymlinkManager() = default;
        ~SymlinkManager() = default;
        SymlinkManager(const SymlinkManager &) = delete;
        SymlinkManager &operator=(const SymlinkManager &) = delete;
        SymlinkManager(SymlinkManager &&) = delete;
        SymlinkManager &operator=(SymlinkManager &&) = delete;

        // 无锁版: 调用方已持有mMutex时使用, 避免死锁
        bool CopyToSymlinkUnsafe(const std::string &sourcePath, std::string_view relativePath) const;

        mutable std::mutex mMutex;
        std::string mSymlinkPath;  // 如: data/src
        std::string mTargetPath;   // 如: /data2/qifeng_ca
        std::atomic<bool> mInitialized {false};
        std::atomic<bool> mData2Available {false};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_UTILS_SYMLINK_MANAGER_H
