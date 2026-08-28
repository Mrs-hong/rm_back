#include <cstdint>
#include <filesystem>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <vector>

#include "json/reader.h"
#include "json/value.h"
#include "minidocx.hpp"
#include "qifeng_framework/common/logger.h"

#include "common/audio_enums.h"
#include "common/common.h"
#include "common/config/tmp_path_config.h"
#include "common/device/disk_check.h"
#include "common/status.h"
#include "common/utils/docx/docx_document.h"
#include "common/utils/docx/zip_utils.h"
#include "common/utils/file_opt.h"
#include "common/utils/symlink_manager.h"
#include "core/meeting/meeting_access.h"
#include "core/meeting/recording_download_service.h"
#include "dao/models/bms_audio.h"
#include "dao/models/bms_note.h"
#include "dao/models/bms_summary.h"
#include "dao/models/bms_trans.h"
#include "dao_managers/meeting_dao_manager.h"
#include "dao_managers/user_dao_manager.h"

namespace qifeng_ca {

    static bool IsValidOfficialType(int32_t official) {
        return official >= 1 && official <= 3;
    }

    [[maybe_unused]] static std::string GetKindName(int32_t kind) {
        switch (kind) {
            case 4:
                return "晨会";
            case 3:
                return "决策会议";
            case 2:
                return "党务会议";
            case 1:
            default:
                return "标准会议";
        }
    }

    static std::string FormatTimestampForDocx(int64_t timestampMs) {
        if (timestampMs <= 0) {
            return "";
        }
        time_t timeT = static_cast<time_t>(timestampMs / 1000);
        struct tm tmBuf {};
        localtime_r(&timeT, &tmBuf);
        std::array<char, 64> buf {};
        strftime(buf.data(), buf.size(), "%Y年%m月%d日 %H:%M", &tmBuf);
        return buf.data();
    }

    // ============================================================================
    // 下载专用文档生成函数
    // 基于 qifeng_ca::docx::DocxDocument 实现:
    //   - 笔记: 空白模板 + GenerateDocument(标题 + 正文)
    //   - 纪要: 按official区分(默认模板/会议纪要模板/联合行文模板)
    // 仅用于RecordDownload接口, 不影响 SummaryDocumentPreview 等其它接口
    // ============================================================================

    // 模板文件基础路径(部署时需确保模板文件存在于该目录下)
    // 目录结构: {templateBase}/默认模板.docx          (空白模板)
    //           {templateBase}/summary/会议纪要模板.docx, 联合行文模板.docx
    //           {templateBase}/note/默认笔记模板.docx
    static const std::string TEMPLATE_BASE_PATH = "data/template";

    // 将参会人员JSON数组字符串格式化为"张三、李四"形式
    // mAttendees为JSON数组字符串, 如 ["张三","李四"]; 解析失败则原样返回
    static std::string FormatAttendees(const std::string &attendeesJson) {
        if (attendeesJson.empty()) {
            return "无";
        }
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
        if (!reader->parse(attendeesJson.c_str(), attendeesJson.c_str() + attendeesJson.size(), &root, &errs)) {
            // 解析失败, 当作纯文本返回
            return attendeesJson;
        }
        if (!root.isArray() || root.empty()) {
            return "无";
        }
        std::string result;
        for (Json::ArrayIndex i = 0; i < root.size(); ++i) {
            if (i > 0) {
                result += "、";
            }
            if (root[i].isString()) {
                result += root[i].asString();
            } else {
                result += root[i].toStyledString();
            }
        }
        return result.empty() ? "无" : result;
    }

    // 判断一段文本是否为Editor.js JSON格式(包含 "blocks" 数组)
    static bool IsEditorJsJson(const std::string &content) {
        if (content.empty())
            return false;
        // 简单特征判断: 以 { 开头且包含 "blocks" 字段
        return content.find_first_not_of(" \t\r\n") == std::string::npos
                   ? false
                   : content[content.find_first_not_of(" \t\r\n")] == '{' &&
                         content.find("\"blocks\"") != std::string::npos;
    }

