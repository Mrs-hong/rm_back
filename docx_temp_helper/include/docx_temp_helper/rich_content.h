/**
 * @file rich_content.h
 * @brief 富文本内容解析与渲染模块
 *
 * 将 HTML/Markdown 内容解析为结构化段落，再渲染为 OOXML <w:p> 元素，
 * 用于替换 docx 模板中独占一行的 {{正文}} 等占位符。
 *
 * 支持的格式：
 *   - Markdown: 标题(#~####)、加粗(**text**)、斜体(*text*)、
 *               无序列表(- /*)、有序列表(1.)、段落、空行分隔
 *   - HTML: <p>, <h1>~<h4>, <b>/<strong>, <i>/<em>, <u>,
 *           <ul>/<ol>/<li>, <br>
 *
 * 格式标准参考 GB/T 9704-2012（党政机关公文格式）：
 *   - h1: 方正小标宋简体 22pt 居中
 *   - h2: 黑体 16pt 左对齐
 *   - h3: 楷体 16pt 左对齐
 *   - h4: 仿宋 16pt 左对齐
 *   - 正文: 仿宋 16pt 首行缩进2字符 两端对齐
 *   - 列表: 仿宋 16pt 悬挂缩进
 *   - 西文: Times New Roman
 */

#pragma once

#include <pugixml.hpp>

#include <string>
#include <vector>
#include <map>

namespace docx_temp_helper {

// ───────── 内容类型枚举 ─────────

/// 富文本内容类型
enum class ContentType {
    Plain,     ///< 纯文本（走原有文本替换逻辑）
    HTML,      ///< HTML 格式
    Markdown   ///< Markdown 格式
};

// ───────── 结构化内容模型 ─────────

/// 富文本 run（带格式的文本片段）
struct RichRun {
    std::string text;       ///< 文本内容
    bool bold = false;      ///< 加粗
    bool italic = false;    ///< 斜体
    bool underline = false; ///< 下划线
};

/// 富文本段落（对应一个 OOXML <w:p>）
struct RichParagraph {
    std::vector<RichRun> runs;  ///< 段落中的 run 列表
    int headingLevel = 0;       ///< 标题级别：0=正文, 1-4=h1-h4
    bool isListItem = false;    ///< 是否为列表项
    bool isOrdered = false;     ///< 是否为有序列表（仅 isListItem=true 时有效）
    int listIndex = 0;          ///< 有序列表序号（从1开始）
    std::string alignment;      ///< 对齐方式: "left"/"center"/"right"/"justify"
    int firstLineIndent = 0;    ///< 首行缩进（twips，1pt=20twips）
    int leftIndent = 0;         ///< 左缩进（twips）
    int hangingIndent = 0;      ///< 悬挂缩进（twips）
    int spaceAfter = 0;         ///< 段后间距（twips）
    double lineSpacing = 1.0;   ///< 行距倍数
};

/// 富文本替换项
struct RichReplacement {
    std::string content;              ///< 内容字符串
    ContentType type = ContentType::Plain;  ///< 内容类型
};

// ───────── 解析函数 ─────────

/// 解析 Markdown 字符串为结构化段落列表
/// @param md Markdown 文本
/// @return 段落列表
std::vector<RichParagraph> parseMarkdown(const std::string& md);

/// 解析 HTML 字符串为结构化段落列表
/// @param html HTML 文本
/// @return 段落列表
std::vector<RichParagraph> parseHtml(const std::string& html);

// ───────── 渲染函数 ─────────

/// 将结构化段落渲染为 OOXML <w:p> 元素并插入到文档中
/// @param parent 父节点（通常是 <w:body> 或表格单元格）
/// @param paragraphs 段落列表
/// @param placeholderParagraph 占位符所在的 <w:p> 节点
/// @note 新段落插入到 placeholderParagraph 之前，之后删除 placeholderParagraph
/// @note 仅用于 DOM 模式；流式模式请使用 serializeParagraphs
void renderParagraphsToXml(pugi::xml_node parent,
                           const std::vector<RichParagraph>& paragraphs,
                           pugi::xml_node placeholderParagraph);

/// 将结构化段落列表序列化为 XML 字符串（不依赖 parent 节点）
/// @param paragraphs 段落列表
/// @return XML 字符串，包含多个 <w:p> 元素的拼接
/// @note 用于流式模式下替换占位符段落
std::string serializeParagraphs(const std::vector<RichParagraph>& paragraphs);

/// 将结构化段落渲染为 OOXML <w:p> 元素并插入到指定节点之前
/// @param parent 父节点（通常是 <w:body>）
/// @param paragraphs 段落列表
/// @param beforeNode 新段落将插入到此节点之前（如 <w:sectPr>），为空则追加到末尾
/// @note 用于从空白模板生成文档（generateDocument）
void appendParagraphsBefore(pugi::xml_node parent,
                             const std::vector<RichParagraph>& paragraphs,
                             pugi::xml_node beforeNode);

} // namespace docx_temp_helper
