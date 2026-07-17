/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include "common/types.h"
#include "yaml-cpp/yaml.h"

#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace qifeng::scm::utils {
    class YamlResolve {
    public:
        YamlResolve() = default;
        ~YamlResolve() = default;

        YamlResolve(const YamlResolve &) = delete;
        YamlResolve &operator=(const YamlResolve &) = delete;

        YamlResolve(YamlResolve &&) = default;
        YamlResolve &operator=(YamlResolve &&) = default;

    public:
        /**
         * @brief 初始化 YAML 解析器
         * @param filePath YAML 文件路径
         * @return ResultMsg 操作结果，失败时包含错误信息
         */
        ResultMsg Init(const std::string &filePath);

        /**
         * @brief 判断指定路径的节点是否存在
         * @param nodeName 节点路径，例如 "resources.ports"
         * @return bool 节点存在且已定义返回 true
         */
        bool HasNode(const std::string &nodeName) const noexcept;

        /**
         * @brief 获取 YAML 节点的值
         * @param nodeName 节点路径，例如 "services.service1.port"
         * @param defaultValue 默认值(无该节点、异常、类型不匹配等返回默认值)
         * @return T 节点值
         */
        template <typename T>
        T GetNodeValue(const std::string &nodeName, T defaultValue) noexcept;

        /**
         * @brief 获取 YAML 节点的值（可区分缺失与异常）
         * @param nodeName 节点路径，例如 "execution.workDir"
         * @return std::optional<T> 节点存在且解析成功返回有值；节点缺失或解析异常返回 nullopt
         */
        template <typename T>
        std::optional<T> GetOptionalNodeValue(const std::string &nodeName) const noexcept;

        /**
         * @brief 获取指定路径的 YAML 节点（深拷贝）
         * @details 返回克隆后的 Node 对象，可安全用于列表/对象遍历，
         *          不影响内部 mConfigFileNode 状态
         * @param nodeName 节点路径，例如 "resources.requires"
         * @return std::optional<YAML::Node> 节点存在返回克隆的 Node，否则 nullopt
         */
        std::optional<YAML::Node> GetNode(const std::string &nodeName) const;

        /**
         * @brief 获取列表节点的所有值
         * @details 适用于简单标量列表，如 ports: [80, 443]
         * @param nodeName 节点路径，例如 "resources.ports"
         * @return std::vector<T> 列表值列表，节点不存在或非列表时返回空
         */
        template <typename T>
        std::vector<T> GetListValues(const std::string &nodeName) const noexcept;

    private:
        /**
         * @brief 按路径遍历节点，返回目标节点（深拷贝）
         * @param nodeName 节点路径，例如 "a.b.c"
         * @return std::optional<YAML::Node> 找到返回克隆的节点，否则 nullopt
         */
        std::optional<YAML::Node> TraverseNode(const std::string &nodeName) const;

    private:
        YAML::Node mConfigFileNode;
        bool mInitialized = false;
    };

    inline ResultMsg YamlResolve::Init(const std::string &filePath) {
        try {
            mConfigFileNode = YAML::LoadFile(filePath);
        } catch (const YAML::ParserException &e) {
            return MakeError("YamlResolve Init parser exception: " + std::string(e.what()));
        } catch (const YAML::BadFile &e) {
            return MakeError("YamlResolve Init bad file exception: " + std::string(e.what()));
        }
        mInitialized = true;
        return {0, "YamlResolve Init success"};
    }

    inline std::optional<YAML::Node> YamlResolve::TraverseNode(const std::string &nodeName) const {
        if (!mInitialized) {
            return std::nullopt;
        }
        try {
            // 使用 Clone 深拷贝，避免非 const operator[] 修改 mConfigFileNode 内部节点
            // yaml-cpp 的 Node 浅拷贝共享 m_pNode，operator[] 会修改共享的内部结构
            YAML::Node node = YAML::Clone(mConfigFileNode);
            std::stringstream ss(nodeName);
            std::string segment;
            while (std::getline(ss, segment, '.')) {
                if (!node || !node.IsMap()) {
                    return std::nullopt;
                }
                node = node[segment];
            }
            if (!node || !node.IsDefined()) {
                return std::nullopt;
            }
            return node;
        } catch (...) {
            return std::nullopt;
        }
    }

    inline bool YamlResolve::HasNode(const std::string &nodeName) const noexcept {
        return TraverseNode(nodeName).has_value();
    }

    template <typename T>
    inline T YamlResolve::GetNodeValue(const std::string &nodeName, T defaultValue) noexcept {
        auto nodeOpt = TraverseNode(nodeName);
        if (!nodeOpt) {
            return defaultValue;
        }
        try {
            return nodeOpt->as<T>();
        } catch (...) {
            return defaultValue;
        }
    }

    template <typename T>
    inline std::optional<T> YamlResolve::GetOptionalNodeValue(const std::string &nodeName) const noexcept {
        auto nodeOpt = TraverseNode(nodeName);
        if (!nodeOpt) {
            return std::nullopt;
        }
        try {
            return nodeOpt->as<T>();
        } catch (...) {
            return std::nullopt;
        }
    }

    inline std::optional<YAML::Node> YamlResolve::GetNode(const std::string &nodeName) const {
        return TraverseNode(nodeName);
    }

    template <typename T>
    inline std::vector<T> YamlResolve::GetListValues(const std::string &nodeName) const noexcept {
        auto nodeOpt = TraverseNode(nodeName);
        if (!nodeOpt || !nodeOpt->IsSequence()) {
            return {};
        }
        try {
            std::vector<T> result;
            for (const auto &item : *nodeOpt) {
                result.push_back(item.as<T>());
            }
            return result;
        } catch (...) {
            return {};
        }
    }

    /**
     * @brief 从 service.yaml 读取 initDB_sql_dir 字段
     * @param yamlPath service.yaml 完整路径
     * @return std::string sqlDir 相对路径，未配置或解析失败返回空字符串
     */
    inline std::string ReadInitSqlDir(const std::string &yamlPath) {
        YamlResolve resolver;
        if (!resolver.Init(yamlPath).IsDefalutSuccess()) {
            return "";
        }
        return resolver.GetNodeValue<std::string>("initDB_sql_dir", "");
    }

    /**
     * @brief 从 service.yaml 读取 serviceName 字段
     * @param yamlPath service.yaml 完整路径
     * @return std::string 服务名称，未配置或解析失败返回空字符串
     */
    inline std::string ReadServiceName(const std::string &yamlPath) {
        YamlResolve resolver;
        if (!resolver.Init(yamlPath).IsDefalutSuccess()) {
            return "";
        }
        return resolver.GetNodeValue<std::string>("serviceName", "");
    }

    /**
     * @brief 从 service.yaml 读取 version 字段
     * @param yamlPath service.yaml 完整路径
     * @return std::string 版本号，未配置或解析失败返回空字符串
     */
    inline std::string ReadVersion(const std::string &yamlPath) {
        YamlResolve resolver;
        if (!resolver.Init(yamlPath).IsDefalutSuccess()) {
            return "";
        }
        return resolver.GetNodeValue<std::string>("version", "");
    }
}  // namespace qifeng::scm::utils
