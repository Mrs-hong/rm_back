//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/atomic_write_file.h"
#include "core/upgrade/upgrade_package_deployer.h"

namespace qifeng_ca {

    // ===================== 文件系统辅助 =====================

    namespace {

        // CA组件名称子串模式(用于规范化目录名, 确保 qf_scmc 能识别)
        constexpr std::array<const char*, 4> CaComponentPatterns = {"qifeng_ca", "qifeng-ca", "model", "ngin"};

        // metadata.json 文件名
        constexpr const char* MetadataFileName = "metadata.json";

        // 规范化CA组件目录名: 去掉匹配模式前的前缀
        // 例如 "01_qifeng_ca_v2" → "qifeng_ca_v2", 确保 qf_scmc 能按名称扫描到组件
        std::string NormalizeCaDirName(const std::string &dirName) {
            size_t earliest = std::string::npos;
            for (const char* pattern : CaComponentPatterns) {
                size_t pos = dirName.find(pattern);
                if (pos != std::string::npos && (earliest == std::string::npos || pos < earliest)) {
                    earliest = pos;
                }
            }
            if (earliest == std::string::npos || earliest == 0) {
                return dirName;
            }
            return dirName.substr(earliest);
        }

        // 获取目录下的顶级子目录列表(排除隐藏目录)
        std::vector<std::string> ListTopLevelDirs(const std::string &dirPath) {
            std::vector<std::string> dirs;
            std::error_code ec;
            for (const auto &entry : std::filesystem::directory_iterator(dirPath, ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_directory(ec)) {
                    continue;
                }
                std::string name = entry.path().filename().string();
                if (name.empty() || name[0] == '.') {
                    continue;
                }
                dirs.push_back(name);
            }
            return dirs;
        }

        bool RemoveDirRecursive(const std::string &dirPath) {
            std::error_code ec;
            if (!std::filesystem::exists(dirPath, ec)) {
                return true;
            }
            std::filesystem::remove_all(dirPath, ec);
            if (ec) {
                SLOG_ERROR << "RemoveDirRecursive: failed, path=" << dirPath << ", err=" << ec.message();
                return false;
            }
            return true;
        }

