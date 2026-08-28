//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pugixml.hpp"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"

#include "common/utils/docx/xml_utils.h"
#include "common/utils/docx/zip_utils.h"
#include "common/utils/template_file_reader.h"

namespace qifeng_ca {

    // ===================== 二进制伪装校验 =====================

    // 检查文件前8KB是否含NUL字节, 含则判定为二进制伪装
    static bool IsTextFile(const std::string &filePath) {
        std::ifstream ifs(filePath, std::ios::binary);
        if (!ifs.is_open()) {
            return false;
        }
        constexpr size_t kCheckSize = 8192;
        std::array<char, kCheckSize> buf {};
        ifs.read(buf.data(), kCheckSize);
        size_t readSize = static_cast<size_t>(ifs.gcount());
        for (size_t i = 0; i < readSize; ++i) {
            if (buf[i] == '\0') {
                return false;
            }
        }
        return true;
    }

    // ===================== txt/md 解析 =====================

    static std::pair<bool, std::string> ParsePlainText(const std::string &filePath) {
        if (!IsTextFile(filePath)) {
            SLOG_WARN << "ParsePlainText: binary data detected, path=" << filePath;
            return std::make_pair(false, "");
        }
        std::ifstream ifs(filePath);
        if (!ifs.is_open()) {
            SLOG_WARN << "ParsePlainText: open file failed, path=" << filePath;
            return std::make_pair(false, "");
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        SLOG_INFO << "ParsePlainText: textLen=" << content.size();
        return std::make_pair(true, content);
    }

    // ===================== docx 解析(zip解压 + XML解析) =====================

    // 从 pStyle 的 w:val 判断标题级别, 非标题返回0
    static int GetHeadingLevelFromStyle(const std::string &styleVal) {
        static const std::unordered_map<std::string, int> kHeadingMap = {
            {"Title", 1}, {"Subtitle", 1}, {"Heading1", 1}, {"1", 1},        {"Heading2", 2},
            {"2", 2},     {"Heading3", 3}, {"3", 3},        {"Heading4", 4}, {"4", 4},
        };
        auto it = kHeadingMap.find(styleVal);
        return (it == kHeadingMap.end()) ? 0 : it->second;
    }

    // 从 <w:outlineLvl w:val="N"/> 获取标题级别(N=0~8 对应 H1~H9)
    static int GetOutlineLevel(pugi::xml_node pPr) {
        pugi::xml_node outlineLvl = pPr.child("w:outlineLvl");
        if (!outlineLvl) {
            return 0;
        }
        int level = outlineLvl.attribute("w:val").as_int(-1);
        if (level < 0 || level > 8) {
            return 0;
        }
        return level + 1;
    }

    // 综合 pStyle 和 outlineLvl 判断段落标题级别
    static int GetParagraphHeadingLevel(pugi::xml_node p) {
        pugi::xml_node pPr = p.child("w:pPr");
        if (!pPr) {
            return 0;
        }
        int level = GetOutlineLevel(pPr);
        if (level > 0) {
            return level;
        }
        pugi::xml_node pStyle = pPr.child("w:pStyle");
        if (pStyle) {
            return GetHeadingLevelFromStyle(pStyle.attribute("w:val").as_string());
        }
        return 0;
    }

    static std::string FormatParagraphLine(int headingLevel, const std::string &text) {
        if (headingLevel <= 0) {
            return text;
        }
        return std::string(headingLevel, '#') + " " + text;
    }

    // 递归收集节点下所有 <w:r> 文本, 遇到 <w:tab/> 和 <w:br/> 保留空白/换行
    // 同时处理域代码文本 <w:instrText> 和修订删除文本 <w:delText>
    static void CollectRunsRecursive(pugi::xml_node node, std::string &outText) {
        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
            const char* name = child.name();
            if (std::strcmp(name, "w:r") == 0) {
                for (pugi::xml_node rChild = child.first_child(); rChild; rChild = rChild.next_sibling()) {
                    const char* rName = rChild.name();
                    if (std::strcmp(rName, "w:t") == 0 || std::strcmp(rName, "w:instrText") == 0 ||
                        std::strcmp(rName, "w:delText") == 0) {
                        outText += rChild.text().get();
                    } else if (std::strcmp(rName, "w:tab") == 0) {
                        outText += "\t";
                    } else if (std::strcmp(rName, "w:br") == 0) {
                        outText += "\n";
                    }
                }
            } else {
                CollectRunsRecursive(child, outText);
            }
        }
    }

