#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>

#include "drogon/HttpTypes.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/config/meeting_config.h"
#include "common/config/tmp_path_config.h"
#include "common/device/disk_check.h"
#include "common/http/http.h"
#include "common/status.h"

namespace qifeng_ca {

    // 校验上传音频文件的扩展名和大小
    static bool ValidateUploadAudioFile(const drogon::HttpFile &file) {
        // 允许的音频文件扩展名
        static const std::unordered_set<std::string> AllowedExt = {"wav", "mp3"};

        std::string fileName = file.getFileName();
        size_t dotPos = fileName.find_last_of('.');
        if (dotPos == std::string::npos) {
            return false;
        }

        std::string ext = fileName.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        return !(AllowedExt.find(ext) == AllowedExt.end());
    }

    // 保存上传文件到临时目录, 返回保存后的绝对路径
    static std::string SaveUploadToTemp(const drogon::HttpFile &file) {
        auto &cfg = TmpPathConfig::GetInstance();
        std::string tempDir = cfg.GetAudioUploadTmpPath();

        // 确保临时目录存在
        if (!std::filesystem::exists(tempDir)) {
            std::error_code ec;
            std::filesystem::create_directories(tempDir, ec);
            if (ec) {
                return "";
            }
        }

        // 生成唯一文件名: 时间戳_原始文件名, 避免同名文件覆盖
        std::string filePath = tempDir + std::to_string(GetTimeMs()) + "_" + file.getFileName();
        SLOG_INFO << "SaveUploadToTemp: filePath=" << filePath;

        int saveResult = file.saveAs(filePath);
        if (saveResult != 0) {
            return "";
        }

        return filePath;
    }

    // 从multipart参数中填充AddRecordingRequest字段
    static void FillUploadRequestFromParams(const drogon::SafeStringMap<std::string> &params,  // NOLINT
                                            AddRecordingRequest &req) {
        if (params.find("theme") != params.end()) {
            req.set_theme(params.at("theme"));
        }
        if (params.find("recordingTime") != params.end()) {
            try {
                req.set_recording_time(std::stoll(params.at("recordingTime")));
            } catch (...) {  // NOLINT
                req.set_recording_time(0);
            }
        }
        if (params.find("kind") != params.end()) {
            try {
                req.set_kind(std::stoi(params.at("kind")));
            } catch (...) {  // NOLINT
                req.set_kind(0);
            }
        }
        if (params.find("moderator") != params.end()) {
            req.set_moderator(params.at("moderator"));
        }
        if (params.find("attendees") != params.end()) {
            req.set_attendees(params.at("attendees"));
        }
        if (params.find("places") != params.end()) {
            req.set_places(params.at("places"));
        }
        if (params.find("remark") != params.end()) {
            req.set_remark(params.at("remark"));
        }
        if (params.find("fileName") != params.end()) {
            req.set_filename(params.at("fileName"));
        }
        if (params.find("cancel") != params.end()) {
            try {
                req.set_cancel(std::stoi(params.at("cancel")));
            } catch (...) {  // NOLINT
                req.set_cancel(0);
            }
        }
    }

