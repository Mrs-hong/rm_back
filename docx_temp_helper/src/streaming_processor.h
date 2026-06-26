/**
 * @file streaming_processor.h
 * @brief 流式 XML 处理器（内部头文件，不对外暴露）
 *
 * 按 <w:p>...</w:p> 段落边界逐段处理 document.xml，
 * 避免全量加载到内存。内存复杂度 O(最大单个段落大小)。
 *
 * 算法：
 *   1. 以 64KB 块读取 document.xml
 *   2. 在缓冲区中扫描 <w:p ...> 标签起始
 *   3. 继续读取直到找到 </w:p> 标签结束
 *   4. 提取完整段落 XML 字符串，调用 handler 处理
 *   5. handler 返回非空字符串则替换，返回空串则保持原文
 *   6. 段落外内容（XML 声明、<w:body>、<w:sectPr> 等）直接透传
 */

#pragma once

#include <string>
#include <functional>

namespace docx_temp_helper::detail {

/// 段落处理器回调函数类型
/// @param paraXml 段落原始 XML 字符串（完整的 <w:p>...</w:p>）
/// @return 处理后的 XML 字符串；若返回空串则保持原文不变
using ParagraphHandler = std::function<std::string(const std::string& paraXml)>;

/// 流式处理 document.xml
/// 按 <w:p>...</w:p> 边界逐段读取，对每段调用 handler
/// 段落外的 XML 内容（header, <w:body>, <w:sectPr> 等）直接透传
/// @param inputXmlPath  输入 XML 文件路径
/// @param outputXmlPath 输出 XML 文件路径
/// @param handler       段落处理回调
/// @return true=成功, false=失败
bool streamProcessXml(const std::string& inputXmlPath,
                      const std::string& outputXmlPath,
                      ParagraphHandler handler);

} // namespace docx_temp_helper::detail
