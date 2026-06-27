/**
 * @file docx_document.cpp
 * @brief DOCX 文档处理器 - 主实现
 *
 * 核心算法：跨 Run 占位符替换（逐字符法）
 *
 * OOXML 中 {{会议主题}} 通常被 Word 拆分为 3 个 <w:r> 元素：
 *   <w:r><w:rPr>样式A</w:rPr><w:t>{{</w:t></w:r>
 *   <w:r><w:rPr>样式B</w:rPr><w:t>会议主题</w:t></w:r>
 *   <w:r><w:rPr>样式A</w:rPr><w:t>}}</w:t></w:r>
 *
 * 替换策略：逐字符遍历每个 run 的文本：
 *   - 匹配区间 [startPos, endPos) 内的字符跳过（属于 {{name}}）
 *   - 在 startPos 位置插入替换文本
 *   - 匹配区间外的字符保留原样
 *   - 所有 <w:r> / <w:rPr> 节点不动，仅修改 <w:t> 文本
 *
 * 双模式架构：
 *   - DOM 模式：全量加载 document.xml，支持多次替换复用 DOM
 *   - 流式模式：按段落逐个处理，内存占用 O(最大段落大小)
 */

#include "utils/docx/docx_document.h"
#include "utils/docx/rich_content.h"
#include "streaming_processor.h"
#include "zip_utils.h"
#include "xml_utils.h"

#include <pugixml.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <functional>
#include <map>

namespace fs = std::filesystem;

