/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * json_utils.h - JSON工具类，提供结构体与Json::Value之间的双向转换
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_JSON_UTILS_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_JSON_UTILS_H

#include <ctime>
#include <iomanip>
#include <json/json.h>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "common/config_define.h"

/**
 * @brief JSON工具类，提供struct和Json::Value之间的快速转换方法
 */

// 前置声明
namespace common {
    namespace utils {
        class JsonUtils {
        public:
            /**
             * @brief 解析算术类型与Json::Value之间的转换
             * @tparam T 算术类型（排除bool、Json::Int64、Json::UInt64）
             * @param jsonValue Json::Value对象
             * @param memberName 成员名称
             * @param member 结构体成员引用
             * @param readJson 是否从Json读取到结构体（true: Json->结构体, false: 结构体->Json）
             */
            template <typename T>
            static typename std::enable_if<std::is_arithmetic<T>::value && !std::is_same<T, bool>::value &&
                                           !std::is_same<T, Json::Int64>::value &&
                                           !std::is_same<T, Json::UInt64>::value>::type
            ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson);

            /**
             * @brief 解析bool类型与Json::Value之间的转换
             * @tparam T bool类型
             * @param jsonValue Json::Value对象
             * @param memberName 成员名称
             * @param member 结构体成员引用
             * @param readJson 是否从Json读取到结构体（true: Json->结构体, false: 结构体->Json）
             */
            template <typename T>
            static typename std::enable_if<std::is_same<T, bool>::value>::type
            ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson);

            /**
             * @brief 解析std::string类型与Json::Value之间的转换
             * @tparam T std::string类型
             * @param jsonValue Json::Value对象
             * @param memberName 成员名称
             * @param member 结构体成员引用
             * @param readJson 是否从Json读取到结构体（true: Json->结构体, false: 结构体->Json）
             */
            template <typename T>
            static typename std::enable_if<std::is_same<T, std::string>::value>::type
            ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson);

            /**
             * @brief 解析Json::Int64类型与Json::Value之间的转换
             * @tparam T Json::Int64类型
             * @param jsonValue Json::Value对象
             * @param memberName 成员名称
             * @param member 结构体成员引用
             * @param readJson 是否从Json读取到结构体（true: Json->结构体, false: 结构体->Json）
             */
            template <typename T>
            static typename std::enable_if<std::is_same<T, Json::Int64>::value>::type
            ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson);

            /**
             * @brief 解析Json::UInt64类型与Json::Value之间的转换
             * @tparam T Json::UInt64类型
             * @param jsonValue Json::Value对象
             * @param memberName 成员名称
             * @param member 结构体成员引用
             * @param readJson 是否从Json读取到结构体（true: Json->结构体, false: 结构体->Json）
             */
            template <typename T>
            static typename std::enable_if<std::is_same<T, Json::UInt64>::value>::type
            ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson);

            /**
             * @brief 解析std::tm类型与Json::Value之间的转换
             * @tparam T std::tm类型
             * @param jsonValue Json::Value对象
             * @param memberName 成员名称
             * @param member 结构体成员引用
             * @param readJson 是否从Json读取到结构体（true: Json->结构体, false: 结构体->Json）
             */
            template <typename T>
            static typename std::enable_if<std::is_same<T, std::tm>::value>::type
            ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson);

            /**
             * @brief 解析枚举类型与Json::Value之间的转换
             * @tparam T 枚举类型
             * @param jsonValue Json::Value对象
             * @param memberName 成员名称
             * @param member 枚举成员引用
             * @param readJson 是否从Json读取到结构体（true: Json->结构体, false: 结构体->Json）
             */
            template <typename T>
            static typename std::enable_if<std::is_enum<T>::value>::type
            ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson);

            /**
             * @brief 解析Json::Value类型与Json::Value之间的转换
             * @param jsonValue Json::Value对象
             * @param memberName 成员名称
             * @param member Json::Value成员引用
             * @param readJson 是否从Json读取到结构体（true: Json->结构体, false: 结构体->Json）
             */
            template <typename T>
            static typename std::enable_if<std::is_same<T, Json::Value>::value>::type
            ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson);

            /**
             * @brief 解析std::optional类型与Json::Value之间的转换
             * @tparam T 可选类型的实际类型
             * @param jsonValue Json::Value对象
             * @param memberName 成员名称
             * @param member 可选类型成员引用
             * @param readJson 是否从Json读取到结构体（true: Json->结构体, false: 结构体->Json）
             */
            template <typename T>
            static void ParseMember(Json::Value& jsonValue, const std::string& memberName, std::optional<T>& member,
                                    bool readJson);

            /**
             * @brief 解析结构体类型与Json::Value之间的转换
             * @tparam T 结构体类型（具有parseJson方法的类型）
             * @param jsonValue Json::Value对象
             * @param memberName 成员名称
             * @param member 结构体成员引用
             * @param readJson 是否从Json读取到结构体（true: Json->结构体, false: 结构体->Json）
             */
            template <typename T>
            static
                typename std::enable_if<!std::is_enum<T>::value && !std::is_arithmetic<T>::value &&
                                        !std::is_same<T, std::string>::value && !std::is_same<T, std::tm>::value &&
                                        !std::is_same<T, Json::Int64>::value && !std::is_same<T, Json::UInt64>::value &&
                                        !std::is_same<T, bool>::value && !std::is_same<T, Json::Value>::value>::type
                ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson);

            /**
             * @brief 解析数组类型与Json::Value之间的转换
             * @tparam T 数组元素类型
             * @param parentJson Json::Value父对象
             * @param memberName 成员名称
             * @param member 数组引用
             * @param readJson 是否从Json读取到结构体（true: Json->结构体, false: 结构体->Json）
             */
            template <typename T>
            static void ParseMember(Json::Value& parentJson, const std::string& memberName, std::vector<T>& member,
                                    bool readJson);

            /**
             * @brief 解析集合类型与Json::Value之间的转换
             * @tparam T 集合元素类型
             * @param parentJson Json::Value父对象
             * @param memberName 成员名称
             * @param member 集合引用
             * @param readJson 是否从Json读取到结构体（true: Json->结构体, false: 结构体->Json）
             */
            template <typename T>
            static void ParseMember(Json::Value& parentJson, const std::string& memberName, std::set<T>& member,
                                    bool readJson);
        };

        /**
         * @brief 将 Json::Value 序列化为紧凑 JSON 字符串（无多余空白）。
         * @param[in] val json
         * @return 紧凑 JSON 字符串
         */
        std::string JsonToCompactString(const Json::Value& val);
    }  // namespace utils
}  // namespace common