    // 清理纪要正文内容, 对齐Python _build_summary 中的内容预处理
    // 移除固定格式的"会议纪要"标题块和会议基本信息块(模板已含这些信息, 避免重复)
    // 注意: 保留原始格式(HTML/Markdown), 仅移除固定块, 不剥离标签
    static std::string CleanSummaryRichContent(std::string content) {
        if (content.empty()) {
            return content;
        }

        // 去掉 <h2>会议纪要</h2> 标签(可能在开头或内容中)
        static const std::regex h2Pattern(R"(<h2>\s*会议纪要\s*</h2>\s*)", std::regex::icase);
        content = std::regex_replace(content, h2Pattern, "");

        // 去掉 Markdown "## 会议纪要 ... ### 关键词：" 块, 替换为 "### 关键词："
        static const std::regex mdBlockPattern(R"(##\s*会议纪要[\s\S]*?###\s*关键词：\s*)");
        content = std::regex_replace(content, mdBlockPattern, "### 关键词：");

        // 去掉 HTML "<h2...>会议纪要 ... <h3...>关键词：</h3>" 块
        static const std::regex htmlBlockPattern(R"(<h2[^>]*>会议纪要[\s\S]*?<h3[^>]*>关键词：\s*</h3>\s*)",
                                                 std::regex::icase);
        content = std::regex_replace(content, htmlBlockPattern, "<h3>关键词：</h3>");

        // 去除Markdown固定格式的会议基本信息块
        // 注意: 不使用 std::regex::multiline(C++17), 改用 [\s\S] 匹配跨行以兼容ARM旧版编译器
        static const std::regex mdInfoPattern(
            R"(#[\s]*会议纪要[\s]*##[\s]*会议基本信息[\s\S]*?(会议时间：[^\n]*\n)?(会议地点：[^\n]*\n)?(主持人：[^\n]*\n)?(参会人员：[^\n]*\n)?)");
        content = std::regex_replace(content, mdInfoPattern, "");

        return content;
    }

    // 检测正文内容类型并转换为DocxDocument可处理的格式
    //   - Editor.js JSON: 返回EditorJS类型, 交由 docx 库的 ParseEditorJs 渲染富文本
    //   - HTML(含<html/<div/<p等标签): 返回HTML类型
    //   - 其它(含Markdown/纯文本): 返回Markdown类型
    static qifeng_ca::docx::RichReplacement BuildBodyReplacement(const std::string &content) {
        qifeng_ca::docx::RichReplacement rep;
        if (content.empty()) {
            rep.type = qifeng_ca::docx::ContentType::Plain;
            return rep;
        }
        // Editor.js JSON 优先: 直接交给 docx 库的 ParseEditorJs 渲染富文本(保留标题/列表/加粗等格式)
        if (IsEditorJsJson(content)) {
            rep.content = content;
            rep.type = qifeng_ca::docx::ContentType::EditorJS;
            return rep;
        }
        // 含HTML块级标签则按HTML解析
        if (content.find("<p") != std::string::npos || content.find("<h") != std::string::npos ||
            content.find("<div") != std::string::npos || content.find("<ul") != std::string::npos ||
            content.find("<ol") != std::string::npos || content.find("<br") != std::string::npos) {
            rep.content = content;
            rep.type = qifeng_ca::docx::ContentType::HTML;
            return rep;
        }
        // 默认按Markdown处理(纯文本也能被Markdown解析器正确渲染为段落)
        rep.content = content;
        rep.type = qifeng_ca::docx::ContentType::Markdown;
        return rep;
    }

