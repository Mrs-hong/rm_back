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

#include "docx_temp_helper/docx_document.h"
#include "docx_temp_helper/rich_content.h"
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

namespace docx_temp_helper {

// ═══════════════════════════════════════════════════════════════════════════════
// 辅助函数（匿名命名空间，文件内可见）
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// 创建错误信息
ErrorInfo makeError(ErrorCode code, const std::string& message,
                    const std::string& detail = "") {
    ErrorInfo err;
    err.code = code;
    err.message = message;
    err.detail = detail;
    return err;
}

/// 检查文件是否存在
bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

/// 递归删除目录
void removeDir(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

/// 创建唯一临时目录（使用 mkdtemp）
std::string createTempDir(const std::string& baseDir) {
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
int replaceInParagraph(
    pugi::xml_node paragraph,
    const std::map<std::string, std::string>& replacements,
    std::vector<ReplaceRecord>& records,
    bool verbose) {

    std::vector<RunInfo> runs;
    std::string fullText = collectRuns(paragraph, runs);

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
            setRunText(run.runNode, newText);
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
bool isParagraphOnlyPlaceholder(pugi::xml_node paragraph,
                                const std::string& placeholderName) {
    std::vector<RunInfo> runs;
    std::string fullText = collectRuns(paragraph, runs);

    // 去除首尾空白后检查
    size_t start = fullText.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return false;
    size_t end = fullText.find_last_not_of(" \t\r\n");
    std::string trimmed = fullText.substr(start, end - start + 1);

    std::string expected = "{{" + placeholderName + "}}";
    return trimmed == expected;
}

/// 检查段落是否只包含 {{...}} 模式（用于通配符匹配）
bool isParagraphOnlyAnyPlaceholder(pugi::xml_node paragraph) {
    std::vector<RunInfo> runs;
    std::string fullText = collectRuns(paragraph, runs);

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
// ErrorInfo 实现
// ═══════════════════════════════════════════════════════════════════════════════

std::string ErrorInfo::toString() const {
    std::string result = "[";

    switch (code) {
        case ErrorCode::Ok:                 result += "Ok"; break;
        case ErrorCode::FileNotFound:       result += "FileNotFound"; break;
        case ErrorCode::FileNotReadable:    result += "FileNotReadable"; break;
        case ErrorCode::InvalidDocxFormat:  result += "InvalidDocxFormat"; break;
        case ErrorCode::UnzipFailed:        result += "UnzipFailed"; break;
        case ErrorCode::XmlParseFailed:     result += "XmlParseFailed"; break;
        case ErrorCode::XmlSaveFailed:      result += "XmlSaveFailed"; break;
        case ErrorCode::ZipFailed:          result += "ZipFailed"; break;
        case ErrorCode::TempDirCreateFailed: result += "TempDirCreateFailed"; break;
        case ErrorCode::NoMatchFound:       result += "NoMatchFound"; break;
        case ErrorCode::InvalidPattern:     result += "InvalidPattern"; break;
        case ErrorCode::OutputWriteFailed:  result += "OutputWriteFailed"; break;
        case ErrorCode::NotOpened:          result += "NotOpened"; break;
        case ErrorCode::UnknownError:       result += "UnknownError"; break;
    }
    result += "] ";

    if (!message.empty()) result += message;
    if (!detail.empty()) result += " | " + detail;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// DocxDocument 构造与析构
// ═══════════════════════════════════════════════════════════════════════════════

DocxDocument::DocxDocument() = default;

DocxDocument::DocxDocument(const DocxConfig& config) : config_(config) {}

DocxDocument::~DocxDocument() {
    close();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 生命周期管理
// ═══════════════════════════════════════════════════════════════════════════════

ErrorInfo DocxDocument::open(const std::string& path) {
    if (opened_) {
        close();  // 先关闭已打开的文档
    }

    // 1. 检查文件存在
    if (!fileExists(path)) {
        return makeError(ErrorCode::FileNotFound,
                         "输入文件不存在", "path: " + path);
    }

    // 2. 记录文件大小
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec) {
        return makeError(ErrorCode::FileNotReadable,
                         "无法读取文件大小", "path: " + path + ", error: " + ec.message());
    }
    fileSize_ = static_cast<size_t>(size);
    inputPath_ = path;

    // 3. 创建临时目录
    auto dirErr = createTempDir_();
    if (!dirErr.ok()) return dirErr;

    // 4. 解压 docx
    auto unzipErr = unzip_();
    if (!unzipErr.ok()) {
        cleanupTempDir_();
        return unzipErr;
    }

    if (config_.verbose) {
        std::cout << "已打开: " << path << " (" << fileSize_ << " bytes)" << std::endl;
        std::cout << "临时目录: " << tempDir_ << std::endl;
    }

    opened_ = true;
    return ErrorInfo{};  // Ok
}

ErrorInfo DocxDocument::save(const std::string& path) {
    if (!opened_) {
        return makeError(ErrorCode::NotOpened, "文档未打开", "");
    }

    // DOM 模式下需先保存 DOM 到文件
    if (domLoaded_ && !streamingMode_) {
        auto err = saveXmlDom_();
        if (!err.ok()) return err;
    }

    // 压缩为 docx
    return zip_(path);
}

void DocxDocument::close() {
    cleanupTempDir_();

    // 重置状态
    tempDir_.clear();
    inputPath_.clear();
    fileSize_ = 0;
    streamingMode_ = false;
    opened_ = false;
    domLoaded_ = false;
    domDoc_.reset();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 文本替换
// ═══════════════════════════════════════════════════════════════════════════════

ReplaceResult DocxDocument::replaceText(const std::map<std::string, std::string>& replacements) {
    ReplaceResult result;

    if (!opened_) {
        result.error = makeError(ErrorCode::NotOpened, "文档未打开", "");
        return result;
    }

    if (replacements.empty()) {
        result.error = makeError(ErrorCode::InvalidPattern,
                                 "替换映射为空", "replacements map is empty");
        return result;
    }

    // 首次调用时确定处理模式
    if (!domLoaded_ && !streamingMode_) {
        size_t extraSize = 0;
        for (const auto& [k, v] : replacements) {
            extraSize += v.size();
        }

        if (shouldUseStreaming_(extraSize)) {
            streamingMode_ = true;
            if (config_.verbose) {
                std::cout << "启用流式处理模式 (fileSize=" << fileSize_
                          << " + content=" << extraSize
                          << " > limit=" << config_.memoryLimit << ")" << std::endl;
            }
        } else {
            auto err = parseXmlDom_();
            if (!err.ok()) {
                result.error = err;
                return result;
            }
            domLoaded_ = true;
        }
    }

    if (streamingMode_) {
        return replaceTextStreaming_(replacements);
    } else {
        return replaceTextDom_(replacements);
    }
}

ReplaceResult DocxDocument::replaceText(const std::string& pattern,
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

    return replaceText(replacements);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 富文本替换
// ═══════════════════════════════════════════════════════════════════════════════

ReplaceResult DocxDocument::replaceRich(const std::map<std::string, RichReplacement>& replacements) {
    ReplaceResult result;

    if (!opened_) {
        result.error = makeError(ErrorCode::NotOpened, "文档未打开", "");
        return result;
    }

    if (replacements.empty()) {
        result.error = makeError(ErrorCode::InvalidPattern,
                                 "替换映射为空", "replacements map is empty");
        return result;
    }

    // 首次调用时确定处理模式
    if (!domLoaded_ && !streamingMode_) {
        size_t extraSize = 0;
        for (const auto& [k, v] : replacements) {
            extraSize += v.content.size();
        }

        if (shouldUseStreaming_(extraSize)) {
            streamingMode_ = true;
            if (config_.verbose) {
                std::cout << "启用流式处理模式 (fileSize=" << fileSize_
                          << " + content=" << extraSize
                          << " > limit=" << config_.memoryLimit << ")" << std::endl;
            }
        } else {
            auto err = parseXmlDom_();
            if (!err.ok()) {
                result.error = err;
                return result;
            }
            domLoaded_ = true;
        }
    }

    if (streamingMode_) {
        return replaceRichStreaming_(replacements);
    } else {
        return replaceRichDom_(replacements);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 文档生成（从空白模板）
// ═══════════════════════════════════════════════════════════════════════════════

ReplaceResult DocxDocument::generateDocument(const std::string& title,
                                               const std::string& bodyContent,
                                               ContentType contentType) {
    ReplaceResult result;

    if (!opened_) {
        result.error = makeError(ErrorCode::NotOpened, "文档未打开", "");
        return result;
    }

    if (bodyContent.empty()) {
        result.error = makeError(ErrorCode::InvalidPattern, "正文内容为空", "");
        return result;
    }

    // generateDocument 始终使用 DOM 模式（空白模板很小，无需流式）
    if (!domLoaded_) {
        auto err = parseXmlDom_();
        if (!err.ok()) {
            result.error = err;
            return result;
        }
        domLoaded_ = true;
        streamingMode_ = false;
    }

    // 找到 <w:body> 和 <w:sectPr>
    pugi::xml_node root = domDoc_.document_element();  // <w:document>
    pugi::xml_node body = root.child("w:body");
    if (!body) {
        result.error = makeError(ErrorCode::XmlParseFailed,
                                 "未找到 <w:body> 元素", "");
        return result;
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
        auto bodyParas = parseHtml(bodyContent);
        paragraphs.insert(paragraphs.end(), bodyParas.begin(), bodyParas.end());
    } else if (contentType == ContentType::Markdown) {
        auto bodyParas = parseMarkdown(bodyContent);
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
        result.error = makeError(ErrorCode::InvalidPattern,
                                 "解析后无有效段落", "");
        return result;
    }

    // 渲染并插入到 <w:sectPr> 之前
    appendParagraphsBefore(body, paragraphs, sectPr);

    // 记录生成结果
    ReplaceRecord record;
    record.placeholder = "[Generated]";
    record.replacement = "title=" + (title.empty() ? std::string("(none)") : title) +
                         ", paragraphs=" + std::to_string(paragraphs.size()) +
                         ", type=" + std::string(contentType == ContentType::HTML ? "HTML" :
                                                contentType == ContentType::Markdown ? "Markdown" : "Plain");
    record.count = static_cast<int>(paragraphs.size());
    result.records.push_back(record);
    result.totalReplaced = 1;

    if (config_.verbose) {
        std::cout << "文档生成完成: " << paragraphs.size() << " 个段落" << std::endl;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 状态查询
// ═══════════════════════════════════════════════════════════════════════════════

bool DocxDocument::isOpen() const { return opened_; }
bool DocxDocument::isStreamingMode() const { return streamingMode_; }
size_t DocxDocument::getFileSize() const { return fileSize_; }
const DocxConfig& DocxDocument::config() const { return config_; }

// ═══════════════════════════════════════════════════════════════════════════════
// 私有工具方法
// ═══════════════════════════════════════════════════════════════════════════════

bool DocxDocument::shouldUseStreaming_(size_t extraSize) const {
    return fileSize_ + extraSize > config_.memoryLimit;
}

ErrorInfo DocxDocument::createTempDir_() {
    if (!config_.tempDir.empty()) {
        tempDir_ = config_.tempDir;
        std::error_code ec;
        fs::create_directories(tempDir_, ec);
        if (ec) {
            return makeError(ErrorCode::TempDirCreateFailed,
                             "无法创建临时目录",
                             "path: " + tempDir_ + ", error: " + ec.message());
        }
    } else {
        tempDir_ = createTempDir("");
        if (tempDir_.empty()) {
            return makeError(ErrorCode::TempDirCreateFailed,
                             "无法创建系统临时目录", "/tmp/docx_*");
        }
    }
    return ErrorInfo{};
}

ErrorInfo DocxDocument::unzip_() {
    if (!unzipToDir(inputPath_, tempDir_)) {
        return makeError(ErrorCode::UnzipFailed,
                         "解压 docx 文件失败", "file: " + inputPath_);
    }
    return ErrorInfo{};
}

ErrorInfo DocxDocument::parseXmlDom_() {
    std::string xmlPath = tempDir_ + "/word/document.xml";
    pugi::xml_parse_result parseResult = domDoc_.load_file(xmlPath.c_str());

    if (!parseResult) {
        return makeError(ErrorCode::XmlParseFailed,
                         "XML 解析失败",
                         "file: " + xmlPath + ", error: " + parseResult.description());
    }

    if (config_.verbose) {
        std::cout << "XML DOM 加载完成: " << xmlPath << std::endl;
    }
    return ErrorInfo{};
}

ErrorInfo DocxDocument::saveXmlDom_() {
    std::string xmlPath = tempDir_ + "/word/document.xml";
    if (!domDoc_.save_file(xmlPath.c_str())) {
        return makeError(ErrorCode::XmlSaveFailed,
                         "保存 XML 文件失败", "file: " + xmlPath);
    }
    return ErrorInfo{};
}

ErrorInfo DocxDocument::zip_(const std::string& outputPath) {
    if (!zipDir(tempDir_, outputPath)) {
        return makeError(ErrorCode::ZipFailed,
                         "压缩为 docx 文件失败", "output: " + outputPath);
    }

    if (config_.verbose) {
        std::cout << "输出文件: " << outputPath << std::endl;
    }
    return ErrorInfo{};
}

void DocxDocument::cleanupTempDir_() {
    if (!tempDir_.empty() && !config_.keepTempDir) {
        removeDir(tempDir_);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// DOM 模式替换
// ═══════════════════════════════════════════════════════════════════════════════

ReplaceResult DocxDocument::replaceTextDom_(
    const std::map<std::string, std::string>& replacements) {

    ReplaceResult result;
    pugi::xml_node root = domDoc_.document_element();  // <w:document>

    // 递归遍历所有 <w:p> 节点（包括表格单元格中的段落）
    int totalReplaced = 0;

    std::function<void(pugi::xml_node)> traverseNodes =
        [&](pugi::xml_node node) {
        for (pugi::xml_node child = node.first_child(); child;
             child = child.next_sibling()) {
            if (strcmp(child.name(), "w:p") == 0) {
                totalReplaced += replaceInParagraph(
                    child, replacements, result.records, config_.verbose);
            }
            traverseNodes(child);
        }
    };

    traverseNodes(root);

    result.totalReplaced = totalReplaced;

    if (config_.verbose) {
        std::cout << "替换完成，总替换次数: " << totalReplaced << std::endl;
    }

    if (totalReplaced == 0) {
        result.error = makeError(ErrorCode::NoMatchFound,
                                 "未找到匹配的占位符",
                                 "pattern keys: " + std::to_string(replacements.size()) + " entries");
    }

    return result;
}

ReplaceResult DocxDocument::replaceRichDom_(
    const std::map<std::string, RichReplacement>& replacements) {

    ReplaceResult result;

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

    pugi::xml_node root = domDoc_.document_element();
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
                        match = isParagraphOnlyAnyPlaceholder(para);
                    } else {
                        match = isParagraphOnlyPlaceholder(para, key);
                    }

                    if (match) {
                        if (config_.verbose) {
                            std::cout << "  富文本替换: {{" << key << "}} ("
                                      << (rich.type == ContentType::HTML ? "HTML" : "MD")
                                      << ")" << std::endl;
                        }

                        // 解析内容
                        std::vector<RichParagraph> richParas;
                        if (rich.type == ContentType::HTML) {
                            richParas = parseHtml(rich.content);
                        } else {
                            richParas = parseMarkdown(rich.content);
                        }

                        // 渲染并替换段落
                        pugi::xml_node parent = para.parent();
                        renderParagraphsToXml(parent, richParas, para);

                        // 记录替换
                        ReplaceRecord record;
                        record.placeholder = "{{" + key + "}}";
                        record.replacement = "[RichContent:" +
                            std::string(rich.type == ContentType::HTML ? "HTML" : "MD") +
                            ":" + std::to_string(richParas.size()) + " paragraphs]";
                        record.count = 1;
                        result.records.push_back(record);
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
                    totalReplaced += replaceInParagraph(
                        child, plainReplacements, result.records, config_.verbose);
                }
                plainTraverse(child);
            }
        };

        plainTraverse(root);
    }

    result.totalReplaced = totalReplaced;

    if (config_.verbose) {
        std::cout << "替换完成，总替换次数: " << totalReplaced << std::endl;
    }

    if (totalReplaced == 0) {
        result.error = makeError(ErrorCode::NoMatchFound,
                                 "未找到匹配的占位符",
                                 "replacements: " + std::to_string(replacements.size()) + " entries");
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 流式模式替换
// ═══════════════════════════════════════════════════════════════════════════════

ReplaceResult DocxDocument::replaceTextStreaming_(
    const std::map<std::string, std::string>& replacements) {

    ReplaceResult result;

    std::string inputXmlPath = tempDir_ + "/word/document.xml";
    std::string outputXmlPath = tempDir_ + "/word/document_out.xml";

    int totalReplaced = 0;

    // 段落处理回调：解析段落 → 执行文本替换 → 序列化返回
    detail::ParagraphHandler handler = [&](const std::string& paraXml) -> std::string {
        pugi::xml_document fragDoc;
        pugi::xml_parse_result parseResult = fragDoc.load_string(paraXml.c_str());
        if (!parseResult) {
            return "";  // 解析失败，保持原文
        }

        pugi::xml_node paraNode = fragDoc.document_element();  // <w:p>
        int replaced = replaceInParagraph(paraNode, replacements,
                                          result.records, config_.verbose);

        if (replaced > 0) {
            totalReplaced += replaced;
            return serializeNode(paraNode);
        }
        return "";  // 无修改，保持原文
    };

    if (!detail::streamProcessXml(inputXmlPath, outputXmlPath, handler)) {
        result.error = makeError(ErrorCode::XmlSaveFailed,
                                 "流式处理 XML 失败", "file: " + inputXmlPath);
        return result;
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

    result.totalReplaced = totalReplaced;

    if (config_.verbose) {
        std::cout << "流式替换完成，总替换次数: " << totalReplaced << std::endl;
    }

    if (totalReplaced == 0) {
        result.error = makeError(ErrorCode::NoMatchFound,
                                 "未找到匹配的占位符",
                                 "pattern keys: " + std::to_string(replacements.size()) + " entries");
    }

    return result;
}

ReplaceResult DocxDocument::replaceRichStreaming_(
    const std::map<std::string, RichReplacement>& replacements) {

    ReplaceResult result;

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

    std::string inputXmlPath = tempDir_ + "/word/document.xml";
    std::string outputXmlPath = tempDir_ + "/word/document_out.xml";

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
                match = isParagraphOnlyAnyPlaceholder(paraNode);
            } else {
                match = isParagraphOnlyPlaceholder(paraNode, key);
            }

            if (match) {
                if (config_.verbose) {
                    std::cout << "  富文本替换(流式): {{" << key << "}} ("
                              << (rich.type == ContentType::HTML ? "HTML" : "MD")
                              << ")" << std::endl;
                }

                // 解析富文本内容
                std::vector<RichParagraph> richParas;
                if (rich.type == ContentType::HTML) {
                    richParas = parseHtml(rich.content);
                } else {
                    richParas = parseMarkdown(rich.content);
                }

                // 记录替换
                ReplaceRecord record;
                record.placeholder = "{{" + key + "}}";
                record.replacement = "[RichContent:" +
                    std::string(rich.type == ContentType::HTML ? "HTML" : "MD") +
                    ":" + std::to_string(richParas.size()) + " paragraphs]";
                record.count = 1;
                result.records.push_back(record);
                totalReplaced++;

                // 序列化新段落（替换原段落）
                return serializeParagraphs(richParas);
            }
        }

        // ── 阶段2：纯文本替换（仅在无富文本匹配时）──
        if (!plainReplacements.empty()) {
            int replaced = replaceInParagraph(paraNode, plainReplacements,
                                              result.records, config_.verbose);
            if (replaced > 0) {
                totalReplaced += replaced;
                return serializeNode(paraNode);
            }
        }

        return "";  // 无修改，保持原文
    };

    if (!detail::streamProcessXml(inputXmlPath, outputXmlPath, handler)) {
        result.error = makeError(ErrorCode::XmlSaveFailed,
                                 "流式处理 XML 失败", "file: " + inputXmlPath);
        return result;
    }

    // 用处理后的文件替换原文件
    std::error_code ec;
    fs::rename(outputXmlPath, inputXmlPath, ec);
    if (ec) {
        fs::copy_file(outputXmlPath, inputXmlPath,
                      fs::copy_options::overwrite_existing, ec);
        fs::remove(outputXmlPath, ec);
    }

    result.totalReplaced = totalReplaced;

    if (config_.verbose) {
        std::cout << "流式富文本替换完成，总替换次数: " << totalReplaced << std::endl;
    }

    if (totalReplaced == 0) {
        result.error = makeError(ErrorCode::NoMatchFound,
                                 "未找到匹配的占位符",
                                 "replacements: " + std::to_string(replacements.size()) + " entries");
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ZIP 工具函数
// ═══════════════════════════════════════════════════════════════════════════════

ErrorInfo zipCompress(const std::string& srcPath,
                       const std::string& outputPath,
                       const std::string& zipName) {
    namespace fs = std::filesystem;

    // 检查源路径是否存在
    if (!fs::exists(srcPath)) {
        return makeError(ErrorCode::FileNotFound, "源路径不存在", "path: " + srcPath);
    }

    // 确保输出目录存在
    std::error_code ec;
    fs::create_directories(outputPath, ec);
    if (ec) {
        return makeError(ErrorCode::TempDirCreateFailed,
                         "无法创建输出目录", "path: " + outputPath);
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
        ok = zipDir(srcPath, fullZipPath);
    } else {
        // 单文件压缩
        ok = zipSingleFile(srcPath, fullZipPath);
    }

    if (!ok) {
        return makeError(ErrorCode::ZipFailed,
                         "压缩失败", "src: " + srcPath + " -> zip: " + fullZipPath);
    }

    return ErrorInfo{};  // 成功
}

ErrorInfo zipExtract(const std::string& zipPath, const std::string& destDir) {
    namespace fs = std::filesystem;

    // 检查 ZIP 文件是否存在
    if (!fs::exists(zipPath)) {
        return makeError(ErrorCode::FileNotFound, "ZIP 文件不存在", "path: " + zipPath);
    }

    // 确保目标目录存在
    std::error_code ec;
    fs::create_directories(destDir, ec);
    if (ec) {
        return makeError(ErrorCode::TempDirCreateFailed,
                         "无法创建目标目录", "path: " + destDir);
    }

    if (!unzipToDir(zipPath, destDir)) {
        return makeError(ErrorCode::UnzipFailed,
                         "解压失败", "zip: " + zipPath + " -> dir: " + destDir);
    }

    return ErrorInfo{};  // 成功
}

} // namespace docx_temp_helper