    // 删除已保存的临时文件列表(出错时回滚用)
    static void CleanupTempFiles(const std::vector<std::string> &filePaths) {
        for (const auto &path : filePaths) {
            if (!path.empty() && std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
        }
    }

    static constexpr int64_t MaxPerFileBytes = 2254857830LL;
    // 校验所有上传文件并保存到临时目录, 返回保存路径列表
    // 任一文件校验失败则清理已保存文件并返回错误
    static Status ValidateAndSaveAllFiles(uint64_t accountId, const drogon::MultiPartParser &parser,
                                          std::vector<std::string> &savedPaths) {
        const auto &files = parser.getFiles();
        if (files.empty()) {
            return Status {-1, "未上传文件"};
        }

        // 校验所有文件格式
        int64_t totalSizeMb = 0;
        for (const auto &file : files) {
            if (!ValidateUploadAudioFile(file)) {
                return Status {-1, "文件格式不支持: " + file.getFileName()};
            }
            // 硬限制: 单文件不得超过2.1GB
            if (static_cast<int64_t>(file.fileLength()) > MaxPerFileBytes) {
                SLOG_WARN << "ValidateUploadAudioFile: file exceeds 2.1GB hard limit, size=" << file.fileLength()
                          << ", name=" << file.getFileName();
                return Status {-1, "文件超过2.1GB限制: " + file.getFileName()};
            }
            // 校验上传文件大小, 默认最大5GB (5120MB)
            int64_t maxSizeMb = MeetingConfig::GetInstance().GetMaxUploadFileSizeMb();
            int64_t fileSizeMb = static_cast<int64_t>(file.fileLength()) / (1024LL * 1024);
            totalSizeMb += fileSizeMb;

            if (fileSizeMb > maxSizeMb) {
                SLOG_WARN << "ValidateUploadAudioFile: file too large, size=" << fileSizeMb << "MB, max=" << maxSizeMb
                          << "MB, name=" << file.getFileName();
                return Status {-1, "文件过大: " + file.getFileName()};
            }
        }

        // 校验总上传大小是否超过限制
        int64_t maxTotalSizeMb = MeetingConfig::GetInstance().GetMaxUploadTotalSizeMb();
        if (totalSizeMb > maxTotalSizeMb) {
            return Status {-1, "总上传文件大小超过限制: " + std::to_string(totalSizeMb) +
                                   "MB, max=" + std::to_string(maxTotalSizeMb) + "MB"};
        }

        std::string fileName = "";
        auto params = parser.getParameters();
        if (params.find("fileName") != params.end()) {
            fileName = params.at("fileName");
        }
        SLOG_INFO << "ValidateAndSaveAllFiles: fileName=" << fileName;

        // 预测总上传大小, 提前校验磁盘空间是否足够
        int64_t totalFileSize = 0;
        for (const auto &file : files) {
            totalFileSize += static_cast<int64_t>(file.fileLength());
        }
        Status diskStatus = DiskCheck::GetInstance().IsDiskLow(accountId, totalFileSize);
        if (!diskStatus.IsSuccess()) {
            return diskStatus;
        }

        // 保存所有文件到临时目录
        for (const auto &file : files) {
            std::string filePath = SaveUploadToTemp(file);
            if (filePath.empty()) {
                // 保存失败, 清理已保存的文件
                CleanupTempFiles(savedPaths);
                return Status {-1, "保存上传文件失败: " + file.getFileName()};
            }
            savedPaths.push_back(filePath);
        }
        return {};
    }

    // 上传录音文件前置处理: 解析multipart请求, 校验所有文件, 保存到临时目录
    // 将文件路径列表和accountId写入req, 供后续业务层使用
    Status BmsPreUploadFileReq(const drogon::HttpRequestPtr &httpReq, AddRecordingRequest &req) {
        // 设置accountId
        uint64_t accountId = httpReq->attributes()->get<uint64_t>("accountId");
        req.set_account_id(accountId);

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

        // 校验所有文件并保存到临时目录
        std::vector<std::string> savedPaths;
        Status saveStatus = ValidateAndSaveAllFiles(accountId, parser, savedPaths);
        if (saveStatus.GetCode() != 0) {
            return saveStatus;
        }

        // 将所有文件路径写入req
        auto &cfg = TmpPathConfig::GetInstance();
        // TIPS: 实际路径都是相对drogon配置中tmp_path，所以下面还要拼接GetDrogonTmpPath
        for (const auto &path : savedPaths) {
            req.add_file_paths(cfg.GetDrogonTmpPath() + "/" + path);
        }

        // 从multipart参数中填充业务字段
        FillUploadRequestFromParams(parser.getParameters(), req);

        // 离线上传, is_rel_time固定为false
        req.set_is_rel_time(false);
        SLOG_INFO << "BmsPreUploadFileReq: req=" << req.DebugString();
        return {};
    }

}  // namespace qifeng_ca