    static void ProcessParagraph(pugi::xml_node p, std::string &outText) {
        std::string line;
        CollectRunsRecursive(p, line);
        if (line.empty()) {
            return;
        }

        int level = GetParagraphHeadingLevel(p);
        std::string formatted = FormatParagraphLine(level, line);
        if (!outText.empty()) {
            outText += "\n";
        }
        outText += formatted;
    }

    // 递归收集 body/表格/内容控件/文本框等结构内的所有 w:p 段落
    static void CollectParagraphsRecursive(pugi::xml_node node, std::string &outText) {
        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
            if (std::strcmp(child.name(), "w:p") == 0) {
                ProcessParagraph(child, outText);
            } else {
                CollectParagraphsRecursive(child, outText);
            }
        }
    }

    // 解析 document.xml: 递归遍历 body 下所有段落(含表格/内容控件/文本框内段落),
    // 按 pStyle/outlineLvl 判断标题级别, 提取所有 run 文本
    static std::string ParseDocxXml(const std::string &xmlPath) {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());
        if (!result) {
            SLOG_WARN << "ParseDocxXml: load xml failed, " << result.description();
            return "";
        }

        pugi::xml_node body = doc.child("w:document").child("w:body");
        if (!body) {
            SLOG_WARN << "ParseDocxXml: w:body not found";
            return "";
        }

        std::string text;
        CollectParagraphsRecursive(body, text);
        return text;
    }

    static std::pair<bool, std::string> ParseDocx(const std::string &filePath) {
        std::string tempDir = filePath + "_unzip";
        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
        std::filesystem::create_directories(tempDir, ec);

        ScopeExit cleanup([&tempDir]() {
            std::error_code ignore;
            std::filesystem::remove_all(tempDir, ignore);
        });

        // 尝试解压, 失败则判定为伪装文件
        if (!docx::UnzipToDir(filePath, tempDir)) {
            SLOG_WARN << "ParseDocx: unzip failed (invalid docx), path=" << filePath;
            return std::make_pair(false, "");
        }

        std::string xmlPath = tempDir + "/word/document.xml";
        if (!std::filesystem::exists(xmlPath, ec)) {
            SLOG_WARN << "ParseDocx: document.xml not found";
            return std::make_pair(false, "");
        }

        std::string text = ParseDocxXml(xmlPath);
        SLOG_DEBUG << "ParseDocx: textLen=" << text.size() << ", text=" << text;
        return std::make_pair(true, text);
    }

    // ===================== TemplateFileReader 实现 =====================

    TemplateFileReader &TemplateFileReader::GetInstance() {
        static TemplateFileReader Instance;
        return Instance;
    }

    TemplateFileReader::TemplateFileReader() {
        mReaders["docx"] = [](const std::string &filePath) { return ParseDocx(filePath); };
        mReaders["txt"] = [](const std::string &filePath) { return ParsePlainText(filePath); };
        mReaders["md"] = [](const std::string &filePath) { return ParsePlainText(filePath); };
    }

    std::pair<bool, std::string> TemplateFileReader::Read(const std::string &filePath, const std::string &ext) const {
        if (filePath.empty() || !std::filesystem::exists(filePath)) {
            SLOG_WARN << "TemplateFileReader::Read: file not found, path=" << filePath;
            return std::make_pair(false, "文件不存在");
        }
        auto it = mReaders.find(ext);
        if (it == mReaders.end()) {
            SLOG_WARN << "TemplateFileReader::Read: unsupported ext=" << ext;
            return std::make_pair(false, "不支持的文件格式");
        }
        auto [success, text] = it->second(filePath);
        return success ? std::make_pair(true, text) : std::make_pair(false, "异常文件");
    }

    bool TemplateFileReader::IsSupported(const std::string &ext) const {
        return mReaders.find(ext) != mReaders.end();
    }

}  // namespace qifeng_ca