        // 解压tar包到指定目录, 返回解压目标目录路径, 失败返回空串
        std::string ExtractTarToDir(const std::string &tarPath, const std::string &destDir) {
            std::string lower = tarPath;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::string tarFlag;
            if ((lower.size() > 7 && lower.compare(lower.size() - 7, 7, ".tar.gz") == 0) ||
                (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".tgz") == 0)) {
                tarFlag = "-xzf";
            } else if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".tar") == 0) {
                tarFlag = "-xf";
            } else {
                SLOG_ERROR << "ExtractTarToDir: unsupported extension: " << tarPath;
                return "";
            }

            std::string cmd = "tar " + tarFlag + " \"" + tarPath + "\" -C \"" + destDir + "\"";
            SLOG_INFO << "ExtractTarToDir: exec: " << cmd;
            int ret = std::system(cmd.c_str());
            if (ret != 0) {
                SLOG_ERROR << "ExtractTarToDir: tar failed, ret=" << ret;
                return "";
            }
            SLOG_INFO << "ExtractTarToDir: ok, path=" << destDir;
            return destDir;
        }

        // 跨文件系统移动目录: 优先 rename, 跨设备时回退 copy + remove
        bool MoveDirectory(const std::string &src, const std::string &dst, std::error_code &ec) {
            std::filesystem::rename(src, dst, ec);
            if (!ec) {
                return true;
            }
            if (ec.value() != EXDEV) {
                return false;
            }
            ec.clear();
            std::filesystem::copy(src, dst, std::filesystem::copy_options::recursive, ec);
            if (ec) {
                return false;
            }
            std::filesystem::remove_all(src, ec);
            return true;
        }

        // 移动单个组件目录到升级目录(若目标已存在则先删除)
        Status MoveComponentDir(const std::string &srcPath, const std::string &dstPath) {
            std::error_code ec;
            if (std::filesystem::exists(dstPath, ec)) {
                SLOG_INFO << "MoveComponentDir: remove existing: " << dstPath;
                std::filesystem::remove_all(dstPath, ec);
            }
            if (!MoveDirectory(srcPath, dstPath, ec)) {
                SLOG_ERROR << "MoveComponentDir: move failed, src=" << srcPath << ", dst=" << dstPath
                           << ", err=" << ec.message();
                return Status {-1, "移动组件目录失败: " + ec.message()};
            }
            SLOG_INFO << "MoveComponentDir: moved " << srcPath << " -> " << dstPath;
            return {};
        }

        // 清除升级目录下的残留组件文件, 保留 tmp 子目录(上传临时)
        // ota_tmp 与 soft_dir 平级(不在 targetDir 内), 不受清理影响
        void CleanUpgradeDir(const std::string &targetDir) {
            std::error_code ec;
            if (!std::filesystem::exists(targetDir, ec)) {
                return;
            }
            SLOG_INFO << "CleanUpgradeDir: clean old upgrade files in " << targetDir;
            for (const auto &entry : std::filesystem::directory_iterator(targetDir, ec)) {
                if (ec) {
                    break;
                }
                std::string name = entry.path().filename().string();
                if (name == "tmp") {
                    continue;  // 保留 tmp 子目录
                }
                if (!name.empty() && name[0] == '.') {
                    continue;  // 跳过隐藏文件
                }
                std::filesystem::remove_all(entry.path(), ec);
                if (ec) {
                    SLOG_WARN << "CleanUpgradeDir: remove failed: " << entry.path() << ", err=" << ec.message();
                    ec.clear();
                }
            }
        }

        // 在目录中查找 .deb 文件(仅扩展名匹配, metadata 已确认组件类型)
        std::string FindDebFile(const std::string &dirPath) {
            std::error_code ec;
            for (const auto &entry : std::filesystem::directory_iterator(dirPath, ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file(ec)) {
                    continue;
                }
                std::string name = entry.path().filename().string();
                if (name.size() > 4 && name.compare(name.size() - 4, 4, ".deb") == 0) {
                    return entry.path().string();
                }
            }
            return "";
        }

        // ===================== metadata 驱动部署 =====================

        // 按 metadata 移动组件目录到升级目录
        // 对每个存在的组件, 从 innerDir/component.dir 移动到 targetDir
        Status DeployByMetadata(const std::string &innerDir, const PackageMetadata &metadata,
                                const std::string &targetDir) {
            // screen 组件 (保持目录名不变)
            if (metadata.screen) {
                std::string src = innerDir + "/" + metadata.screen->dir;
                std::string dirName = std::filesystem::path(src).filename().string();
                Status s = MoveComponentDir(src, targetDir + "/" + dirName);
                if (!s.IsSuccess()) {
                    return s;
                }
                SLOG_INFO << "DeployByMetadata: moved screen from " << metadata.screen->dir;
            }

            // CA 组件 (qifeng_ca, 规范化目录名确保 qf_scmc 可识别)
            if (metadata.qifengCa) {
                std::string src = innerDir + "/" + metadata.qifengCa->dir;
                std::string dirName = NormalizeCaDirName(std::filesystem::path(src).filename().string());
                Status s = MoveComponentDir(src, targetDir + "/" + dirName);
                if (!s.IsSuccess()) {
                    return s;
                }
                SLOG_INFO << "DeployByMetadata: moved qifeng_ca from " << metadata.qifengCa->dir;
            }

            // model 组件 (归入 CA 组件范畴, qf_scmc 会扫描)
            if (metadata.model) {
                std::string src = innerDir + "/" + metadata.model->dir;
                std::string dirName = NormalizeCaDirName(std::filesystem::path(src).filename().string());
                Status s = MoveComponentDir(src, targetDir + "/" + dirName);
                if (!s.IsSuccess()) {
                    return s;
                }
                SLOG_INFO << "DeployByMetadata: moved model from " << metadata.model->dir;
            }

            // nginx 组件 (归入 CA 组件范畴)
            if (metadata.nginx) {
                std::string src = innerDir + "/" + metadata.nginx->dir;
                std::string dirName = NormalizeCaDirName(std::filesystem::path(src).filename().string());
                Status s = MoveComponentDir(src, targetDir + "/" + dirName);
                if (!s.IsSuccess()) {
                    return s;
                }
                SLOG_INFO << "DeployByMetadata: moved nginx from " << metadata.nginx->dir;
            }

            // SCM deb 组件 (metadata.dir 即为组件包: 可能是 .deb 文件本身, 也可能是含 .deb 的目录)
            if (metadata.qifengScm) {
                std::string scmPath = innerDir + "/" + metadata.qifengScm->dir;
                std::string debPath;
                std::error_code ec;
                // dir 直接指向 .deb 文件: 直接使用(dir 值即为组件包, 非目录)
                if (std::filesystem::is_regular_file(scmPath, ec) &&
                    scmPath.size() > 4 && scmPath.compare(scmPath.size() - 4, 4, ".deb") == 0) {
                    debPath = scmPath;
                } else if (std::filesystem::is_directory(scmPath, ec)) {
                    // dir 指向目录: 在目录下查找 .deb 文件(兼容目录形态的包结构)
                    debPath = FindDebFile(scmPath);
                }
                if (debPath.empty()) {
                    return Status {-1, "metadata 指定的 qifeng_scm 组件无效(非 .deb 文件且目录下无 .deb): " + scmPath};
                }
                std::string fileName = std::filesystem::path(debPath).filename().string();
                std::string dstPath = targetDir + "/" + fileName;
                if (std::filesystem::exists(dstPath, ec)) {
                    std::filesystem::remove(dstPath, ec);
                }
                std::filesystem::rename(debPath, dstPath, ec);
                if (ec) {
                    return Status {-1, "移动deb文件失败: " + ec.message()};
                }
                SLOG_INFO << "DeployByMetadata: moved scm deb: " << fileName;
            }

            return {};
        }

    }  // namespace

    // ===================== 公开接口实现 =====================

    Status UpgradePackageDeployer::Deploy(const std::string &packagePath, const std::string &targetDir,
                                          PackageMetadata *outMetadata) {
        SLOG_INFO << "Deployer::Deploy: package=" << packagePath << ", targetDir=" << targetDir;

        if (outMetadata) {
            *outMetadata = {};
        }

        // 1. 清除上一次升级残留(保留tmp子目录)
        CleanUpgradeDir(targetDir);

        // 2. 确保目标升级目录存在
        std::error_code ec;
        std::filesystem::create_directories(targetDir, ec);
        if (ec) {
            SLOG_ERROR << "Deployer::Deploy: create target dir failed: " << targetDir;
            return Status {-1, "创建升级目录失败"};
        }

        // 3. 创建临时解压目录(隐藏目录, 确保与目标同文件系统)
        std::string tempExtractDir = targetDir + "/.extract_" + std::to_string(GetTimeMs());
        if (!std::filesystem::create_directories(tempExtractDir, ec)) {
            SLOG_ERROR << "Deployer::Deploy: create temp dir failed: " << tempExtractDir;
            return Status {-1, "创建临时解压目录失败"};
        }

        // 4. 解压tar包到临时目录
        if (ExtractTarToDir(packagePath, tempExtractDir).empty()) {
            RemoveDirRecursive(tempExtractDir);
            return Status {-1, "解压软件包失败"};
        }

        // 5. 包结构校验: 解压后顶层必须有且仅有一个外层目录
        std::vector<std::string> topDirs = ListTopLevelDirs(tempExtractDir);
        if (topDirs.size() != 1) {
            RemoveDirRecursive(tempExtractDir);
            return Status {-1, "升级包结构不合法: 需有且仅有一个外层目录"};
        }

        // 6. 进入外层目录(自动去除外层目录)
        std::string innerDir = tempExtractDir + "/" + topDirs[0];
        SLOG_INFO << "Deployer::Deploy: unwrap outer dir: " << topDirs[0];

        // 7. 解析 metadata.json (设计文档规定为必须, 不存在或无效则部署失败)
        std::string metadataPath = innerDir + "/" + MetadataFileName;
        PackageMetadata metadata;
        Status metaStatus = PackageMetadata::ParseFromFile(metadataPath, metadata);
        if (!metaStatus.IsSuccess()) {
            RemoveDirRecursive(tempExtractDir);
            return Status {-1, "升级包缺少 metadata.json 或解析失败: " + metaStatus.GetMsg()};
        }
        if (!metadata.HasAnyComponent()) {
            RemoveDirRecursive(tempExtractDir);
            return Status {-1, "metadata.json 未包含任何可升级组件"};
        }
        SLOG_INFO << "Deployer::Deploy: metadata.json parsed, version=" << metadata.version;
        if (outMetadata) {
            *outMetadata = metadata;
        }

        // 8. 按 metadata 精确部署组件到升级目录
        Status deployStatus = DeployByMetadata(innerDir, metadata, targetDir);

        // 9. 清理临时解压目录
        RemoveDirRecursive(tempExtractDir);

        // 部署失败: 清理已部分移动到 targetDir 的组件, 避免残留影响下次升级或被误识别
        if (!deployStatus.IsSuccess()) {
            SLOG_WARN << "Deployer::Deploy: deploy failed, clean partial components in " << targetDir;
            CleanUpgradeDir(targetDir);
        }

        return deployStatus;
    }

    UpgradePackageDeployer::DeployedComponents
    UpgradePackageDeployer::IdentifyComponents(const std::string &targetDir, const PackageMetadata &metadata) {
        DeployedComponents result;

        // screen 组件: 从 metadata.dir 推导部署后的完整路径
        if (metadata.screen) {
            std::string dirName = std::filesystem::path(metadata.screen->dir).filename().string();
            result.screenDir = targetDir + "/" + dirName;
        }

        // CA 组件: qifeng_ca/model/nginx 任一存在即标记
        result.hasCa = metadata.qifengCa.has_value() || metadata.model.has_value() || metadata.nginx.has_value();

        // SCM deb 组件: 在升级目录中查找 .deb 文件路径
        if (metadata.qifengScm) {
            result.debFile = FindDebFile(targetDir);
        }

        SLOG_INFO << "IdentifyComponents: hasScreen=" << !result.screenDir.empty()
                  << ", hasCa=" << result.hasCa << ", hasDeb=" << !result.debFile.empty();
        return result;
    }

}  // namespace qifeng_ca