// 辅助宏定义
#define BEGIN_JSON_PARSER                                            \
public:                                                              \
    void parseJson(Json::Value& jsonObject, bool readJson = false) { \
        parseJsonImpl(jsonObject, readJson);                         \
    }                                                                \
                                                                     \
    void parseJsonImpl(Json::Value& jsonObject, bool readJson) {
#define ADD_MEMBER(member) ::common::utils::JsonUtils::ParseMember(jsonObject, #member, this->member, readJson);
#define ADD_OPTIONAL_MEMBER(member) \
    ::common::utils::JsonUtils::ParseMember(jsonObject, #member, this->member, readJson);
#define END_JSON_PARSER                            \
    }                                              \
                                                   \
    Json::Value toJson() {                         \
        Json::Value jsonObject;                    \
        parseJsonImpl(jsonObject, false);          \
        return jsonObject;                         \
    }                                              \
                                                   \
    bool fromJson(const Json::Value& jsonObject) { \
        try {                                      \
            Json::Value mutableJson = jsonObject;  \
            parseJsonImpl(mutableJson, true);      \
            return true;                           \
        } catch (const std::exception&) {          \
            return false;                          \
        }                                          \
    }

// 模板特化实现
namespace common {
    namespace utils {

        template <typename T>
        inline void ReadArithmeticMember(Json::Value& jsonValue, const std::string& memberName, T& member) {
            if (!jsonValue.isMember(memberName)) {
                return;
            }

            const Json::Value& memberJson = jsonValue[memberName];
            if constexpr (std::is_integral<T>::value && std::is_signed<T>::value) {
                if (memberJson.isInt()) {
                    member = static_cast<T>(memberJson.asInt());
                    return;
                }
                if (memberJson.isInt64()) {
                    member = static_cast<T>(memberJson.asInt64());
                }
            } else if constexpr (std::is_integral<T>::value && std::is_unsigned<T>::value) {
                if (memberJson.isUInt()) {
                    member = static_cast<T>(memberJson.asUInt());
                    return;
                }
                if (memberJson.isUInt64()) {
                    member = static_cast<T>(memberJson.asUInt64());
                }
            } else if (memberJson.isDouble()) {
                member = static_cast<T>(memberJson.asDouble());
            }
        }

        template <typename T>
        inline void WriteArithmeticMember(Json::Value& jsonValue, const std::string& memberName, const T& member) {
            if constexpr (std::is_integral<T>::value && std::is_signed<T>::value) {
                jsonValue[memberName] = static_cast<Json::Int64>(member);
            } else if constexpr (std::is_integral<T>::value && std::is_unsigned<T>::value) {
                jsonValue[memberName] = static_cast<Json::UInt64>(member);
            } else {
                jsonValue[memberName] = static_cast<double>(member);
            }
        }

        // 算术类型SFINAE实现
        template <typename T>
        inline
            typename std::enable_if<std::is_arithmetic<T>::value && !std::is_same<T, bool>::value &&
                                    !std::is_same<T, Json::Int64>::value && !std::is_same<T, Json::UInt64>::value>::type
            JsonUtils::ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson) {
            if (readJson) {
                ReadArithmeticMember(jsonValue, memberName, member);
                return;
            }

            WriteArithmeticMember(jsonValue, memberName, member);
        }

        // bool类型SFINAE实现
        template <typename T>
        inline typename std::enable_if<std::is_same<T, bool>::value>::type
        JsonUtils::ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson) {
            if (readJson) {
                // 从Json读取到结构体
                if (jsonValue.isMember(memberName) && jsonValue[memberName].isBool()) {
                    member = jsonValue[memberName].asBool();
                }
            } else {
                // 从结构体写入到Json
                jsonValue[memberName] = member;
            }
        }

        // std::string类型SFINAE实现
        template <typename T>
        inline typename std::enable_if<std::is_same<T, std::string>::value>::type
        JsonUtils::ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson) {
            if (readJson) {
                // 从Json读取到结构体
                if (jsonValue.isMember(memberName) && jsonValue[memberName].isString()) {
                    member = jsonValue[memberName].asString();
                }
            } else {
                // 从结构体写入到Json
                jsonValue[memberName] = member;
            }
        }

        // Json::Int64类型SFINAE实现
        template <typename T>
        inline typename std::enable_if<std::is_same<T, Json::Int64>::value>::type
        JsonUtils::ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson) {
            if (readJson) {
                // 从Json读取到结构体
                if (jsonValue.isMember(memberName) && jsonValue[memberName].isInt64()) {
                    member = jsonValue[memberName].asInt64();
                }
            } else {
                // 从结构体写入到Json
                jsonValue[memberName] = member;
            }
        }

        // Json::UInt64类型SFINAE实现
        template <typename T>
        inline typename std::enable_if<std::is_same<T, Json::UInt64>::value>::type
        JsonUtils::ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson) {
            if (readJson) {
                // 从Json读取到结构体
                if (jsonValue.isMember(memberName) && jsonValue[memberName].isUInt64()) {
                    member = jsonValue[memberName].asUInt64();
                }
            } else {
                // 从结构体写入到Json
                jsonValue[memberName] = member;
            }
        }

        // std::tm类型SFINAE实现
        template <typename T>
        inline typename std::enable_if<std::is_same<T, std::tm>::value>::type
        JsonUtils::ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson) {
            if (readJson) {
                // 从Json读取到结构体 - 支持字符串格式的时间解析
                if (jsonValue.isMember(memberName) && jsonValue[memberName].isString()) {
                    std::string timeStr = jsonValue[memberName].asString();
                    std::istringstream ss(timeStr);
                    ss >> std::get_time(&member, TimerFormat.begin());
                }
            } else {
                // 从结构体写入到Json
                // 使用独立的时间字符串转换实现，不依赖于外部函数
                std::ostringstream oss;
                oss << std::put_time(&member, "%Y-%m-%d %H:%M:%S");
                jsonValue[memberName] = oss.str();
            }
        }

        // 数组类型特化
        template <typename T>
        inline void JsonUtils::ParseMember(Json::Value& parentJson, const std::string& memberName,
                                           std::vector<T>& member, bool readJson) {
            if (readJson) {
                // 从Json读取到结构体数组
                if (parentJson.isMember(memberName) && parentJson[memberName].isArray()) {
                    const Json::Value& arrayJson = parentJson[memberName];
                    member.clear();
                    for (Json::ArrayIndex i = 0; i < arrayJson.size(); ++i) {
                        T item;
                        Json::Value itemJson = arrayJson[i];
                        item.parseJson(itemJson, true);
                        member.push_back(item);
                    }
                }
            } else {
                // 从结构体数组写入到Json
                Json::Value arrayJson(Json::arrayValue);
                for (std::size_t i = 0; i < member.size(); ++i) {
                    Json::Value itemJson;
                    member[i].parseJson(itemJson, false);
                    arrayJson.append(itemJson);
                }
                parentJson[memberName] = arrayJson;
            }
        }

        // 集合类型特化
        template <typename T>
        inline void JsonUtils::ParseMember(Json::Value& parentJson, const std::string& memberName, std::set<T>& member,
                                           bool readJson) {
            if (readJson) {
                // 从Json读取到结构体集合
                if (parentJson.isMember(memberName) && parentJson[memberName].isArray()) {
                    const Json::Value& arrayJson = parentJson[memberName];
                    member.clear();
                    for (Json::ArrayIndex i = 0; i < arrayJson.size(); ++i) {
                        T item;
                        Json::Value itemJson = arrayJson[i];
                        item.parseJson(itemJson, true);
                        member.insert(item);
                    }
                }
            } else {
                // 从结构体集合写入到Json
                Json::Value arrayJson(Json::arrayValue);
                for (const auto& item : member) {
                    Json::Value itemJson;
                    T copyItem = item;
                    copyItem.parseJson(itemJson, false);
                    arrayJson.append(itemJson);
                }
                parentJson[memberName] = arrayJson;
            }
        }

        // 枚举类型SFINAE实现
        template <typename T>
        inline typename std::enable_if<std::is_enum<T>::value>::type
        JsonUtils::ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson) {
            if (readJson) {
                // 从Json读取到结构体
                if (jsonValue.isMember(memberName)) {
                    if (jsonValue[memberName].isUInt()) {
                        member = static_cast<T>(jsonValue[memberName].asUInt());
                    } else if (jsonValue[memberName].isInt()) {
                        member = static_cast<T>(jsonValue[memberName].asInt());
                    }
                }
            } else {
                // 从结构体写入到Json
                jsonValue[memberName] = static_cast<unsigned int>(member);
            }
        }

        // Json::Value类型的特殊处理
        template <typename T>
        inline typename std::enable_if<std::is_same<T, Json::Value>::value>::type
        JsonUtils::ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson) {
            if (readJson) {
                // 从Json读取到Json::Value
                if (jsonValue.isMember(memberName)) {
                    member = jsonValue[memberName];
                }
            } else {
                // 从Json::Value写入到Json
                jsonValue[memberName] = member;
            }
        }

        // std::optional类型特化
        template <typename T>
        inline void JsonUtils::ParseMember(Json::Value& jsonValue, const std::string& memberName,
                                           std::optional<T>& member, bool readJson) {
            if (readJson) {
                // 从Json读取到std::optional
                if (jsonValue.isMember(memberName)) {
                    T value = T();
                    JsonUtils::ParseMember(jsonValue, memberName, value, true);
                    member = value;
                }
            } else {
                // 从std::optional写入到Json
                if (member.has_value()) {
                    JsonUtils::ParseMember(jsonValue, memberName, *member, false);
                }
            }
        }

        // 结构体类型SFINAE实现
        template <typename T>
        inline typename std::enable_if<!std::is_enum<T>::value && !std::is_arithmetic<T>::value &&
                                       !std::is_same<T, std::string>::value && !std::is_same<T, std::tm>::value &&
                                       !std::is_same<T, Json::Int64>::value && !std::is_same<T, Json::UInt64>::value &&
                                       !std::is_same<T, bool>::value && !std::is_same<T, Json::Value>::value>::type
        JsonUtils::ParseMember(Json::Value& jsonValue, const std::string& memberName, T& member, bool readJson) {
            if (readJson) {
                // 从Json读取到结构体
                if (jsonValue.isMember(memberName)) {
                    member.parseJson(jsonValue[memberName], true);
                }
            } else {
                // 从结构体写入到Json
                Json::Value nestedJson;
                member.parseJson(nestedJson, false);
                jsonValue[memberName] = nestedJson;
            }
        }
    }  // namespace utils
}  // namespace common

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_JSON_UTILS_H