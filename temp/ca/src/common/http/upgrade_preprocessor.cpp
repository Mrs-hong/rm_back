#include <cstdint>
#include <filesystem>
#include <string>

#include "drogon/HttpTypes.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/config/upgrade_config.h"
#include "common/device/disk_check.h"
#include "common/device/sn_check.h"
#include "common/http/http.h"
#include "common/status.h"
#include "internal/recording_manager.h"

namespace qifeng_ca {
    // ===================== 服务升级上传前置处理 =====================

    // 校验升级软件包扩展名合法性(.tar.gz/.tgz/.tar)
    static bool ValidateUpgradePackageFile(const std::string &fileName) {
        std::string lower = fileName;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.size() > 7 && lower.compare(lower.size() - 7, 7, ".tar.gz") == 0) {
            return true;
        }
        if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".tgz") == 0) {
            return true;
        }
        if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".tar") == 0) {
            return true;
        }
        return false;
    }

    // 校验升级sha256清单文件扩展名合法性(.sha256)
    static bool ValidateUpgradeSha256File(const std::string &fileName) {
        std::string lower = fileName;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower.size() > 7 && lower.compare(lower.size() - 7, 7, ".sha256") == 0;
    }

    // 从multipart获取指定名称的文件, 不存在返回nullptr
    static const drogon::HttpFile* FindFileByName(const drogon::MultiPartParser &parser, const std::string &name) {
        for (const auto &file : parser.getFiles()) {
            if (file.getItemName() == name) {
                return &file;
            }
        }
        return nullptr;
    }

    // 保存升级上传文件(软件包/sha256清单文件)到升级专用临时目录, 返回保存后的绝对路径
    static std::string SaveUpgradeFile(const drogon::HttpFile &file) {
        std::string tmpUpgradeDir = UpgradeConfig::GetInstance().GetUpgradeTmpDir();

        // 确保临时升级目录存在
        if (!std::filesystem::exists(tmpUpgradeDir)) {
            std::error_code ec;
            std::filesystem::create_directories(tmpUpgradeDir, ec);
            if (ec) {
                SLOG_ERROR << "SaveUpgradeFile: create dir failed: " << tmpUpgradeDir << ", err=" << ec.message();
                return "";
            }
        }

        // 生成唯一文件名: 时间戳_原始文件名
        std::string relativePath = tmpUpgradeDir + "/" + std::to_string(GetTimeMs()) + "_" + file.getFileName();
        // 转为绝对路径: saveAs相对路径会被拼上 upload_path 前缀, 此处显式用绝对路径避免偏差
        std::string savePath = std::filesystem::absolute(relativePath).string();
        SLOG_INFO << "SaveUpgradeFile: savePath=" << savePath;
        if (file.saveAs(savePath) != 0) {
            SLOG_ERROR << "SaveUpgradeFile: save failed: " << savePath;
            return "";
        }
        return savePath;
    }

    // 服务升级前置处理: 解析multipart请求, 校验并保存 软件包 与 sha256清单文件
    // 上传两项: package(软件包.tar.gz/.tgz/.tar) + sha256(清单文件.sha256)
    // 将各文件保存路径和accountId写入req, 供后续业务层使用
    Status BmsPreUploadUpgradeReq(const drogon::HttpRequestPtr &httpReq, UpgradeRequest &req) {
        // 设置accountId(来自JWT)
        uint64_t accountId = httpReq->attributes()->get<uint64_t>("accountId");
        req.set_account_id(accountId);

        // 前期开放 Web 上传升级方式, 跳过 SN 白名单校验, 任意设备均可上传升级包
        // TODO: 后期接入正式 SN 校验后恢复此处 SnCheck 流程

        // 会议进行中检查: 升级会杀进程导致录音数据丢失, 会议中直接拒绝, 不保存文件
        if (RecordingManager::GetInstance().IsRecording()) {
            SLOG_WARN << "BmsPreUploadUpgradeReq: recording in progress, reject upgrade upload";
            return Status {-1, "会议进行中, 请先停止会议"};
        }

        // 检查Content-Type为multipart/form-data
        std::string contentType = httpReq->getHeader("Content-Type");
        if (contentType.size() < 19 || contentType.substr(0, 19) != "multipart/form-data") {
            return Status {-1, "请求类型错误, 需要multipart/form-data"};
        }

        // 解析multipart请求
        drogon::MultiPartParser parser;
        if (parser.parse(httpReq) != 0) {
            return Status {-1, "解析上传请求失败"};
        }

        // 校验必传文件: package(软件包) + sha256(清单文件)
        const auto* packageFile = FindFileByName(parser, "package");
        if (packageFile == nullptr) {
            return Status {-1, "缺少必传文件package(软件包)"};
        }
        const auto* sha256File = FindFileByName(parser, "sha256");
        if (sha256File == nullptr) {
            return Status {-1, "缺少必传文件sha256(哈希清单文件)"};
        }

        // 校验文件扩展名
        if (!ValidateUpgradePackageFile(packageFile->getFileName())) {
            return Status {-1, "软件包格式不支持, 仅支持.tar.gz/.tgz/.tar: " + packageFile->getFileName()};
        }
        if (!ValidateUpgradeSha256File(sha256File->getFileName())) {
            return Status {-1, "sha256文件格式不支持, 仅支持.sha256: " + sha256File->getFileName()};
        }
        int64_t totalFileSize =
            static_cast<int64_t>(packageFile->fileLength()) + static_cast<int64_t>(sha256File->fileLength());
        Status diskStatus = DiskCheck::GetInstance().IsDiskLow(accountId, totalFileSize);
        if (!diskStatus.IsSuccess()) {
            return diskStatus;
        }

        // 保存软件包到升级目录
        std::string packagePath = SaveUpgradeFile(*packageFile);
        if (packagePath.empty()) {
            return Status {-1, "保存软件包失败"};
        }

        // 保存sha256清单文件到升级目录, 失败时回滚已保存的软件包
        std::string sha256Path = SaveUpgradeFile(*sha256File);
        if (sha256Path.empty()) {
            std::error_code ec;
            std::filesystem::remove(packagePath, ec);
            return Status {-1, "保存sha256清单文件失败"};
        }

        req.set_package_path(packagePath);
        req.set_sha256_path(sha256Path);
        SLOG_INFO << "BmsPreUploadUpgradeReq: package=" << packagePath << ", sha256=" << sha256Path;
        return {};
    }
}  // namespace qifeng_ca