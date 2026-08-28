//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

/**
 * @file rich_content.h
 * @brief 富文本内容解析与渲染模块
 *
 * 将 HTML/Markdown 内容解析为结构化段落，再渲染为 OOXML <w:p> 元素，
 * 用于替换 docx 模板中独占一行的 {{正文}} 等占位符。
 *
 * 支持的格式：
 *   - Markdown: 标题(#~####)、加粗(**text**)、斜体(*text*)、
 *               无序列表(- 或 *)、有序列表(1.)、段落、空行分隔
 *   - HTML: <p>, <h1>~<h4>, <b>/<strong>, <i>/<em>, <u>,
 *           <ul>/<ol>/<li>, <br>
 *   - Editor.js JSON: {"blocks":[...]} 格式, 支持 header/paragraph/list,
 *           列表项 content 字段内的内联 HTML(<b>/<i>/<u>)也会被解析
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
#include <memory>

namespace qifeng_ca::docx {

// ───────── 内容类型枚举 ─────────

/// 富文本内容类型
enum class ContentType {
    Plain,     ///< 纯文本（走原有文本替换逻辑）
    HTML,      ///< HTML 格式
    Markdown,  ///< Markdown 格式
    EditorJS   ///< Editor.js JSON 格式（{"blocks":[...]}）
};

// ───────── 结构化内容模型 ─────────

/// 富文本 run（带格式的文本片段）
struct RichRun {
    std::string text;       ///< 文本内容
    bool bold = false;      ///< 加粗
    bool italic = false;    ///< 斜体
    bool underline = false; ///< 下划线
};

/// 富文本表格单元格（对应一个 OOXML <w:tc>）
struct RichTableCell {
    std::vector<RichRun> runs;  ///< 单元格内容 run 列表（支持内联格式，可为空）
};

/// 富文本表格（对应一个 OOXML <w:tbl>）
struct RichTable {
    std::vector<std::vector<RichTableCell>> rows;  ///< 行数据，rows[r][c] 为第 r 行第 c 列单元格
    bool hasHeadingRow = false;                    ///< 首行是否为表头（加粗+底纹+居中，跨页重复显示）
};

/// 富文本块：普通段落或表格（取决于 table 字段）
struct RichParagraph {
    std::vector<RichRun> runs;  ///< 段落中的 run 列表（table 非空时忽略）
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
    /// 表格数据（非空时此块渲染为 <w:tbl> 表格元素，上述段落字段被忽略）
    std::shared_ptr<RichTable> table;
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
std::vector<RichParagraph> ParseMarkdown(const std::string& md);

/// 解析 HTML 字符串为结构化段落列表
/// @param html HTML 文本
/// @return 段落列表
std::vector<RichParagraph> ParseHtml(const std::string& html);

/// 解析 Editor.js JSON 字符串为结构化段落列表
/// @param json Editor.js 输出格式 {"time":..., "blocks":[{type, data}]}
/// @return 段落列表；解析失败或无 blocks 返回空
/// @note 支持的 block 类型: header(h1-h4)、paragraph、list(有序/无序, 支持嵌套)、
///       table(渲染为真正的 OOXML <w:tbl> 表格: 全边框/列宽均分/表头加粗灰底/跨页重复表头)
/// @note 列表项 content 字段内的内联 HTML(<b>/<i>/<u>)会被解析为富文本 runs
/// @note 其他未支持的 block 类型(quote/code/delimiter/image 等)会被跳过
std::vector<RichParagraph> ParseEditorJs(const std::string& json);

// ───────── 渲染函数 ─────────

/// 将结构化段落渲染为 OOXML <w:p> 元素并插入到文档中
/// @param parent 父节点（通常是 <w:body> 或表格单元格）
/// @param paragraphs 段落列表
/// @param placeholderParagraph 占位符所在的 <w:p> 节点
/// @note 新段落插入到 placeholderParagraph 之前，之后删除 placeholderParagraph
/// @note 仅用于 DOM 模式；流式模式请使用 SerializeParagraphs
void RenderParagraphsToXml(pugi::xml_node parent,
                           const std::vector<RichParagraph>& paragraphs,
                           pugi::xml_node placeholderParagraph);

/// 将结构化段落列表序列化为 XML 字符串（不依赖 parent 节点）
/// @param paragraphs 段落列表
/// @return XML 字符串，包含多个 <w:p> 元素的拼接
/// @note 用于流式模式下替换占位符段落
std::string SerializeParagraphs(const std::vector<RichParagraph>& paragraphs);

/// 将结构化段落渲染为 OOXML <w:p> 元素并插入到指定节点之前
/// @param parent 父节点（通常是 <w:body>）
/// @param paragraphs 段落列表
/// @param beforeNode 新段落将插入到此节点之前（如 <w:sectPr>），为空则追加到末尾
/// @note 用于从空白模板生成文档（generateDocument）
void AppendParagraphsBefore(pugi::xml_node parent,
                             const std::vector<RichParagraph>& paragraphs,
                             pugi::xml_node beforeNode);

} // namespace qifeng_ca::docx
