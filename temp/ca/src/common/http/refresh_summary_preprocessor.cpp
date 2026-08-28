//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "drogon/HttpTypes.h"
#include "json/value.h"
#include "qifeng_framework/common/logger.h"
#include "utf8/checked.h"

#include "common/config/tmp_path_config.h"
#include "common/http/http.h"
#include "common/utils/file_name_generator.h"
#include "common/utils/file_opt.h"
#include "common/utils/template_file_reader.h"

namespace qifeng_ca {

    // ===================== JSON 辅助 =====================

    static Json::Value ParseJsonString(const std::string &jsonStr) {
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
        if (!reader->parse(jsonStr.c_str(), jsonStr.c_str() + jsonStr.size(), &root, &errs)) {
            return Json::Value();
        }
        return root;
    }

    // ===================== 文件扩展名校验 =====================

    static std::string GetFileExtension(const std::string &fileName) {
        size_t dotPos = fileName.find_last_of('.');
        if (dotPos == std::string::npos) {
            return "";
        }
        std::string ext = fileName.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }

    // ===================== 临时文件Guard =====================

    namespace {
        constexpr int64_t kMaxTemplateFileSize = 512 * 1024;  // 512KB

        // RAII 临时文件清理守卫：确保模板文件在任何退出路径下都被删除
        class TempFileGuard {
        public:
            explicit TempFileGuard(const std::string &path) : mPath(path) {}
            ~TempFileGuard() { Remove(); }

            TempFileGuard(const TempFileGuard &) = delete;
            TempFileGuard &operator=(const TempFileGuard &) = delete;

            TempFileGuard(TempFileGuard &&other) noexcept : mPath(std::move(other.mPath)) {}
            TempFileGuard &operator=(TempFileGuard &&other) noexcept {
                if (this != &other) {
                    Remove();
                    mPath = std::move(other.mPath);
                }
                return *this;
            }

            const std::string &GetPath() const { return mPath; }
            void Release() { mPath.clear(); }

        private:
            void Remove() {
                if (mPath.empty()) {
                    return;
                }
                std::error_code ec;
                std::filesystem::remove(mPath, ec);
                if (ec) {
                    SLOG_WARN << "TempFileGuard: remove failed, path=" << mPath << ", err=" << ec.message();
                }
                mPath.clear();
            }

            std::string mPath;
        };
    }  // namespace

    static std::string SaveTemplateToTemp(const drogon::HttpFile &file) {
        auto &cfg = TmpPathConfig::GetInstance();
        std::string tempDir = cfg.GetSummaryTmpPath();
        if (!FileOpt::CreateDstDirectory(tempDir)) {
            return "";
        }
        std::string ext = GetFileExtension(file.getFileName());
        std::string uniqueName = FileNameGenerator::GenFile("template", "." + ext);
        std::string filePath = tempDir + uniqueName;
        SLOG_INFO << "SaveTemplateToTemp: filePath=" << filePath << ", ext=" << ext << ", uniqueName=" << uniqueName;
        if (file.saveAs(filePath) != 0) {
            return "";
        }
        return filePath;
    }

    // ===================== multipart 参数解析 =====================

    // 议题数量与单条字符数上限(在HTTP前置层做硬性校验, 超限直接返回错误)
    // 上限值通过MeetingConfig可配置: max_topics / max_topic_len

    // 解析 topics 参数并校验
    static Status FillTopicsFromParams(const drogon::SafeStringMap<std::string> &params, RefreshSummaryRequest &req) {
        auto it = params.find("topics");
        if (it == params.end()) {
            return {};
        }
        const std::string &val = it->second;
        if (val.empty()) {
            return {};
        }
        Json::Value arr = ParseJsonString(val);
        if (!arr.isArray()) {
            SLOG_WARN << "FillTopicsFromParams: topics is not a JSON array, val=" << val;
            return Status {-1, "topics参数必须为JSON数组"};
        }
        size_t maxTopics = MeetingConfig::GetInstance().GetMaxTopics();
        if (static_cast<size_t>(arr.size()) > maxTopics) {
            return Status {-1, "议题数量不能超过" + std::to_string(maxTopics) + "条"};
        }
        size_t maxTopicLen = MeetingConfig::GetInstance().GetMaxTopicLen();
        for (const auto &elem : arr) {
            if (!elem.isString()) {
                continue;
            }
            std::string topic = elem.asString();
            if (static_cast<size_t>(utf8::distance(topic.begin(), topic.end())) > maxTopicLen) {
                return Status {-1, "单条议题长度不能超过" + std::to_string(maxTopicLen) + "字符"};
            }
            req.add_topics(std::move(topic));
        }
        SLOG_DEBUG << "FillTopicsFromParams: extracted " << req.topics_size() << " topics";
        return {};
    }

