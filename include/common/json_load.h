/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <json/json.h>

#include <string>
#include <type_traits>

namespace qifeng::scm {

    /**
     * @brief JSON 文件加载与取值工具类
     * @details 封装 jsoncpp 读取，提供带默认值的安全取值。
     *          实例由 ScmServer 管理，传入 CheckerRunner / CheckerRegistry 使用。
     *          文件不存在或解析失败返回 false 并告警，调用方可继续用默认配置。
     */
    class JsonLoad {
    public:
        JsonLoad() = default;
        ~JsonLoad() = default;
        JsonLoad(const JsonLoad &) = default;
        JsonLoad &operator=(const JsonLoad &) = default;
        JsonLoad(JsonLoad &&) = default;
        JsonLoad &operator=(JsonLoad &&) = default;

        /**
         * @brief 从文件加载 JSON
         * @param path JSON 文件路径
         * @return true 加载成功；false 文件不存在或解析失败（会告警）
         */
        bool LoadFromFile(const std::string &path);

        /**
         * @brief 从字符串加载 JSON
         * @param content JSON 文本
         * @return true 解析成功；false 解析失败
         */
        bool LoadFromString(const std::string &content);

        /**
         * @brief 是否已加载有效的根对象
         */
        bool IsValid() const { return mValid; }

        /**
         * @brief 根对象（只读）。未加载有效数据时返回空 Value。
         */
        const Json::Value &Root() const { return mRoot; }

        /**
         * @brief 根对象是否包含某 key
         */
        bool Has(const std::string &key) const { return mValid && mRoot.isMember(key); }

        /**
         * @brief 取子对象，不存在返回空 Value
         */
        Json::Value Get(const std::string &key) const;

        /**
         * @brief 取标量值带默认（支持 string/int/bool/double）
         * @param key 根对象下的字段名
         * @param def 默认值
         * @return 字段值或默认值
         */
        template <class T>
        T GetOr(const std::string &key, const T &def) const {
            if (!mValid || !mRoot.isMember(key)) {
                return def;
            }
            return GetTyped<T>(mRoot[key], def);
        }

        /**
         * @brief 取子对象内某标量字段
         * @param parentKey 父字段名（根对象下）
         * @param childKey 子字段名
         * @param def 默认值
         * @return 字段值或默认值
         */
        template <class T>
        T GetOr(const std::string &parentKey, const std::string &childKey, const T &def) const {
            if (!mValid || !mRoot.isMember(parentKey)) {
                return def;
            }
            const Json::Value &parent = mRoot[parentKey];
            if (!parent.isMember(childKey)) {
                return def;
            }
            return GetTyped<T>(parent[childKey], def);
        }

    private:
        /**
         * @brief 按 T 类型从 Json::Value 取值，类型不符返回 def
         * @details 利用 if constexpr 在编译期分派 jsoncpp 对应的取值方法
         */
        template <class T>
        static T GetTyped(const Json::Value &v, const T &def) {
            if constexpr (std::is_same_v<T, std::string>) {
                return v.isString() ? v.asString() : def;
            } else if constexpr (std::is_same_v<T, int>) {
                return v.isInt() ? v.asInt() : def;
            } else if constexpr (std::is_same_v<T, bool>) {
                return v.isBool() ? v.asBool() : def;
            } else if constexpr (std::is_same_v<T, double>) {
                return v.isDouble() ? v.asDouble() : def;
            } else {
                return def;
            }
        }

        Json::Value mRoot;
        bool mValid{false};
    };

}  // namespace qifeng::scm
