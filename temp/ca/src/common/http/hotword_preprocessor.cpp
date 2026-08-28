#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>

#include "drogon/HttpTypes.h"
#include "qifeng_framework/common/logger.h"

#include "common/config/tmp_path_config.h"
#include "common/http/http.h"
#include "common/status.h"
#include "common/utils/file_name_generator.h"

namespace qifeng_ca {

    // 删除已保存的临时文件列表(出错时回滚用)
    static void CleanupTempFiles(const std::vector<std::string> &filePaths) {
        for (const auto &path : filePaths) {
            if (!path.empty() && std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
        }
    }

    // 校验上传热词Excel文件的扩展名
    static bool ValidateUploadHotwordFile(const drogon::HttpFile &file) {
        static const std::unordered_set<std::string> AllowedExt = {"xlsx", "xls"};

        std::string fileName = file.getFileName();
        size_t dotPos = fileName.find_last_of('.');
        if (dotPos == std::string::npos) {
            return false;
        }

        std::string ext = fileName.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return AllowedExt.find(ext) != AllowedExt.end();
    }

    // 保存热词上传文件到临时目录, 返回保存后的绝对路径
    static std::string SaveHotwordUploadToTemp(const drogon::HttpFile &file) {
        auto &cfg = TmpPathConfig::GetInstance();
        std::string tempDir = cfg.GetHotwordUploadTmpPath();

        // 确保临时目录存在
        if (!std::filesystem::exists(tempDir)) {
            std::error_code ec;
            std::filesystem::create_directories(tempDir, ec);
            if (ec) {
                return "";
            }
        }

        // 生成唯一文件名
        std::string uniqueName = FileNameGenerator::GenFile("hotword", ".xlsx");
        std::string filePath = tempDir + uniqueName;

        SLOG_INFO << "SaveHotwordUploadToTemp: filePath=" << filePath;
        int saveResult = file.saveAs(filePath);
        if (saveResult != 0) {
            return "";
        }
        return filePath;
    }

    // 校验并保存所有热词上传文件
    static Status ValidateAndSaveHotwordFiles(const drogon::MultiPartParser &parser,
                                              std::vector<std::string> &savedPaths) {
        const auto &files = parser.getFiles();
        if (files.empty()) {
            return Status {-1, "未上传文件"};
        }

        // 校验所有文件格式
        for (const auto &file : files) {
            if (!ValidateUploadHotwordFile(file)) {
                return Status {-1, "文件格式不支持, 仅支持xlsx/xls: " + file.getFileName()};
            }
        }

        // 保存所有文件到临时目录
        auto &cfg = TmpPathConfig::GetInstance();
        for (const auto &file : files) {
            // TIPS: 实际路径都是相对drogon配置中tmp_path，所以下面还要拼接GetDrogonTmpPath
            std::string filePath = cfg.GetDrogonTmpPath() + "/" + SaveHotwordUploadToTemp(file);
            if (filePath.empty()) {
                CleanupTempFiles(savedPaths);
                return Status {-1, "保存上传文件失败: " + file.getFileName()};
            }
            savedPaths.push_back(filePath);
        }
        return {};
    }

    // 导入热词文件前置处理: 解析multipart请求, 校验Excel文件格式, 保存到临时目录
    // 将文件路径列表(file_paths)和accountId写入req, 供后续业务层使用
    Status BmsPreUploadHotwordFileReq(const drogon::HttpRequestPtr &httpReq, HotwordImportRequest &req) {
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

        // 校验并保存所有Excel文件到临时目录
        std::vector<std::string> savedPaths;
        Status saveStatus = ValidateAndSaveHotwordFiles(parser, savedPaths);
        if (saveStatus.GetCode() != 0) {
            return saveStatus;
        }

        // 将所有文件路径写入req
        for (const auto &path : savedPaths) {
            req.add_file_paths(path);
        }

        // 从multipart参数中填充文件名
        const auto &params = parser.getParameters();
        if (params.find("filename") != params.end()) {
            req.set_filename(params.at("filename"));
        }
        return {};
    }
}  // namespace qifeng_ca