//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "qifeng_framework/common/logger.h"

#include "core/upgrade/pkg_usb_detector.h"

namespace qifeng_ca {

    // 升级包文件名匹配模式: qifeng_*_upgrade_*.tar.gz / .tgz / .tar
    static bool IsUpgradePackageFile(const std::string &fileName) {
        std::string lower = fileName;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("qifeng_") == std::string::npos || lower.find("_upgrade_") == std::string::npos) {
            return false;
        }
        return lower.size() > 7 && lower.compare(lower.size() - 7, 7, ".tar.gz") == 0;
    }

    // 升级包对应的 sha256 清单文件名: 包名 + ".sha256"
    static std::string Sha256FileNameFor(const std::string &pkgFileName) {
        return pkgFileName + ".sha256";
    }

    // 从 /proc/mounts 读取 USB 设备挂载点列表
    // 匹配设备名 /dev/sd[b-z]\d* (USB 大容量存储, 排除 sda 系统盘)
    static std::vector<std::string> ListUsbMountPoints() {
        std::vector<std::string> mountPoints;
        std::ifstream mounts("/proc/mounts");
        if (!mounts.is_open()) {
            SLOG_WARN << "PkgUsbDetector: open /proc/mounts failed";
            return mountPoints;
        }
        // 设备名匹配: /dev/sd[b-z] 后跟可选数字
        std::regex devPattern(R"(/dev/sd[b-z]\d*)");
        std::string line;
        while (std::getline(mounts, line)) {
            std::istringstream iss(line);
            std::string dev, mountPoint, fsType;
            iss >> dev >> mountPoint >> fsType;
            if (dev.empty() || mountPoint.empty()) {
                continue;
            }
            if (std::regex_match(dev, devPattern)) {
                SLOG_INFO << "PkgUsbDetector: usb mount, dev=" << dev << ", point=" << mountPoint << ", fs=" << fsType;
                mountPoints.push_back(mountPoint);
            }
        }
        return mountPoints;
    }

    PkgUsbDetector::PkgUsbDetector(std::string currentVersion, std::string savePath)
        : UpgradeDetectorBase(std::move(currentVersion), std::move(savePath)) {
    }

    std::vector<PackageInfo> PkgUsbDetector::FindUpgradePackage() {
        std::vector<PackageInfo> candidates;
        auto mountPoints = ListUsbMountPoints();
        if (mountPoints.empty()) {
            SLOG_DEBUG << "PkgUsbDetector: no usb mount point found";
            return candidates;
        }

        for (const auto &mp : mountPoints) {
            std::error_code ec;
            if (!std::filesystem::exists(mp, ec)) {
                continue;
            }
            // 仅扫描挂载点根目录(非递归), 查找升级包文件
            for (auto it = std::filesystem::directory_iterator(mp, ec); it != std::filesystem::directory_iterator();
                 it.increment(ec)) {
                if (ec) {
                    SLOG_WARN << "PkgUsbDetector: iterate error at " << mp << ", err=" << ec.message();
                    break;
                }
                if (!it->is_regular_file(ec)) {
                    continue;
                }
                std::string fileName = it->path().filename().string();
                if (!IsUpgradePackageFile(fileName)) {
                    continue;
                }
                std::string pkgPath = it->path().string();
                std::string shaPath = pkgPath + ".sha256";
                // 校验 sha256 文件存在
                if (!std::filesystem::exists(shaPath, ec)) {
                    SLOG_WARN << "PkgUsbDetector: sha256 file missing, skip, pkg=" << pkgPath;
                    continue;
                }
                std::string version = ParseVersionFromFileName(fileName);
                if (version.empty()) {
                    SLOG_WARN << "PkgUsbDetector: parse version failed, skip, file=" << fileName;
                    continue;
                }
                PackageInfo info;
                info.source = "usb";
                info.package_name = fileName;
                info.package_version = version;
                info.package_path = pkgPath;  // Find 阶段为 USB 上的原始路径
                info.sha256_path = shaPath;
                candidates.push_back(std::move(info));
                SLOG_INFO << "PkgUsbDetector: found package, version=" << version << ", path=" << pkgPath;
            }
        }

        SLOG_INFO << "PkgUsbDetector: found " << candidates.size() << " packages on usb";
        return candidates;
    }

    Status PkgUsbDetector::PrepareUpgradePackage(const PackageInfo &candidate, PackageInfo &out) {
        out = candidate;
        out.source = "usb";

        // 确保保存目录存在
        std::error_code ec;
        std::filesystem::create_directories(mSavePath, ec);
        if (ec) {
            SLOG_ERROR << "PkgUsbDetector: create save dir failed, dir=" << mSavePath << ", err=" << ec.message();
            return Status {-1, "创建拷贝目录失败: " + ec.message()};
        }

        std::string localPkgPath = mSavePath + "/" + candidate.package_name;
        std::string localShaPath = mSavePath + "/" + Sha256FileNameFor(candidate.package_name);

        SLOG_INFO << "PkgUsbDetector: copying package, " << candidate.package_path << " -> " << localPkgPath;
        // 拷贝升级包(覆盖已有文件)
        std::filesystem::copy_file(candidate.package_path, localPkgPath,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            SLOG_ERROR << "PkgUsbDetector: copy package failed, err=" << ec.message();
            return Status {-1, "拷贝升级包失败: " + ec.message()};
        }

        // 拷贝 sha256 清单文件
        std::filesystem::copy_file(candidate.sha256_path, localShaPath,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            SLOG_ERROR << "PkgUsbDetector: copy sha256 failed, err=" << ec.message();
            std::filesystem::remove(localPkgPath, ec);
            return Status {-1, "拷贝sha256清单文件失败: " + ec.message()};
        }

        out.package_path = localPkgPath;
        out.sha256_path = localShaPath;
        SLOG_INFO << "PkgUsbDetector: copy ok, pkg=" << localPkgPath << ", sha=" << localShaPath;
        return {};
    }

}  // namespace qifeng_ca