    // 删除同主题的旧纪要docx残留(涵盖新格式 {theme}_{official}_会议纪要.docx 与
    // 历史带时间戳格式 {theme}_{official}_{ts}_会议纪要.docx)
    // 每次生成前调用, 保证目录中同主题纪要始终只有最新一份, 不随多次点击堆积
    static void RemoveOldSummaryDocx(const std::string &outputDir, const std::string &theme) {
        const std::string prefix = theme + "_";
        const std::string suffix = "_会议纪要.docx";
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(outputDir, ec)) {
            if (ec)
                break;
            if (!entry.is_regular_file(ec))
                continue;
            const std::string name = entry.path().filename().string();
            // 中间至少还应包含 official(及历史时间戳)部分
            if (name.size() <= prefix.size() + suffix.size())
                continue;
            if (!name.starts_with(prefix) || !name.ends_with(suffix))
                continue;
            std::error_code rmEc;
            std::filesystem::remove(entry.path(), rmEc);
            if (rmEc) {
                SLOG_WARN << "RemoveOldSummaryDocx: remove failed, file=" << name << ", " << rmEc.message();
            } else {
                SLOG_INFO << "RemoveOldSummaryDocx: removed old file=" << name;
            }
        }
    }

    // 生成下载用纪要docx
    //   official=1 默认模板: 使用空白模板 + GenerateDocument(标题 + 会议基本信息 + 会议纪要内容 + 正文 + 尾部字段)
    //                        布局对齐 SummaryDocumentPreview 的 BuildPreviewDocx 显示效果
    //   official=2 会议纪要模板: 使用 会议纪要模板.docx 替换占位符
    //   official=3 联合行文模板: 使用 联合行文模板.docx 替换占位符
    static std::string BuildDownloadSummaryDocx(const models::Audio &audio, const models::Summary &summary,
                                                int32_t official, const std::string &outputDir) {
        std::string theme = audio.mTheme.empty() ? "录音" : audio.mTheme;
        std::string recordingTime = FormatTimestampForDocx(audio.mRecordingTime);
        std::string attendeesStr = FormatAttendees(audio.mAttendees);

        FileOpt::CreateDstDirectory(outputDir);
        // 文件名不带时间戳: {theme}_{official}_会议纪要.docx
        // 每次点击重新生成: 先删除同主题旧纪要残留(含历史带时间戳格式文件), 不做缓存复用,
        // 确保内容始终最新且目录中同主题纪要只有一份
        RemoveOldSummaryDocx(outputDir, theme);
        std::string tmpPath = outputDir + theme + "_" + std::to_string(official) + "_会议纪要.docx";

        // 清理固定标题块/会议信息块(模板/GenerateDocument会重新生成, 避免重复)
        std::string bodyContent = CleanSummaryRichContent(summary.mContent);
        SLOG_INFO << "BuildDownloadSummaryDocx: summary.mContent size=" << summary.mContent.size()
                  << ", after clean size=" << bodyContent.size() << ", content head=[" << bodyContent.substr(0, 120)
                  << "]";

        if (official == 1) {
            // 默认模板: 使用空白模板 + GenerateDocument(title, paragraphs) 生成
            // 布局对齐预览: 标题(h1) + "会议基本信息"(h2) + 5行会议信息 + "会议纪要内容"(h2) + 正文 + 尾部字段
            qifeng_ca::docx::DocxConfig config;
            config.tempDir = outputDir + ".docx_tmp_summary_" + std::to_string(GetTimeMs()) + "/";
            qifeng_ca::docx::DocxDocument doc(config);

            std::string blankTemplate = TEMPLATE_BASE_PATH + "/默认模板.docx";
            auto openErr = doc.Open(blankTemplate);
            if (!openErr.Ok()) {
                SLOG_ERROR << "BuildDownloadSummaryDocx: open blank template failed, " << openErr.ToString()
                           << ", path=" << blankTemplate;
                return "";
            }

            // 构造正文段落列表(标题由 GenerateDocument 内部作为 h1 添加)
            std::vector<qifeng_ca::docx::RichParagraph> paragraphs;

            // // 辅助: 追加 h2 小标题段落
            // auto appendH2 = [&](const std::string &text) {
            //     qifeng_ca::docx::RichParagraph para;
            //     para.headingLevel = 2;
            //     qifeng_ca::docx::RichRun run;
            //     run.text = text;
            //     para.runs.push_back(run);
            //     para.alignment = "left";
            //     para.lineSpacing = 1.0;
            //     paragraphs.push_back(para);
            // };
            // 辅助: 追加正文样式段落(左对齐, 无首行缩进, 用于会议信息行和尾部字段行)
            auto appendLine = [&](const std::string &text) {
                qifeng_ca::docx::RichParagraph para;
                qifeng_ca::docx::RichRun run;
                run.text = text;
                para.runs.push_back(run);
                para.alignment = "left";
                para.lineSpacing = 1.0;
                paragraphs.push_back(para);
            };

            // 会议基本信息块
            // appendH2("会议基本信息");
            // appendLine("会议主题：" + (theme.empty() ? std::string("无") : theme));
            // appendLine("会议时间：" + (recordingTime.empty() ? std::string("无") : recordingTime));
            // appendLine("会议地点：" + (audio.mPlaces.empty() ? std::string("无") : audio.mPlaces));
            // appendLine("主持人：" + (audio.mModerator.empty() ? std::string("无") : audio.mModerator));
            // appendLine("参会人员：" + attendeesStr);

            // // 会议纪要内容块
            // appendH2("会议纪要内容");

            // 正文: 不再把会议信息拼入正文, 直接用清理后的 summary 内容按检测类型渲染
            // (EditorJS/HTML/Markdown 都由 docx 库的 ParseBodyContent 解析为富文本段落)
            auto bodyRep = BuildBodyReplacement(bodyContent);
            const char* typeStr = "Unknown";
            switch (bodyRep.type) {
                case qifeng_ca::docx::ContentType::Plain:
                    typeStr = "Plain";
                    break;
                case qifeng_ca::docx::ContentType::HTML:
                    typeStr = "HTML";
                    break;
                case qifeng_ca::docx::ContentType::Markdown:
                    typeStr = "Markdown";
                    break;
                case qifeng_ca::docx::ContentType::EditorJS:
                    typeStr = "EditorJS";
                    break;
            }
            SLOG_INFO << "BuildDownloadSummaryDocx: bodyRep type=" << typeStr
                      << ", content size=" << bodyRep.content.size();
            if (!bodyRep.content.empty()) {
                // 借助 ParseBodyContent 将正文解析为段落(通过临时调用 GenerateDocument 的字符串重载不合适,
                // 这里直接复用 docx 库的解析函数)
                // 注: ParseBodyContent 是 docx_document.cpp 的匿名命名空间函数, 外部不可见;
                //     此处通过 GenerateDocument(title, bodyContent, type) 的字符串重载间接解析不可行(它会插入标题)。
                //     解决方案: 用 docx 库的公开 ParseHtml/ParseMarkdown/ParseEditorJs 直接解析。
                std::vector<qifeng_ca::docx::RichParagraph> bodyParas;
                if (bodyRep.type == qifeng_ca::docx::ContentType::HTML) {
                    bodyParas = qifeng_ca::docx::ParseHtml(bodyRep.content);
                } else if (bodyRep.type == qifeng_ca::docx::ContentType::EditorJS) {
                    bodyParas = qifeng_ca::docx::ParseEditorJs(bodyRep.content);
                } else if (bodyRep.type == qifeng_ca::docx::ContentType::Markdown) {
                    bodyParas = qifeng_ca::docx::ParseMarkdown(bodyRep.content);
                } else {
                    // Plain: 按行分割为正文段落
                    std::istringstream iss(bodyRep.content);
                    std::string line;
                    while (std::getline(iss, line)) {
                        if (line.find_first_not_of(" \t\r\n") == std::string::npos)
                            continue;
                        qifeng_ca::docx::RichParagraph para;
                        qifeng_ca::docx::RichRun run;
                        run.text = line;
                        para.runs.push_back(run);
                        para.alignment = "justify";
                        para.firstLineIndent = 640;
                        para.lineSpacing = 1.0;
                        paragraphs.push_back(para);
                    }
                }
                SLOG_INFO << "BuildDownloadSummaryDocx: bodyParas parsed, count=" << bodyParas.size();
                paragraphs.insert(paragraphs.end(), bodyParas.begin(), bodyParas.end());
            }

            // 尾部字段块(对齐 纪要.txt 尾部格式: 出席/请假/列席/地点/会议类型/主持人/备注)
            // 数据来源: audio 现有字段, 缺失字段留空
            appendLine("出席：\t" + attendeesStr);
            appendLine("请假：\t");
            appendLine("列席：\t");
            appendLine("地点：\t" + (audio.mPlaces.empty() ? std::string("无") : audio.mPlaces));
            // appendLine("会议类型：\t" + GetKindName(audio.mKind));
            appendLine("主持人：\t" + (audio.mModerator.empty() ? std::string("无") : audio.mModerator));
            appendLine("备注：\t" + (audio.mRemark.empty() ? std::string("无") : audio.mRemark));

            // 调用段落列表版本的 GenerateDocument(标题作为 h1 由内部添加)
            SLOG_INFO << "BuildDownloadSummaryDocx: before GenerateDocument, total paragraphs=" << paragraphs.size();
            auto genRes = doc.GenerateDocument(theme + " - 会议纪要", paragraphs);
            if (!genRes.Ok()) {
                SLOG_ERROR << "BuildDownloadSummaryDocx: generate document failed, " << genRes.error.ToString();
                return "";
            }

            auto saveErr = doc.Save(tmpPath);
            doc.Close();
            if (!saveErr.Ok()) {
                SLOG_ERROR << "BuildDownloadSummaryDocx: save failed, " << saveErr.ToString();
                return "";
            }
            SLOG_INFO << "BuildDownloadSummaryDocx: saved to " << tmpPath << ", official=1";
            return tmpPath;
        }

        // official=2/3: 使用模板docx文件, 替换占位符(对齐Python sdgm.summary.build逻辑)
        std::string templateName = (official == 2) ? "会议纪要模板.docx" : "联合行文模板.docx";
        std::string templatePath = TEMPLATE_BASE_PATH + "/summary/" + templateName;

        SLOG_INFO << "BuildDownloadSummaryDocx: using template=" << templatePath << ", official=" << official;

        if (!std::filesystem::exists(templatePath)) {
            SLOG_ERROR << "BuildDownloadSummaryDocx: template not found, path=" << templatePath;
            return "";
        }

        qifeng_ca::docx::DocxConfig config;
        config.tempDir = outputDir + ".docx_tmp_summary_" + std::to_string(GetTimeMs()) + "/";
        qifeng_ca::docx::DocxDocument doc(config);

        auto openErr = doc.Open(templatePath);
        if (!openErr.Ok()) {
            SLOG_ERROR << "BuildDownloadSummaryDocx: open template failed, " << openErr.ToString()
                       << ", path=" << templatePath;
            return "";
        }

        // 占位符替换映射(模板中使用 {{xxx}} 格式, DocxDocument按"名称"匹配, 不含{{}})
        // 正文用富文本替换(支持HTML/Markdown/EditorJS渲染), 其余字段用纯文本替换
        std::map<std::string, qifeng_ca::docx::RichReplacement> richReplacements;
        richReplacements["正文"] = BuildBodyReplacement(bodyContent);
        richReplacements["会议主题"] = {theme, qifeng_ca::docx::ContentType::Plain};
        richReplacements["会议时间"] = {recordingTime.empty() ? "无" : recordingTime,
                                        qifeng_ca::docx::ContentType::Plain};
        richReplacements["会议地点"] = {audio.mPlaces.empty() ? "无" : audio.mPlaces,
                                        qifeng_ca::docx::ContentType::Plain};
        richReplacements["主持人"] = {audio.mModerator.empty() ? "无" : audio.mModerator,
                                      qifeng_ca::docx::ContentType::Plain};
        richReplacements["参会人员"] = {attendeesStr, qifeng_ca::docx::ContentType::Plain};
        // richReplacements["会议类型"] = {GetKindName(audio.mKind), qifeng_ca::docx::ContentType::Plain};
        richReplacements["备注"] = {audio.mRemark.empty() ? "无" : audio.mRemark, qifeng_ca::docx::ContentType::Plain};
        // 尾部字段占位符(模板若含则填充, 不含则未匹配不报错)
        richReplacements["出席"] = {attendeesStr, qifeng_ca::docx::ContentType::Plain};
        richReplacements["请假"] = {"", qifeng_ca::docx::ContentType::Plain};
        richReplacements["列席"] = {"", qifeng_ca::docx::ContentType::Plain};

        auto richRes = doc.ReplaceRich(richReplacements);
        if (!richRes.Ok()) {
            // ReplaceRich在未匹配占位符时返回NoMatchFound, 不影响保存, 仅记录警告
            SLOG_WARN << "BuildDownloadSummaryDocx: replace rich finished, " << richRes.error.ToString()
                      << ", replaced=" << richRes.totalReplaced;
        } else {
            SLOG_INFO << "BuildDownloadSummaryDocx: replace rich ok, replaced=" << richRes.totalReplaced;
        }

        auto saveErr = doc.Save(tmpPath);
        doc.Close();
        if (!saveErr.Ok()) {
            SLOG_ERROR << "BuildDownloadSummaryDocx: save failed, " << saveErr.ToString();
            return "";
        }
        SLOG_INFO << "BuildDownloadSummaryDocx: saved to " << tmpPath;
        return tmpPath;
    }

    // 生成下载用笔记docx: 使用空白模板 + GenerateDocument(标题 + 正文)
    // 笔记不包含会议信息块, 仅 标题 + 正文
    // 内容为空(空字符串或{"blocks":[]})时生成仅含标题的空正文 docx
    static std::string BuildDownloadNoteDocx(const models::Audio &audio, const models::Note &note,
                                             const std::string &outputDir) {
        std::string theme = audio.mTheme.empty() ? "录音" : audio.mTheme;

        FileOpt::CreateDstDirectory(outputDir);
        std::string tmpPath = outputDir + theme + "_笔记.docx";

        qifeng_ca::docx::DocxConfig config;
        config.tempDir = outputDir + ".docx_tmp_note_" + std::to_string(GetTimeMs()) + "/";
        qifeng_ca::docx::DocxDocument doc(config);

        std::string blankTemplate = TEMPLATE_BASE_PATH + "/默认模板.docx";
        auto openErr = doc.Open(blankTemplate);
        if (!openErr.Ok()) {
            SLOG_ERROR << "BuildDownloadNoteDocx: open blank template failed, " << openErr.ToString()
                       << ", path=" << blankTemplate;
            return "";
        }

        // 标题作为h1, 正文按检测到的内容类型渲染(纯文本/HTML/Markdown/Editor.js JSON)
        auto bodyRep = BuildBodyReplacement(note.mContent);
        // 笔记内容为空时, 生成仅含标题的 docx(正文留空)
        // 传单个空格占位: ParseBodyContent 的 Plain 分支会跳过空行, 最终 docx 仅含标题
        std::string bodyContent = bodyRep.content;
        qifeng_ca::docx::ContentType bodyType = bodyRep.type;
        if (bodyContent.empty()) {
            bodyContent = " ";
            bodyType = qifeng_ca::docx::ContentType::Plain;
        }
        auto genRes = doc.GenerateDocument(theme + " - 笔记", bodyContent, bodyType);
        if (!genRes.Ok()) {
            SLOG_ERROR << "BuildDownloadNoteDocx: generate document failed, " << genRes.error.ToString();
            return "";
        }

        auto saveErr = doc.Save(tmpPath);
        doc.Close();
        if (!saveErr.Ok()) {
            SLOG_ERROR << "BuildDownloadNoteDocx: save failed, " << saveErr.ToString();
            return "";
        }
        SLOG_INFO << "BuildDownloadNoteDocx: saved to " << tmpPath;
        return tmpPath;
    }

    // 预估RecordDownload所需磁盘空间并校验是否充足(在准备临时目录前提前拦截)
    static Status CheckDiskSpaceForDownload(const RecordDownloadRequest &req, const models::Audio &audio) {
        int64_t estimatedBytes = 0;
        // 音频文件: 直接取实际文件大小
        for (const auto &content : req.contents()) {
            if (content == "audio" && !audio.mFileName.empty() && std::filesystem::exists(audio.mFileName)) {
                std::error_code ec;
                auto fileSize = std::filesystem::file_size(audio.mFileName, ec);
                if (!ec) {
                    estimatedBytes += static_cast<int64_t>(fileSize);
                }
            }
        }
        // docx 生成需要解压模板 + 写入临时文件 + 重压缩, 每个 docx 预留 5MB 余量
        // note/summary 各生成一个 docx, 加上 zip 打包额外开销
        constexpr int64_t kDocxOverhead = 5LL * 1024 * 1024;
        for (const auto &content : req.contents()) {
            if (content == "note" || content == "summary") {
                estimatedBytes += kDocxOverhead;
            }
        }
        // zip 打包 + 临时目录开销再预留 1MB
        estimatedBytes += 1LL * 1024 * 1024;

        Status diskStatus = DiskCheck::GetInstance().IsDiskLow(req.account_id(), estimatedBytes);
        if (!diskStatus.IsSuccess()) {
            return diskStatus;
        }
        return Status {};
    }

    Status RecordingDownloadService::SummaryDocumentPreview(const SummaryDocumentPreviewRequest &req,
                                                            SummaryDocumentPreviewResponse* resp) {
        if (req.audio_id().empty()) {
            return Status {-1, "录音audio_id不能为空"};
        }

        if (!IsValidOfficialType(req.official())) {
            return Status {-1, "不支持的模板类型，支持1/2/3"};
        }

        auto [audio, accessStatus] = GetAccessibleAudio(req.account_id(), req.audio_id());
        if (!accessStatus.IsSuccess()) {
            return accessStatus;
        }
        if (audio.mStatus != static_cast<int>(AudioStatus::SummaryComplete)) {
            return Status {-1, "该录音状态无法生成预览"};
        }

        std::vector<models::Trans> transList;
        models::Summary summary;
        MeetingDaoManager::GetInstance().GetRecordingDetailData(audio.mAccountId, audio.mAudioId, transList, summary);

        if (summary.mContent.empty()) {
            return Status {-1, "纪要内容为空，无法生成预览"};
        }

        try {
            auto &cfg = TmpPathConfig::GetInstance();
            std::string baseDir = cfg.GetDrogonTmpPath() + "/" + cfg.GetDocxTmpPath();
            std::string workDir = baseDir + std::to_string(req.account_id()) + "/";
            FileOpt::CreateDstDirectory(workDir);
            std::string filePath = BuildDownloadSummaryDocx(audio, summary, req.official(), workDir);
            resp->set_file_url(filePath);

            SLOG_INFO << "Summary document preview generated: " << filePath;
        } catch (const std::exception &e) {
            SLOG_ERROR << "Generate document preview failed: " << e.what();
            return Status {-1, "生成文档预览失败"};
        }

        return Status {};
    }

    Status RecordingDownloadService::DownloadAudio(const RecordInfoRequest &req, RecordDownloadResponse* resp) {
        if (req.audio_id().empty()) {
            return Status {-1, "录音audio_id不能为空"};
        }

        auto [audio, accessStatus] = GetAccessibleAudio(req.account_id(), req.audio_id());
        if (!accessStatus.IsSuccess()) {
            return accessStatus;
        }

        if (audio.mFileName.empty()) {
            return Status {-1, "录音文件名为空"};
        }

        std::string wavPath = SymlinkManager::GetInstance().ResolveData2Path(audio.mFileName);
        if (!std::filesystem::exists(wavPath)) {
            return Status {-1, "录音文件不存在"};
        }

        // 下载前校验剩余空间
        std::error_code ec;
        auto fileSize = static_cast<int64_t>(std::filesystem::file_size(wavPath, ec));
        if (ec) {
            return Status {-1, "获取录音文件大小失败"};
        }

        Status diskStatus = DiskCheck::GetInstance().IsDiskLow(req.account_id(), fileSize);
        if (!diskStatus.IsSuccess()) {
            return diskStatus;
        }

        resp->set_file_url(wavPath);
        return Status {};
    }

    Status RecordingDownloadService::RecordDownload(const RecordDownloadRequest &req, RecordDownloadResponse* resp) {
        // 1. 参数校验
        if (req.id().empty()) {
            return Status {-1, "录音ID不能为空"};
        }
        if (!IsValidOfficialType(req.official())) {
            return Status {-1, "不支持的公文格式，支持1/2/3"};
        }
        if (req.type() != 1) {
            return Status {-1, "不支持的文件格式，目前仅支持docx(类型1)"};
        }
        if (req.contents().empty()) {
            return Status {-1, "下载内容不能为空"};
        }

        // 2. 查询audio记录(全局查询, 随后校验访问权限)
        uint64_t audioPkId = 0;
        try {
            audioPkId = std::stoull(req.id());
        } catch (const std::exception &e) {
            return Status {-1, "录音ID格式无效"};
        }
        models::Audio audio = MeetingDaoManager::GetInstance().GetByIdGlobal(audioPkId);
        if (audio.mId == 0) {
            return Status {-1, "录音不存在"};
        }
        // 校验访问权限
        {
            models::User user = UserDaoManager::GetInstance().GetByAccountId(req.account_id());
            if (user.mAccountId == 0) {
                return Status {-1, "用户不存在"};
            }
            if (!IsAudioAccessible(req.account_id(), user.mGroupId, audio.mAccountId)) {
                return Status {-1, "无权访问该录音"};
            }
        }

        // 2.5. 预估磁盘占用并校验空间是否充足(在准备临时目录前提前拦截)
        if (auto status = CheckDiskSpaceForDownload(req, audio); !status.IsSuccess()) {
            return status;
        }

        // 3. 准备工作目录: {docx_tmp}/{accountId}/
        //    与预览共享同一目录以复用已生成的 docx(文件名相同则不重复生成, 节约资源);
        //    下载时仅打包本次请求的 packedFiles 列表(调用 ZipFiles), 不会带入预览残留的其他 official 文档
        auto &cfg = TmpPathConfig::GetInstance();
        std::string baseDir = cfg.GetDrogonTmpPath() + "/" + cfg.GetDocxTmpPath();
        std::string workDir = baseDir + std::to_string(req.account_id()) + "/";
        FileOpt::CreateDstDirectory(workDir);

        std::string theme = audio.mTheme.empty() ? "录音" : audio.mTheme;
        std::vector<std::string> packedFiles;  // 相对于workDir的文件名列表

        // 4. 遍历contents, 生成对应文件
        // 使用与 record_content.py 对齐的下载专用文档生成函数, 不影响其它接口
        for (const auto &content : req.contents()) {
            if (content == "note") {
                // 查询笔记(使用音频所有者的account_id)
                models::Note note = MeetingDaoManager::GetInstance().GetNoteByAudioId(audio.mAccountId, audio.mAudioId);
                if (note.mId == 0) {
                    // 笔记不存在才跳过; 内容为空时仍生成仅含标题的空正文 docx
                    SLOG_WARN << "RecordDownload: note not found, audioId=" << audio.mAudioId;
                    continue;
                }
                try {
                    // 直接在workDir生成, 文件名与Python一致: {theme}_笔记.docx
                    std::string notePath = BuildDownloadNoteDocx(audio, note, workDir);
                    std::string fileName = std::filesystem::path(notePath).filename().string();
                    packedFiles.push_back(fileName);
                } catch (const std::exception &e) {
                    SLOG_ERROR << "RecordDownload: build note docx failed, " << e.what();
                }
            } else if (content == "summary") {
                if (audio.mStatus != static_cast<int>(AudioStatus::SummaryComplete)) {
                    return Status {-1, "该录音状态无法下载文档"};
                }
                // 查询纪要
                std::vector<models::Trans> transList;
                models::Summary summary;
                MeetingDaoManager::GetInstance().GetRecordingDetailData(audio.mAccountId, audio.mAudioId, transList,
                                                                        summary);
                if (summary.mContent.empty()) {
                    SLOG_WARN << "RecordDownload: summary is empty, audioId=" << audio.mAudioId;
                    continue;
                }
                try {
                    // 直接在workDir生成, 文件名与Python一致: {theme}_会议纪要.docx
                    // 按official区分: 1=默认模板 2=会议纪要模板 3=联合行文模板
                    std::string summaryPath = BuildDownloadSummaryDocx(audio, summary, req.official(), workDir);
                    std::string summaryFileName = std::filesystem::path(summaryPath).filename().string();
                    packedFiles.push_back(summaryFileName);
                } catch (const std::exception &e) {
                    SLOG_ERROR << "RecordDownload: build summary docx failed, " << e.what();
                }
            } else if (content == "audio") {
                // 拷贝原始音频文件到workDir
                std::string audioSrcPath = audio.mFileName;
                if (audioSrcPath.empty() || !std::filesystem::exists(audioSrcPath)) {
                    SLOG_WARN << "RecordDownload: audio file not exist, path=" << audioSrcPath;
                    continue;
                }
                std::string audioFileName = theme + ".wav";
                std::string audioDstPath = workDir + audioFileName;
                std::error_code ec;
                std::filesystem::copy_file(audioSrcPath, audioDstPath,
                                           std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    SLOG_ERROR << "RecordDownload: copy audio failed, " << ec.message();
                    continue;
                }
                packedFiles.push_back(audioFileName);
            } else {
                SLOG_WARN << "RecordDownload: unknown content type: " << content;
            }
        }

        if (packedFiles.empty()) {
            // 清理空目录
            std::filesystem::remove_all(workDir);
            return Status {-1, "无可下载的内容文件"};
        }

        // 5. 仅打包本次请求生成的文件(仅存储不压缩, 速度优先)
        //    使用 ZipFiles 按 packedFiles 列表打包, 不打包 workDir 中预览残留的其他 official 文档;
        //    zip 输出到 baseDir, 文件名带毫秒时间戳({theme}_{ts}.zip), 每次下载天然生成不同文件名;
        //    不清理旧zip: 多用户并发下载同主题时, 旧zip可能仍被其他用户下载中, 按前缀删除会误删,
        //    各次下载的zip独立保留, 由外部定期清理策略兜底
        std::vector<std::string> absFilePaths;
        absFilePaths.reserve(packedFiles.size());
        for (const auto &fileName : packedFiles) {
            absFilePaths.push_back(workDir + fileName);
        }
        std::string zipFileName = theme + "_" + std::to_string(GetTimeMs()) + ".zip";
        std::string zipPath = baseDir + zipFileName;
        if (!qifeng_ca::docx::ZipFiles(absFilePaths, zipPath, true)) {
            SLOG_ERROR << "RecordDownload: zip files failed, workDir=" << workDir << ", files=" << absFilePaths.size();
            return Status {-1, "打包文件失败"};
        }

        // 6. 清理临时工作目录(zip已包含所有文件)
        std::error_code rmEc;
        std::filesystem::remove_all(workDir, rmEc);
        if (rmEc) {
            SLOG_WARN << "RecordDownload: cleanup workDir failed, " << rmEc.message();
        }

        resp->set_file_url(zipPath);
        SLOG_INFO << "RecordDownload: zip generated, path=" << zipPath << ", files=" << packedFiles.size();
        return Status {};
    }

}  // namespace qifeng_ca