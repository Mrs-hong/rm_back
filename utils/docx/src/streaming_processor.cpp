/**
 * @file streaming_processor.cpp
 * @brief 流式 XML 处理器实现
 *
 * 按 <w:p>...</w:p> 段落边界逐段处理 document.xml。
 * 使用状态机：在段落外扫描 <w:p 标签，在段落内扫描 </w:p> 结束标签。
 * 内存复杂度：O(最大单个段落大小)，典型段落 < 10KB。
 */

#include "streaming_processor.h"

#include <fstream>
#include <vector>

namespace utils::docx::detail {

/// 判断字符是否可作为 <w:p 标签后的合法分隔符
/// <w:p> <w:p  <w:p/ <w:p\t <w:p\n 均合法；<w:pPr 不合法
static inline bool IsValidParaTagChar(char c) {
    return c == '>' || c == ' ' || c == '/' ||
           c == '\t' || c == '\n' || c == '\r';
}

bool StreamProcessXml(const std::string& inputXmlPath,
                      const std::string& outputXmlPath,
                      ParagraphHandler handler) {
    std::ifstream in(inputXmlPath, std::ios::binary);
    if (!in.is_open()) return false;

    std::ofstream out(outputXmlPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;

    const size_t CHUNK_SIZE = 64 * 1024;
    std::string buffer;  ///< 未处理的累积数据
    buffer.reserve(CHUNK_SIZE * 2);
    std::vector<char> chunk(CHUNK_SIZE);
    bool inParagraph = false;  ///< 是否处于 <w:p>...</w:p> 内部

    /// 读取下一块数据到 buffer，返回是否读到数据
    auto readMore = [&]() -> bool {
        in.read(chunk.data(), CHUNK_SIZE);
        std::streamsize n = in.gcount();
        if (n > 0) {
            buffer.append(chunk.data(), static_cast<size_t>(n));
            return true;
        }
        return false;
    };

    while (true) {
        // buffer 为空时尝试读取更多数据
        if (buffer.empty()) {
            if (!readMore()) break;  // EOF
        }

        if (!inParagraph) {
            // ── 段落外：扫描 <w:p 标签起始 ──
            size_t pStart = buffer.find("<w:p");

            if (pStart == std::string::npos) {
                // 未找到 <w:p，保留最后 4 字节以防跨块边界拆分
                if (buffer.size() <= 4) {
                    // 数据不足，需读取更多
                    if (!readMore()) {
                        // EOF：写出全部剩余内容
                        out.write(buffer.data(), buffer.size());
                        buffer.clear();
                        break;
                    }
                    continue;
                }
                size_t writeLen = buffer.size() - 4;
                out.write(buffer.data(), writeLen);
                buffer = buffer.substr(writeLen);
                // 继续读取
                if (!readMore()) {
                    // EOF：写出保留的最后 4 字节
                    out.write(buffer.data(), buffer.size());
                    buffer.clear();
                    break;
                }
                continue;
            }

            // 写出 <w:p 之前的内容（XML 声明、<w:body> 等）
            if (pStart > 0) {
                out.write(buffer.data(), pStart);
            }
            buffer = buffer.substr(pStart);

            // 检查是否为合法的 <w:p> 标签（而非 <w:pPr 等）
            if (buffer.size() < 5) {
                // 数据不足以判断，读取更多
                if (!readMore()) {
                    // EOF：作为普通内容输出
                    out.write(buffer.data(), buffer.size());
                    buffer.clear();
                    break;
                }
            }

            char c = buffer[4];  // "<w:p" 后的字符
            if (!IsValidParaTagChar(c)) {
                // 不是 <w:p> 标签（如 <w:pPr），写出 "<w:p" 后继续扫描
                out.write("<w:p", 4);
                buffer = buffer.substr(4);
                continue;
            }

            // 合法段落起始，进入段落处理
            inParagraph = true;
            // 继续到段落内处理
        }

        // ── 段落内：扫描 </w:p> 结束标签 ──
        size_t pEnd = buffer.find("</w:p>");
        if (pEnd == std::string::npos) {
            // 未找到结束标签，需读取更多数据
            // buffer 可能较大（大段落），这是预期行为
            if (!readMore()) {
                // EOF 但段落未闭合（异常 XML）：原样输出
                out.write(buffer.data(), buffer.size());
                buffer.clear();
                inParagraph = false;
                break;
            }
            continue;
        }

        // 完整段落找到
        size_t paraEnd = pEnd + 6;  // "</w:p>" 长度
        std::string paraXml(buffer, 0, paraEnd);

        // 调用处理器回调
        std::string processed = handler(paraXml);
        if (!processed.empty()) {
            out.write(processed.data(), processed.size());
        } else {
            // 返回空串表示保持原文不变
            out.write(paraXml.data(), paraXml.size());
        }

        // 从 buffer 移除已处理的段落
        buffer = buffer.substr(paraEnd);
        inParagraph = false;
    }

    // 写出任何剩余内容
    if (!buffer.empty()) {
        out.write(buffer.data(), buffer.size());
    }

    return true;
}

} // namespace utils::docx::detail