namespace utils::docx {

// ═══════════════════════════════════════════════════════════════════════════════
// 辅助函数（匿名命名空间，文件内可见）
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// 检查文件是否存在
bool FileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

/// 递归删除目录
void RemoveDir(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

/// 创建唯一临时目录（使用 mkdtemp）
std::string CreateTempDir(const std::string& baseDir) {
    std::string tempName = "docx_" + std::to_string(getpid()) + "_XXXXXX";
    char tempPath[256];
    if (!baseDir.empty()) {
        snprintf(tempPath, sizeof(tempPath), "%s/%s",
                 baseDir.c_str(), tempName.c_str());
    } else {
        snprintf(tempPath, sizeof(tempPath), "/tmp/%s", tempName.c_str());
    }
    char* result = mkdtemp(tempPath);
    if (result == nullptr) return "";
    return std::string(result);
}

/// 占位符匹配信息
struct PlaceholderMatch {
    size_t startPos;          ///< {{ 在段落全文中的起始位置
    size_t endPos;            ///< }} 在段落全文中的结束位置（不含）
    std::string name;         ///< 占位符名称文本
    std::string replacement;  ///< 替换文本
};

/// 在段落全文中搜索所有 {{...}} 占位符
/// @param fullText 段落全文
/// @param replacements 占位符名称→替换文本 的映射
/// @return 匹配列表（含替换文本）
std::vector<PlaceholderMatch> findPlaceholders(
    const std::string& fullText,
    const std::map<std::string, std::string>& replacements) {

    std::vector<PlaceholderMatch> matches;
    size_t searchPos = 0;

    while (true) {
        size_t start = fullText.find("{{", searchPos);
        if (start == std::string::npos) break;

        size_t end = fullText.find("}}", start + 2);
        if (end == std::string::npos) break;

        std::string name = fullText.substr(start + 2, end - start - 2);

        // 查找匹配的替换规则：先精确匹配，再通配符 "*"
        std::string replacement;
        bool matched = false;

        auto it = replacements.find(name);
        if (it != replacements.end()) {
            replacement = it->second;
            matched = true;
        }

        if (!matched) {
            auto wildcard = replacements.find("*");
            if (wildcard != replacements.end()) {
                replacement = wildcard->second;
                matched = true;
            }
        }

        if (!matched) {
            searchPos = end + 2;
            continue;
        }

        PlaceholderMatch match;
        match.startPos = start;
        match.endPos = end + 2;  // 包含 }}
        match.name = name;
        match.replacement = replacement;
        matches.push_back(match);

        searchPos = end + 2;
    }

    return matches;
}

/// 对一个段落中的占位符进行替换（逐字符法核心算法）
///
/// 对于匹配区间 [match.startPos, match.endPos) 内的字符：
///   - match.startPos 位置：插入替换文本
///   - 其它位置：跳过（属于 {{name}} 的一部分，已被替换文本取代）
/// 对于匹配区间外的字符：保留原样
///
/// @param paragraph <w:p> 段落节点
/// @param replacements 占位符名称→替换文本 的映射
/// @param records 替换记录（输出）
/// @param verbose 是否输出详细日志
/// @return 替换次数
int ReplaceInParagraph(
    pugi::xml_node paragraph,
    const std::map<std::string, std::string>& replacements,
    std::vector<ReplaceRecord>& records,
    bool verbose) {

    std::vector<RunInfo> runs;
    std::string fullText = CollectRuns(paragraph, runs);

    if (fullText.find("{{") == std::string::npos) {
        return 0;  // 段落中没有占位符
    }

    auto matches = findPlaceholders(fullText, replacements);
    if (matches.empty()) {
        return 0;
    }

    if (verbose) {
        for (const auto& m : matches) {
            std::cout << "  替换: {{" << m.name << "}} -> "
                      << m.replacement << std::endl;
        }
    }

    // 逐字符处理：匹配区间内的字符被跳过，在区间起始处插入替换文本
    for (const auto& run : runs) {
        std::string newText;

        for (size_t j = 0; j < run.text.size(); j++) {
            size_t absPos = run.startPos + j;  // 该字符在全文中的绝对位置

            bool insideMatch = false;
            bool atMatchStart = false;
            const PlaceholderMatch* currentMatch = nullptr;

            for (const auto& match : matches) {
                if (absPos >= match.startPos && absPos < match.endPos) {
                    insideMatch = true;
                    if (absPos == match.startPos) {
                        atMatchStart = true;
                        currentMatch = &match;
                    }
                    break;
                }
            }

            // 在匹配区间起始处插入替换文本
            if (atMatchStart && currentMatch) {
                newText += currentMatch->replacement;
            }

            // 匹配区间内的字符跳过（已被替换文本取代）
            if (!insideMatch) {
                newText += run.text[j];
            }
        }

        if (newText != run.text) {
            SetRunText(run.runNode, newText);
        }
    }

    // 记录替换结果
    int replaceCount = 0;
    for (const auto& match : matches) {
        ReplaceRecord record;
        record.placeholder = "{{" + match.name + "}}";
        record.replacement = match.replacement;
        record.count = 1;
        records.push_back(record);
        replaceCount++;
    }

    return replaceCount;
}

/// 检查段落是否只包含指定的占位符（独占一行）
/// @param paragraph <w:p> 段落节点
/// @param placeholderName 占位符名称
/// @return 若段落全文只有 {{placeholderName}} 则返回 true
bool IsParagraphOnlyPlaceholder(pugi::xml_node paragraph,
                                const std::string& placeholderName) {
    std::vector<RunInfo> runs;
    std::string fullText = CollectRuns(paragraph, runs);

    // 去除首尾空白后检查
    size_t start = fullText.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return false;
    size_t end = fullText.find_last_not_of(" \t\r\n");
    std::string trimmed = fullText.substr(start, end - start + 1);

    std::string expected = "{{" + placeholderName + "}}";
    return trimmed == expected;
}

/// 检查段落是否只包含 {{...}} 模式（用于通配符匹配）
bool IsParagraphOnlyAnyPlaceholder(pugi::xml_node paragraph) {
    std::vector<RunInfo> runs;
    std::string fullText = CollectRuns(paragraph, runs);

    size_t s = fullText.find_first_not_of(" \t\r\n");
    if (s == std::string::npos) return false;
    size_t e = fullText.find_last_not_of(" \t\r\n");
    std::string trimmed = fullText.substr(s, e - s + 1);

    return trimmed.size() >= 4 &&
           trimmed.substr(0, 2) == "{{" &&
           trimmed.substr(trimmed.size() - 2) == "}}";
}

/// 将 pugi::xml_node 序列化为字符串（无 XML 声明，无缩进）
std::string serializeNode(pugi::xml_node node) {
    std::ostringstream oss;
    node.print(oss, "", pugi::format_raw);
    return oss.str();
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// ToMessage 实现
// ═══════════════════════════════════════════════════════════════════════════════

const char* ToMessage(int code) {
    switch (static_cast<ErrorCode>(code)) {
        case ErrorCode::Ok:                 return "Ok";
        // 文件 / IO
        case ErrorCode::FileNotFound:        return "FileNotFound";
        case ErrorCode::FileNotReadable:     return "FileNotReadable";
        case ErrorCode::InvalidDocxFormat:   return "InvalidDocxFormat";
        case ErrorCode::UnzipFailed:         return "UnzipFailed";
        case ErrorCode::TempDirCreateFailed: return "TempDirCreateFailed";
        case ErrorCode::OutputWriteFailed:  return "OutputWriteFailed";
        // XML / 处理
        case ErrorCode::XmlParseFailed:      return "XmlParseFailed";
        case ErrorCode::XmlSaveFailed:        return "XmlSaveFailed";
        case ErrorCode::ZipFailed:           return "ZipFailed";
        case ErrorCode::NoMatchFound:         return "NoMatchFound";
        case ErrorCode::InvalidPattern:       return "InvalidPattern";
        case ErrorCode::NotOpened:           return "NotOpened";
        case ErrorCode::UnknownError:         return "UnknownError";
    }
    return "Unknown error code";
}

// ═══════════════════════════════════════════════════════════════════════════════
// DocxDocument 构造与析构
// ═══════════════════════════════════════════════════════════════════════════════

DocxDocument::DocxDocument() = default;

DocxDocument::DocxDocument(const DocxConfig& config) : mConfig(config) {}

DocxDocument::~DocxDocument() {
    Close();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 生命周期管理
// ═══════════════════════════════════════════════════════════════════════════════

Result<void> DocxDocument::Open(const std::string& path) {
    if (mOpened) {
        Close();  // 先关闭已打开的文档
    }

    // 1. 检查文件存在
    if (!FileExists(path)) {
        return Result<void>::Fail(ErrorCode::FileNotFound,
                                  "输入文件不存在 | path: " + path);
    }

    // 2. 记录文件大小
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec) {
        return Result<void>::Fail(ErrorCode::FileNotReadable,
                                  "无法读取文件大小 | path: " + path + ", error: " + ec.message());
    }
    mFileSize = static_cast<size_t>(size);
    mInputPath = path;

    // 3. 创建临时目录
    auto dirErr = CreateTempDir_();
    if (!dirErr.IsOk()) return dirErr;

    // 4. 解压 docx
    auto unzipErr = Unzip_();
    if (!unzipErr.IsOk()) {
        CleanupTempDir_();
        return unzipErr;
    }

    if (mConfig.verbose) {
        std::cout << "已打开: " << path << " (" << mFileSize << " bytes)" << std::endl;
        std::cout << "临时目录: " << mTempDir << std::endl;
    }

    mOpened = true;
    return Result<void>::Ok();
}

Result<void> DocxDocument::Save(const std::string& path) {
    if (!mOpened) {
        return Result<void>::Fail(ErrorCode::NotOpened, "文档未打开");
    }

    // DOM 模式下需先保存 DOM 到文件
    if (mDomLoaded && !mStreamingMode) {
        auto err = SaveXmlDom_();
        if (!err.IsOk()) return err;
    }

    // 压缩为 docx
    return Zip_(path);
}

void DocxDocument::Close() {
    CleanupTempDir_();

    // 重置状态
    mTempDir.clear();
    mInputPath.clear();
    mFileSize = 0;
    mStreamingMode = false;
    mOpened = false;
    mDomLoaded = false;
    mDomDoc.reset();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 文本替换
// ═══════════════════════════════════════════════════════════════════════════════

Result<ReplaceStats> DocxDocument::ReplaceText(const std::map<std::string, std::string>& replacements) {
    if (!mOpened) {
        return Result<ReplaceStats>::Fail(ErrorCode::NotOpened, "文档未打开");
    }

    if (replacements.empty()) {
        return Result<ReplaceStats>::Fail(ErrorCode::InvalidPattern,
                                          "替换映射为空 | replacements map is empty");
    }

    // 首次调用时确定处理模式
    if (!mDomLoaded && !mStreamingMode) {
        size_t extraSize = 0;
        for (const auto& [k, v] : replacements) {
            extraSize += v.size();
        }

        if (ShouldUseStreaming_(extraSize)) {
            mStreamingMode = true;
            if (mConfig.verbose) {
                std::cout << "启用流式处理模式 (fileSize=" << mFileSize
                          << " + content=" << extraSize
                          << " > limit=" << mConfig.memoryLimit << ")" << std::endl;
            }
        } else {
            auto err = ParseXmlDom_();
            if (!err.IsOk()) {
                return Result<ReplaceStats>::Fail(err.Code(), err.Message());
            }
            mDomLoaded = true;
        }
    }

    if (mStreamingMode) {
        return ReplaceTextStreaming_(replacements);
    } else {
        return ReplaceTextDom_(replacements);
    }
}

Result<ReplaceStats> DocxDocument::ReplaceText(const std::string& pattern,
                                                 const std::string& replacement) {
    // 解析 pattern，转换为 map 后委托给批量接口
    std::map<std::string, std::string> replacements;

    if (pattern.size() >= 4 && pattern.substr(0, 2) == "{{"
        && pattern.substr(pattern.size() - 2) == "}}") {
        std::string key = pattern.substr(2, pattern.size() - 4);
        replacements[key] = replacement;
    } else if (pattern == "*") {
        replacements["*"] = replacement;
    } else {
        replacements[pattern] = replacement;
    }

    return ReplaceText(replacements);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 富文本替换
// ═══════════════════════════════════════════════════════════════════════════════

Result<ReplaceStats> DocxDocument::ReplaceRich(const std::map<std::string, RichReplacement>& replacements) {
    if (!mOpened) {
        return Result<ReplaceStats>::Fail(ErrorCode::NotOpened, "文档未打开");
    }

    if (replacements.empty()) {
        return Result<ReplaceStats>::Fail(ErrorCode::InvalidPattern,
                                          "替换映射为空 | replacements map is empty");
    }

    // 首次调用时确定处理模式
    if (!mDomLoaded && !mStreamingMode) {
        size_t extraSize = 0;
        for (const auto& [k, v] : replacements) {
            extraSize += v.content.size();
        }

        if (ShouldUseStreaming_(extraSize)) {
            mStreamingMode = true;
            if (mConfig.verbose) {
                std::cout << "启用流式处理模式 (fileSize=" << mFileSize
                          << " + content=" << extraSize
                          << " > limit=" << mConfig.memoryLimit << ")" << std::endl;
            }
        } else {
            auto err = ParseXmlDom_();
            if (!err.IsOk()) {
                return Result<ReplaceStats>::Fail(err.Code(), err.Message());
            }
            mDomLoaded = true;
        }
    }

    if (mStreamingMode) {
        return ReplaceRichStreaming_(replacements);
    } else {
        return ReplaceRichDom_(replacements);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 文档生成（从空白模板）
// ═══════════════════════════════════════════════════════════════════════════════

Result<ReplaceStats> DocxDocument::GenerateDocument(const std::string& title,
                                                       const std::string& bodyContent,
                                                       ContentType contentType) {
    if (!mOpened) {
        return Result<ReplaceStats>::Fail(ErrorCode::NotOpened, "文档未打开");
    }

    if (bodyContent.empty()) {
        return Result<ReplaceStats>::Fail(ErrorCode::InvalidPattern, "正文内容为空");
    }

    // GenerateDocument 始终使用 DOM 模式（空白模板很小，无需流式）
    if (!mDomLoaded) {
        auto err = ParseXmlDom_();
        if (!err.IsOk()) {
            return Result<ReplaceStats>::Fail(err.Code(), err.Message());
        }
        mDomLoaded = true;
        mStreamingMode = false;
    }

    // 找到 <w:body> 和 <w:sectPr>
    pugi::xml_node root = mDomDoc.document_element();  // <w:document>
    pugi::xml_node body = root.child("w:body");
    if (!body) {
        return Result<ReplaceStats>::Fail(ErrorCode::XmlParseFailed,
                                          "未找到 <w:body> 元素");
    }

    // <w:sectPr> 是页面属性节点，新段落需插入到它之前
    pugi::xml_node sectPr = body.child("w:sectPr");

    // 构建段落列表
    std::vector<RichParagraph> paragraphs;

    // 添加标题（如果提供）—— h1 格式：方正小标宋简体 22pt 居中
    if (!title.empty()) {
        RichParagraph titlePara;
        titlePara.headingLevel = 1;
        titlePara.alignment = "center";  // h1 标题居中
        titlePara.lineSpacing = 1.0;
        RichRun titleRun;
        titleRun.text = title;
        titlePara.runs.push_back(titleRun);
        paragraphs.push_back(titlePara);
    }

    // 根据内容类型解析正文
    if (contentType == ContentType::HTML) {
        auto bodyParas = ParseHtml(bodyContent);
        paragraphs.insert(paragraphs.end(), bodyParas.begin(), bodyParas.end());
    } else if (contentType == ContentType::Markdown) {
        auto bodyParas = ParseMarkdown(bodyContent);
        paragraphs.insert(paragraphs.end(), bodyParas.begin(), bodyParas.end());
    } else {
        // Plain：按行分割，每行渲染为仿宋正文段落（首行缩进 32pt = 640 twips）
        std::istringstream iss(bodyContent);
        std::string line;
        while (std::getline(iss, line)) {
            // 跳过空行
            if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;

            RichParagraph para;
            RichRun run;
            run.text = line;
            para.runs.push_back(run);
            para.alignment = "justify";
            para.firstLineIndent = 640;  // 32pt 首行缩进
            para.lineSpacing = 1.0;
            paragraphs.push_back(para);
        }
    }

    if (paragraphs.empty()) {
        return Result<ReplaceStats>::Fail(ErrorCode::InvalidPattern, "解析后无有效段落");
    }

    // 渲染并插入到 <w:sectPr> 之前
    AppendParagraphsBefore(body, paragraphs, sectPr);

    // 记录生成结果
    ReplaceStats stats;
    ReplaceRecord record;
    record.placeholder = "[Generated]";
    record.replacement = "title=" + (title.empty() ? std::string("(none)") : title) +
                         ", paragraphs=" + std::to_string(paragraphs.size()) +
                         ", type=" + std::string(contentType == ContentType::HTML ? "HTML" :
                                                contentType == ContentType::Markdown ? "Markdown" : "Plain");
    record.count = static_cast<int>(paragraphs.size());
    stats.records.push_back(record);
    stats.totalReplaced = 1;

    if (mConfig.verbose) {
        std::cout << "文档生成完成: " << paragraphs.size() << " 个段落" << std::endl;
    }

    return Result<ReplaceStats>::Ok(std::move(stats));
}

// ═══════════════════════════════════════════════════════════════════════════════
// 状态查询
// ═══════════════════════════════════════════════════════════════════════════════

bool DocxDocument::IsOpen() const { return mOpened; }
bool DocxDocument::IsStreamingMode() const { return mStreamingMode; }
size_t DocxDocument::GetFileSize() const { return mFileSize; }
const DocxConfig& DocxDocument::Config() const { return mConfig; }

// ═══════════════════════════════════════════════════════════════════════════════
// 私有工具方法
// ═══════════════════════════════════════════════════════════════════════════════

bool DocxDocument::ShouldUseStreaming_(size_t extraSize) const {
    return mFileSize + extraSize > mConfig.memoryLimit;
}

Result<void> DocxDocument::CreateTempDir_() {
    if (!mConfig.tempDir.empty()) {
        mTempDir = mConfig.tempDir;
        std::error_code ec;
        fs::create_directories(mTempDir, ec);
        if (ec) {
            return Result<void>::Fail(ErrorCode::TempDirCreateFailed,
                                      "无法创建临时目录 | path: " + mTempDir + ", error: " + ec.message());
        }
    } else {
        mTempDir = CreateTempDir("");
        if (mTempDir.empty()) {
            return Result<void>::Fail(ErrorCode::TempDirCreateFailed,
                                      "无法创建系统临时目录 | /tmp/docx_*");
        }
    }
    return Result<void>::Ok();
}

Result<void> DocxDocument::Unzip_() {
    if (!UnzipToDir(mInputPath, mTempDir)) {
        return Result<void>::Fail(ErrorCode::UnzipFailed,
                                  "解压 docx 文件失败 | file: " + mInputPath);
    }
    return Result<void>::Ok();
}

Result<void> DocxDocument::ParseXmlDom_() {
    std::string xmlPath = mTempDir + "/word/document.xml";
    pugi::xml_parse_result parseResult = mDomDoc.load_file(xmlPath.c_str());

    if (!parseResult) {
        return Result<void>::Fail(ErrorCode::XmlParseFailed,
                                  "XML 解析失败 | file: " + xmlPath + ", error: " + parseResult.description());
    }

    if (mConfig.verbose) {
        std::cout << "XML DOM 加载完成: " << xmlPath << std::endl;
    }
    return Result<void>::Ok();
}

Result<void> DocxDocument::SaveXmlDom_() {
    std::string xmlPath = mTempDir + "/word/document.xml";
    if (!mDomDoc.save_file(xmlPath.c_str())) {
        return Result<void>::Fail(ErrorCode::XmlSaveFailed,
                                  "保存 XML 文件失败 | file: " + xmlPath);
    }
    return Result<void>::Ok();
}

Result<void> DocxDocument::Zip_(const std::string& outputPath) {
    if (!ZipDir(mTempDir, outputPath)) {
        return Result<void>::Fail(ErrorCode::ZipFailed,
                                  "压缩为 docx 文件失败 | output: " + outputPath);
    }

    if (mConfig.verbose) {
        std::cout << "输出文件: " << outputPath << std::endl;
    }
    return Result<void>::Ok();
}

void DocxDocument::CleanupTempDir_() {
    if (!mTempDir.empty() && !mConfig.keepTempDir) {
        RemoveDir(mTempDir);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// DOM 模式替换
// ═══════════════════════════════════════════════════════════════════════════════

Result<ReplaceStats> DocxDocument::ReplaceTextDom_(
    const std::map<std::string, std::string>& replacements) {

    ReplaceStats stats;
    pugi::xml_node root = mDomDoc.document_element();  // <w:document>

    // 递归遍历所有 <w:p> 节点（包括表格单元格中的段落）
    int totalReplaced = 0;

    std::function<void(pugi::xml_node)> traverseNodes =
        [&](pugi::xml_node node) {
        for (pugi::xml_node child = node.first_child(); child;
             child = child.next_sibling()) {
            if (strcmp(child.name(), "w:p") == 0) {
                totalReplaced += ReplaceInParagraph(
                    child, replacements, stats.records, mConfig.verbose);
            }
            traverseNodes(child);
        }
    };

    traverseNodes(root);

    stats.totalReplaced = totalReplaced;

    if (mConfig.verbose) {
        std::cout << "替换完成，总替换次数: " << totalReplaced << std::endl;
    }

    if (totalReplaced == 0) {
        return Result<ReplaceStats>::Fail(ErrorCode::NoMatchFound,
                                          "未找到匹配的占位符 | pattern keys: " +
                                          std::to_string(replacements.size()) + " entries");
    }

    return Result<ReplaceStats>::Ok(std::move(stats));
}

Result<ReplaceStats> DocxDocument::ReplaceRichDom_(
    const std::map<std::string, RichReplacement>& replacements) {

    ReplaceStats stats;

    // 将纯文本替换项分离出来
    std::map<std::string, std::string> plainReplacements;
    std::map<std::string, RichReplacement> richReplacements;

    for (const auto& [key, rich] : replacements) {
        if (rich.type == ContentType::Plain) {
            plainReplacements[key] = rich.content;
        } else {
            richReplacements[key] = rich;
        }
    }

    pugi::xml_node root = mDomDoc.document_element();
    int totalReplaced = 0;

    // ── 阶段1：先处理富文本替换（HTML/Markdown）──
    // 富文本替换需要替换整个段落，必须在纯文本替换之前进行
    if (!richReplacements.empty()) {
        std::function<void(pugi::xml_node)> richTraverse =
            [&](pugi::xml_node node) {
            // 先收集所有 <w:p> 子节点（避免遍历时修改导致迭代器失效）
            std::vector<pugi::xml_node> paragraphs;
            for (pugi::xml_node child = node.first_child(); child;
                 child = child.next_sibling()) {
                if (strcmp(child.name(), "w:p") == 0) {
                    paragraphs.push_back(child);
                }
            }

            for (auto& para : paragraphs) {
                for (const auto& [key, rich] : richReplacements) {
                    bool match = false;
                    if (key == "*") {
                        match = IsParagraphOnlyAnyPlaceholder(para);
                    } else {
                        match = IsParagraphOnlyPlaceholder(para, key);
                    }

                    if (match) {
                        if (mConfig.verbose) {
                            std::cout << "  富文本替换: {{" << key << "}} ("
                                      << (rich.type == ContentType::HTML ? "HTML" : "MD")
                                      << ")" << std::endl;
                        }

                        // 解析内容
                        std::vector<RichParagraph> richParas;
                        if (rich.type == ContentType::HTML) {
                            richParas = ParseHtml(rich.content);
                        } else {
                            richParas = ParseMarkdown(rich.content);
                        }

                        // 渲染并替换段落
                        pugi::xml_node parent = para.parent();
                        RenderParagraphsToXml(parent, richParas, para);

                        // 记录替换
                        ReplaceRecord record;
                        record.placeholder = "{{" + key + "}}";
                        record.replacement = "[RichContent:" +
                            std::string(rich.type == ContentType::HTML ? "HTML" : "MD") +
                            ":" + std::to_string(richParas.size()) + " paragraphs]";
                        record.count = 1;
                        stats.records.push_back(record);
                        totalReplaced++;
                        break;  // 一个段落只匹配一个替换项
                    }
                }
            }

            // 递归处理子节点
            for (pugi::xml_node child = node.first_child(); child;
                 child = child.next_sibling()) {
                richTraverse(child);
            }
        };

        richTraverse(root);
    }

    // ── 阶段2：处理纯文本替换 ──
    if (!plainReplacements.empty()) {
        std::function<void(pugi::xml_node)> plainTraverse =
            [&](pugi::xml_node node) {
            for (pugi::xml_node child = node.first_child(); child;
                 child = child.next_sibling()) {
                if (strcmp(child.name(), "w:p") == 0) {
                    totalReplaced += ReplaceInParagraph(
                        child, plainReplacements, stats.records, mConfig.verbose);
                }
                plainTraverse(child);
            }
        };

        plainTraverse(root);
    }

    stats.totalReplaced = totalReplaced;

    if (mConfig.verbose) {
        std::cout << "替换完成，总替换次数: " << totalReplaced << std::endl;
    }

    if (totalReplaced == 0) {
        return Result<ReplaceStats>::Fail(ErrorCode::NoMatchFound,
                                          "未找到匹配的占位符 | replacements: " +
                                          std::to_string(replacements.size()) + " entries");
    }

    return Result<ReplaceStats>::Ok(std::move(stats));
}

// ═══════════════════════════════════════════════════════════════════════════════
// 流式模式替换
// ═══════════════════════════════════════════════════════════════════════════════

Result<ReplaceStats> DocxDocument::ReplaceTextStreaming_(
    const std::map<std::string, std::string>& replacements) {

    ReplaceStats stats;

    std::string inputXmlPath = mTempDir + "/word/document.xml";
    std::string outputXmlPath = mTempDir + "/word/document_out.xml";

    int totalReplaced = 0;

    // 段落处理回调：解析段落 → 执行文本替换 → 序列化返回
    detail::ParagraphHandler handler = [&](const std::string& paraXml) -> std::string {
        pugi::xml_document fragDoc;
        pugi::xml_parse_result parseResult = fragDoc.load_string(paraXml.c_str());
        if (!parseResult) {
            return "";  // 解析失败，保持原文
        }

        pugi::xml_node paraNode = fragDoc.document_element();  // <w:p>
        int replaced = ReplaceInParagraph(paraNode, replacements,
                                          stats.records, mConfig.verbose);

        if (replaced > 0) {
            totalReplaced += replaced;
            return serializeNode(paraNode);
        }
        return "";  // 无修改，保持原文
    };

    if (!detail::StreamProcessXml(inputXmlPath, outputXmlPath, handler)) {
        return Result<ReplaceStats>::Fail(ErrorCode::XmlSaveFailed,
                                          "流式处理 XML 失败 | file: " + inputXmlPath);
    }

    // 用处理后的文件替换原文件
    std::error_code ec;
    fs::rename(outputXmlPath, inputXmlPath, ec);
    if (ec) {
        // rename 失败时尝试 copy + remove
        fs::copy_file(outputXmlPath, inputXmlPath,
                      fs::copy_options::overwrite_existing, ec);
        fs::remove(outputXmlPath, ec);
    }

    stats.totalReplaced = totalReplaced;

    if (mConfig.verbose) {
        std::cout << "流式替换完成，总替换次数: " << totalReplaced << std::endl;
    }

    if (totalReplaced == 0) {
        return Result<ReplaceStats>::Fail(ErrorCode::NoMatchFound,
                                          "未找到匹配的占位符 | pattern keys: " +
                                          std::to_string(replacements.size()) + " entries");
    }

    return Result<ReplaceStats>::Ok(std::move(stats));
}

Result<ReplaceStats> DocxDocument::ReplaceRichStreaming_(
    const std::map<std::string, RichReplacement>& replacements) {

    ReplaceStats stats;

    // 分离纯文本和富文本替换项
    std::map<std::string, std::string> plainReplacements;
    std::map<std::string, RichReplacement> richReplacements;

    for (const auto& [key, rich] : replacements) {
        if (rich.type == ContentType::Plain) {
            plainReplacements[key] = rich.content;
        } else {
            richReplacements[key] = rich;
        }
    }

    std::string inputXmlPath = mTempDir + "/word/document.xml";
    std::string outputXmlPath = mTempDir + "/word/document_out.xml";

    int totalReplaced = 0;

    // 段落处理回调：先检查富文本匹配，再做纯文本替换
    detail::ParagraphHandler handler = [&](const std::string& paraXml) -> std::string {
        pugi::xml_document fragDoc;
        pugi::xml_parse_result parseResult = fragDoc.load_string(paraXml.c_str());
        if (!parseResult) {
            return "";  // 解析失败，保持原文
        }

        pugi::xml_node paraNode = fragDoc.document_element();  // <w:p>

        // ── 阶段1：检查富文本替换 ──
        for (const auto& [key, rich] : richReplacements) {
            bool match = false;
            if (key == "*") {
                match = IsParagraphOnlyAnyPlaceholder(paraNode);
            } else {
                match = IsParagraphOnlyPlaceholder(paraNode, key);
            }

            if (match) {
                if (mConfig.verbose) {
                    std::cout << "  富文本替换(流式): {{" << key << "}} ("
                              << (rich.type == ContentType::HTML ? "HTML" : "MD")
                              << ")" << std::endl;
                }

                // 解析富文本内容
                std::vector<RichParagraph> richParas;
                if (rich.type == ContentType::HTML) {
                    richParas = ParseHtml(rich.content);
                } else {
                    richParas = ParseMarkdown(rich.content);
                }

                // 记录替换
                ReplaceRecord record;
                record.placeholder = "{{" + key + "}}";
                record.replacement = "[RichContent:" +
                    std::string(rich.type == ContentType::HTML ? "HTML" : "MD") +
                    ":" + std::to_string(richParas.size()) + " paragraphs]";
                record.count = 1;
                stats.records.push_back(record);
                totalReplaced++;

                // 序列化新段落（替换原段落）
                return SerializeParagraphs(richParas);
            }
        }

        // ── 阶段2：纯文本替换（仅在无富文本匹配时）──
        if (!plainReplacements.empty()) {
            int replaced = ReplaceInParagraph(paraNode, plainReplacements,
                                              stats.records, mConfig.verbose);
            if (replaced > 0) {
                totalReplaced += replaced;
                return serializeNode(paraNode);
            }
        }

        return "";  // 无修改，保持原文
    };

    if (!detail::StreamProcessXml(inputXmlPath, outputXmlPath, handler)) {
        return Result<ReplaceStats>::Fail(ErrorCode::XmlSaveFailed,
                                          "流式处理 XML 失败 | file: " + inputXmlPath);
    }

    // 用处理后的文件替换原文件
    std::error_code ec;
    fs::rename(outputXmlPath, inputXmlPath, ec);
    if (ec) {
        fs::copy_file(outputXmlPath, inputXmlPath,
                      fs::copy_options::overwrite_existing, ec);
        fs::remove(outputXmlPath, ec);
    }

    stats.totalReplaced = totalReplaced;

    if (mConfig.verbose) {
        std::cout << "流式富文本替换完成，总替换次数: " << totalReplaced << std::endl;
    }

    if (totalReplaced == 0) {
        return Result<ReplaceStats>::Fail(ErrorCode::NoMatchFound,
                                          "未找到匹配的占位符 | replacements: " +
                                          std::to_string(replacements.size()) + " entries");
    }

    return Result<ReplaceStats>::Ok(std::move(stats));
}

// ═══════════════════════════════════════════════════════════════════════════════
// ZIP 工具函数
// ═══════════════════════════════════════════════════════════════════════════════

Result<void> ZipCompress(const std::string& srcPath,
                       const std::string& outputPath,
                       const std::string& zipName) {
    namespace fs = std::filesystem;

    // 检查源路径是否存在
    if (!fs::exists(srcPath)) {
        return Result<void>::Fail(ErrorCode::FileNotFound,
                                   "源路径不存在 | path: " + srcPath);
    }

    // 确保输出目录存在
    std::error_code ec;
    fs::create_directories(outputPath, ec);
    if (ec) {
        return Result<void>::Fail(ErrorCode::TempDirCreateFailed,
                                   "无法创建输出目录 | path: " + outputPath);
    }

    // 拼接完整输出路径
    std::string fullZipPath = outputPath;
    if (!fullZipPath.empty() && fullZipPath.back() != '/') {
        fullZipPath += '/';
    }
    fullZipPath += zipName;

    bool ok = false;
    if (fs::is_directory(srcPath)) {
        // 目录压缩
        ok = ZipDir(srcPath, fullZipPath);
    } else {
        // 单文件压缩
        ok = ZipSingleFile(srcPath, fullZipPath);
    }

    if (!ok) {
        return Result<void>::Fail(ErrorCode::ZipFailed,
                                   "压缩失败 | src: " + srcPath + " -> zip: " + fullZipPath);
    }

    return Result<void>::Ok();  // 成功
}

Result<void> ZipExtract(const std::string& zipPath, const std::string& destDir) {
    namespace fs = std::filesystem;

    // 检查 ZIP 文件是否存在
    if (!fs::exists(zipPath)) {
        return Result<void>::Fail(ErrorCode::FileNotFound,
                                   "ZIP 文件不存在 | path: " + zipPath);
    }

    // 确保目标目录存在
    std::error_code ec;
    fs::create_directories(destDir, ec);
    if (ec) {
        return Result<void>::Fail(ErrorCode::TempDirCreateFailed,
                                   "无法创建目标目录 | path: " + destDir);
    }

    if (!UnzipToDir(zipPath, destDir)) {
        return Result<void>::Fail(ErrorCode::UnzipFailed,
                                   "解压失败 | zip: " + zipPath + " -> dir: " + destDir);
    }

    return Result<void>::Ok();  // 成功
}

} // namespace utils::docx
