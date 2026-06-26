/**
 * @file docx_replacer.h
 * @brief DOCX 模板占位符替换库 - 公开 API
 *
 * 提供对 .docx 文件中 {{xx}} 占位符的文本替换功能，
 * 不改变原有排版样式（字体、字号、段落结构等）。
 *
 * 核心特性：
 *   - 支持通配符 {{*}} 匹配所有占位符
 *   - 支持精确匹配 {{会议主题}} 替换特定占位符
 *   - 支持批量替换（map<string,string> 一次传入多个）
 *   - 保留 OOXML 中 <w:r> / <w:rPr> 的完整格式
 *   - 详细的错误码与错误信息
 *
 * 使用示例：
 * @code
 *   docx_temp_helper::ReplaceOptions opts;
 *   opts.verbose = true;
 *   auto result = docx_temp_helper::replacePlaceholders(
 *       "input.docx", "{{*}}", "小红有才", "output.docx", opts);
 *   if (!result.ok()) {
 *       std::cerr << result.error.toString() << std::endl;
 *   }
 * @endcode
 */

#pragma once

#include "docx_temp_helper/rich_content.h"

#include <string>
#include <map>
#include <vector>

namespace docx_temp_helper {

// ───────── 错误码与错误信息 ─────────

/// 错误码枚举
enum class ErrorCode {
    Ok = 0,               ///< 成功
    FileNotFound,         ///< 输入文件不存在
    FileNotReadable,      ///< 输入文件无法读取
    InvalidDocxFormat,    ///< 非合法 docx 文件（zip 结构损坏）
    UnzipFailed,          ///< 解压失败
    XmlParseFailed,       ///< XML 解析失败
    XmlSaveFailed,        ///< XML 保存失败
    ZipFailed,            ///< 压缩失败
    TempDirCreateFailed,  ///< 临时目录创建失败
    NoMatchFound,         ///< 未找到匹配的占位符
    InvalidPattern,       ///< 占位符模式格式非法
    OutputWriteFailed,    ///< 输出文件写入失败
    UnknownError          ///< 未知错误
};

/// 错误信息结构体，承载详细错误描述
struct ErrorInfo {
    ErrorCode code = ErrorCode::Ok;  ///< 错误码
    std::string message;             ///< 人可读的错误描述
    std::string detail;              ///< 附加上下文（如文件路径、XML 节点路径等）

    /// 是否成功
    bool ok() const { return code == ErrorCode::Ok; }

    /// 转为字符串（用于日志/打印）
    std::string toString() const;
};

// ───────── 替换选项 ─────────

/// 替换选项
struct ReplaceOptions {
    std::string tempDir;            ///< 临时解压目录，为空则使用系统临时目录
    bool preserveFormatting = true;  ///< 是否保留原始格式（默认保留）
    bool verbose = false;            ///< 是否输出详细日志
    bool keepTempDir = false;        ///< 是否保留临时目录（调试用，默认清理）
};

// ───────── 替换结果 ─────────

/// 单个占位符的替换记录
struct ReplaceRecord {
    std::string placeholder;  ///< 原始占位符，如 "{{会议主题}}"
    std::string replacement;  ///< 替换后的文本
    int count = 0;            ///< 替换次数（同一占位符可能出现多次）
};

/// 整体替换结果
struct ReplaceResult {
    ErrorInfo error;                       ///< 错误信息（ok=成功）
    std::vector<ReplaceRecord> records;    ///< 逐个占位符的替换记录
    int totalReplaced = 0;                 ///< 总替换次数

    /// 是否成功
    bool ok() const { return error.ok(); }
};

// ───────── 公开接口 ─────────

/// 【接口1】批量替换：传入 map<string, string> 一次替换多个占位符
/// @param inputPath    输入 docx 文件路径
/// @param replacements 占位符名称→替换文本 的映射
///                     key 为占位符名称（不含 {{}}），如 "会议主题"
///                     value 为替换文本，如 "小红有才"
///                     若 key 为 "*"，则匹配所有 {{xx}} 并统一替换为对应 value
/// @param outputPath   输出 docx 文件路径
/// @param options      可选参数（临时目录等）
/// @return 替换结果（含错误信息与逐项记录）
ReplaceResult replacePlaceholders(
    const std::string& inputPath,
    const std::map<std::string, std::string>& replacements,
    const std::string& outputPath,
    const ReplaceOptions& options = {}
);

/// 【接口2】单模式替换：传入模式字符串和替换文本
/// @param inputPath   输入 docx 文件路径
/// @param pattern     占位符模式，支持 "{{*}}"（匹配所有）或 "{{具体名称}}"
/// @param replacement 替换文本
/// @param outputPath  输出 docx 文件路径
/// @param options     可选参数（临时目录等）
/// @return 替换结果（含错误信息与逐项记录）
ReplaceResult replacePlaceholders(
    const std::string& inputPath,
    const std::string& pattern,
    const std::string& replacement,
    const std::string& outputPath,
    const ReplaceOptions& options = {}
);

/// 【接口3】富文本批量替换：支持纯文本/HTML/Markdown 混合替换
/// @param inputPath    输入 docx 文件路径
/// @param replacements 占位符名称→富文本替换项 的映射
///                     key 为占位符名称（不含 {{}}），如 "正文" 或 "*"
///                     RichReplacement.type 决定内容类型：
///                       - Plain: 走纯文本替换（同接口1）
///                       - HTML: 解析 HTML 并生成格式化段落
///                       - Markdown: 解析 MD 并生成格式化段落
///                     HTML/Markdown 仅替换独占一行的占位符（替换整个段落）
/// @param outputPath   输出 docx 文件路径
/// @param options      可选参数（临时目录等）
/// @return 替换结果（含错误信息与逐项记录）
ReplaceResult replacePlaceholdersRich(
    const std::string& inputPath,
    const std::map<std::string, RichReplacement>& replacements,
    const std::string& outputPath,
    const ReplaceOptions& options = {}
);

} // namespace docx_temp_helper
