/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/user.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <pwd.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace qifeng::scm::utils {
    /**
     * @brief 检查用户是否为 root 用户
     * @return bool 是否为 root 用户
     */
    bool IsRootUser() {
        return geteuid() == 0;
    }

    /**
     * @brief 检查用户是否存在
     * @param user 用户名
     * @return bool 是否存在
     */
    bool ExistUser(const std::string &user) {
        return getpwnam(user.c_str()) != nullptr;
    }

    /**
     * @brief 获取当前用户
     * @return std::string 当前用户
     */
    std::string GetCurrentUserName() {
        struct passwd* pw = getpwuid(geteuid());
        if (pw != nullptr) {
            return std::string(pw->pw_name);
        }
        return "";
    }

    /**
     * @brief 设置文件（目录或者文件）权限，完全归属指定用户和 root 组
     * @param path 文件或目录路径
     * @param user 目标用户名
     * @param mode 文件权限模式（如 0750），默认为 DefaultMode
     * @return ResultMsg 操作结果
     */
    ResultMsg SetFilePermission(const std::string &path, const std::string &user, int mode) {
        namespace fs = std::filesystem;
        if (!fs::exists(path)) {
            return MakeError("Path does not exist: " + path);
        }

        struct passwd* pw = getpwnam(user.c_str());
        if (pw == nullptr) {
            return MakeError("User does not exist: " + user);
        }

        // root 组的 gid 通常为 0，此处通过查询 root 用户获取其主组 gid
        gid_t rootGid = 0;
        struct passwd* rootPw = getpwnam("root");
        if (rootPw != nullptr) {
            rootGid = rootPw->pw_gid;
        }

        if (chown(path.c_str(), pw->pw_uid, rootGid) != 0) {
            return MakeError("Failed to chown " + path + " to " + user + ": " + strerror(errno));
        }

        if (chmod(path.c_str(), static_cast<mode_t>(mode)) != 0) {
            return MakeError("Failed to chmod " + path + ": " + strerror(errno));
        }

        return MakeSuccess();
    }
}  // namespace qifeng::scm::utils
