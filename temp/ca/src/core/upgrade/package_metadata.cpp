//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <memory>
#include <optional>
#include <string>

#include "json/reader.h"
#include "json/value.h"
#include "qifeng_framework/common/logger.h"

#include "common/atomic_write_file.h"
#include "core/upgrade/package_metadata.h"

namespace qifeng_ca {

    namespace {
        // 从 JSON Value 解析单个组件信息, 字段缺失时返回 std::nullopt
        std::optional<ComponentInfo> ParseComponent(const Json::Value &node) {
            if (node.isNull() || !node.isObject()) {
                return std::nullopt;
            }
            ComponentInfo info;
            info.version = node.get("version", "").asString();
            info.dir = node.get("dir", ".").asString();
            if (info.version.empty()) {
                return std::nullopt;
            }
            return info;
        }
    }  // namespace

    bool PackageMetadata::HasAnyComponent() const {
        return screen.has_value() || qifengCa.has_value() || qifengScm.has_value() || model.has_value() ||
               nginx.has_value();
    }

    Status PackageMetadata::ParseFromFile(const std::string &path, PackageMetadata &out) {
        // 1. 读取文件内容
        std::string content;
        Status readStatus = AtomicFileWriter::ReadFile(path, content);
        if (!readStatus.IsSuccess()) {
            SLOG_WARN << "PackageMetadata: read file failed, path=" << path << ", " << readStatus.ToString();
            return readStatus;
        }

        // 2. 解析 JSON
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
        if (!reader->parse(content.c_str(), content.c_str() + content.size(), &root, &errs)) {
            SLOG_ERROR << "PackageMetadata: parse json failed: " << errs << ", content=" << content;
            return Status {-1, "metadata.json 解析失败: " + errs};
        }

        if (!root.isObject()) {
            SLOG_ERROR << "PackageMetadata: root is not object, content=" << content;
            return Status {-1, "metadata.json 根节点不是对象"};
        }

        // 3. 解析顶层字段
        out = {};
        out.version = root.get("version", "").asString();
        out.upgradeTime = root.get("upgrade_time", "").asString();

        if (out.version.empty()) {
            SLOG_WARN << "PackageMetadata: version field is empty, path=" << path;
        }

        // 4. 解析各组件(均为可选)
        out.screen = ParseComponent(root["screen"]);
        out.qifengCa = ParseComponent(root["qifeng_ca"]);
        out.qifengScm = ParseComponent(root["qifeng_scm"]);
        out.model = ParseComponent(root["model"]);
        out.nginx = ParseComponent(root["nginx"]);

        SLOG_INFO << "PackageMetadata: parsed ok, version=" << out.version << ", upgradeTime=" << out.upgradeTime
                  << ", hasScreen=" << out.screen.has_value() << ", hasCa=" << out.qifengCa.has_value()
                  << ", hasScm=" << out.qifengScm.has_value() << ", hasModel=" << out.model.has_value()
                  << ", hasNginx=" << out.nginx.has_value();

        return {};
    }

}  // namespace qifeng_ca
