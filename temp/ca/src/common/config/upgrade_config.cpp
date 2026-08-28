//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <sstream>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "common/atomic_write_file.h"
#include "common/config/upgrade_config.h"

namespace qifeng_ca {

    // 读取当前已安装版本号: 从 data/.version 的 VERSION= 字段解析
    // 文件不存在或无 VERSION= 字段时返回 "0.0.0", 使首次升级包总是被视为新版本
    std::string UpgradeConfig::GetCurrentVersion() const {
        return GetVersionField(kFieldMainVersion, "0.0.0");
    }

    // 读取 .version 中指定字段的值
    // 文件格式为 key=value 逐行, 精确匹配行首 "key=" (如 "VERSION=" 不会误匹配 "SMA_VERSION=")
    // 文件不存在/字段不存在/值为空时返回 defaultValue
    std::string UpgradeConfig::GetVersionField(const std::string &key, const std::string &defaultValue) const {
        std::string path = GetVersionFilePath();
        std::string content;
        if (!AtomicFileWriter::ReadFile(path, content).IsSuccess()) {
            return defaultValue;
        }

        const std::string prefix = key + "=";
        std::istringstream iss(content);
        std::string line;
        while (std::getline(iss, line)) {
            // 去除行尾 \r (兼容 CRLF)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            // 精确匹配行首 "key=", 避免误匹配以 key 为后缀的其他字段(如 VERSION 与 SMA_VERSION)
            if (line.compare(0, prefix.size(), prefix) == 0) {
                std::string value = line.substr(prefix.size());
                // trim 首尾空白
                auto begin = value.find_first_not_of(" \t");
                if (begin == std::string::npos) {
                    return defaultValue;
                }
                auto end = value.find_last_not_of(" \t");
                return value.substr(begin, end - begin + 1);
            }
        }
        return defaultValue;
    }

    // 批量写入 .version 字段: 逐行重建文件内容, 替换命中的字段行, 未涉及的字段保持不变
    // 文件中不存在的字段追加到末尾; 单次原子写入, 保证多字段更新的一致性
    bool UpgradeConfig::SetVersionFields(const std::vector<std::pair<std::string, std::string>> &fields) const {
        if (fields.empty()) {
            return true;
        }
        std::string path = GetVersionFilePath();
        std::string content;
        Status readStatus = AtomicFileWriter::ReadFile(path, content);

        std::ostringstream oss;
        // 记录每个字段是否已在原文件中找到并替换
        std::vector<bool> written(fields.size(), false);

        if (readStatus.IsSuccess()) {
            std::istringstream iss(content);
            std::string line;
            while (std::getline(iss, line)) {
                // 去除行尾 \r (统一为 LF)
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                bool replaced = false;
                for (size_t i = 0; i < fields.size(); ++i) {
                    const std::string prefix = fields[i].first + "=";
                    // 精确匹配行首 "key=", 避免误匹配同后缀字段(如 VERSION 与 SMA_VERSION)
                    if (line.compare(0, prefix.size(), prefix) == 0) {
                        oss << fields[i].first << "=" << fields[i].second << "\n";
                        written[i] = true;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    oss << line << "\n";
                }
            }
        }

        // 原文件中不存在的字段追加到末尾
        for (size_t i = 0; i < fields.size(); ++i) {
            if (!written[i]) {
                oss << fields[i].first << "=" << fields[i].second << "\n";
            }
        }

        if (!AtomicFileWriter::WriteAtomic(path, oss.str(), 0644)) {
            SLOG_ERROR << "UpgradeConfig: write version file failed, path=" << path;
            return false;
        }
        SLOG_INFO << "UpgradeConfig: version file updated, fields=" << fields.size() << ", path=" << path;
        return true;
    }

    // 读取子版本号(U_VERSION): 记录基于当前主版本完成升级的次数, 默认 "0000"
    std::string UpgradeConfig::GetSubVersion() const {
        return GetVersionField(kFieldSubVersion, "0000");
    }

    // 读取组件版本号(V_SCREEN/V_CA/V_SCM/V_MODEL/V_NGINX), 默认 "0.0.0"
    // 组件字段缺失时返回默认值, 使任何正版本组件均可通过升级门槛
    std::string UpgradeConfig::GetComponentVersion(const std::string &fieldKey) const {
        return GetVersionField(fieldKey, "0.0.0");
    }

}  // namespace qifeng_ca
