//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_UTILS_TEMPLATE_FILE_READER_H
#define QIFENG_CA_INCLUDE_COMMON_UTILS_TEMPLATE_FILE_READER_H

#include <functional>
#include <string>
#include <unordered_map>

namespace qifeng_ca {

    // 模板文件读取器: 根据文件扩展名分发到不同的解析方法
    // - docx: zip解压 + XML解析, 提取段落文本和H1/H2/H3标题级别
    // - txt/md: 纯文本读取, 校验无NUL字节等二进制伪装
    // 其他扩展名不支持, Read 返回空, IsSupported 返回 false
    class TemplateFileReader {
    public:
        static TemplateFileReader &GetInstance();

        // 读取模板文件, 根据扩展名分发到对应解析器
        // 不支持的扩展名返回空字符串
        std::pair<bool, std::string> Read(const std::string &filePath, const std::string &ext) const;

        // 校验扩展名是否支持
        bool IsSupported(const std::string &ext) const;

    private:
        TemplateFileReader();

        using ReadFunc = std::function<std::pair<bool, std::string>(const std::string &)>;
        std::unordered_map<std::string, ReadFunc> mReaders;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_UTILS_TEMPLATE_FILE_READER_H