    static void FillRefreshFieldsFromParams(const drogon::SafeStringMap<std::string> &params,
                                            RefreshSummaryRequest &req) {
        if (params.find("audio_id") != params.end()) {
            req.set_audio_id(params.at("audio_id"));
        }
        if (params.find("kind") != params.end()) {
            try {
                req.set_kind(std::stoi(params.at("kind")));
            } catch (...) {  // NOLINT
                req.set_kind(0);
            }
        }
        if (params.find("word_count") != params.end()) {
            try {
                req.set_word_count(std::stoi(params.at("word_count")));
            } catch (...) {  // NOLINT
                req.set_word_count(0);
            }
        }
        if (params.find("use_note") != params.end()) {
            try {
                req.set_use_note(std::stoi(params.at("use_note")) != 0);
            } catch (...) {  // NOLINT
                req.set_use_note(false);
            }
        }
    }

    // ===================== 主入口 =====================

    Status BmsPreRefreshSummaryReq(const drogon::HttpRequestPtr &httpReq, RefreshSummaryRequest &req) {
        uint64_t accountId = httpReq->attributes()->get<uint64_t>("accountId");
        req.set_account_id(accountId);
        std::string contentType = httpReq->getHeader("Content-Type");
        if (contentType.size() < 19 || contentType.substr(0, 19) != "multipart/form-data") {
            return Status {-1, "请求类型错误, 需要multipart/form-data"};
        }
        drogon::MultiPartParser parser;
        if (parser.parse(httpReq) != 0) {
            return Status {-1, "解析上传请求失败"};
        }

        const auto &params = parser.getParameters();
        FillRefreshFieldsFromParams(params, req);
        Status topicsStatus = FillTopicsFromParams(params, req);
        if (!topicsStatus.IsSuccess()) {
            return topicsStatus;
        }

        const auto &files = parser.getFiles();
        if (!files.empty()) {
            const auto &file = files[0];
            if (static_cast<int64_t>(file.fileLength()) > kMaxTemplateFileSize) {
                return Status {-1, "模板文件大小超过512KB限制"};
            }
            std::string ext = GetFileExtension(file.getFileName());
            SLOG_INFO << "BmsPreRefreshSummaryReq: file=" << file.getFileName() << ", ext=" << ext;
            auto &reader = TemplateFileReader::GetInstance();
            if (!reader.IsSupported(ext)) {
                return Status {-1, "模板文件格式不支持, 仅支持docx/txt/md"};
            }
            SLOG_DEBUG << "BmsPreRefreshSummaryReq: file=" << file.getFileName();
            auto &cfg = TmpPathConfig::GetInstance();
            std::string templatePath = cfg.GetDrogonTmpPath() + "/" + SaveTemplateToTemp(file);
            if (templatePath.empty()) {
                return Status {-1, "保存模板文件失败"};
            }
            TempFileGuard guard(templatePath);
            auto [success, templateText] = reader.Read(guard.GetPath(), ext);
            if (!success) {
                return Status {-1, templateText};
            }
            req.set_template_text(templateText);
            guard.Release();
            SLOG_DEBUG << "BmsPreRefreshSummaryReq: templateTextLen=" << templateText.size()
                       << ", templateText=" << templateText;
        } else {
            req.set_template_text("");
        }
        return {};
    }

}  // namespace qifeng_ca
