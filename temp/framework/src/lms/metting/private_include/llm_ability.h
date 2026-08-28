/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef LMS_LLM_ABILITY_H
#define LMS_LLM_ABILITY_H

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "spdlog/fmt/bundled/args.h"
#include "spdlog/fmt/bundled/core.h"
#include "spdlog/fmt/bundled/format.h"

#include "lms/metting/private_include/prompt_data.h"
#include "lms/model.h"
namespace qifeng {

    namespace lms {

        /**
         * @brief 会议类型枚举
         */
        enum class MeetingType {
            Invalid,   ///< 无效会议
            Morning,   ///< 晨会
            Decision,  ///< 决策会议
            Cpc,       ///< 党政会议
            General,   ///< 普通会议
        };

        /**
         * @brief 模型调用估算结果
         *
         * 包含预估的调用耗时和输出大小，用于编排层决策与调度。
         */
        struct EstimateResult {
            std::chrono::milliseconds duration;  ///< 预估调用耗时（毫秒）
            std::size_t outputSize;              ///< 预估输出大小（字符数）
        };

        /**
         * @brief 总结模式枚举（对应 Python InfoMode）
         */
        enum class SummarizeMode {
            Basic,     ///< 标准 Map-Reduce / 一次性总结
            Detailed,  ///< Refine 增量式总结
            Simple,    ///< Map-Only 概要框架
        };

        /**
         * @brief 估算会议纪要总结的总耗时
         * @param textLength  会议原文总长度（字符数）
         * @param mode        总结模式
         * @param chunkSize   分块大小（字符数）
         * @param docCount    分块数量
         * @return 预估耗时（毫秒）
         *
         * @note 基于 Python _estimate_time 实现，使用实测校准的 GEN_SPEED / INPUT_SPEED 常量。
         *       信息密度随文本长度递减：density = max(0.15, 0.6 / (1 + textLength / 15000))
         *       每次 LLM 调用 = OVERHEAD + min(input, MAX_INPUT) / INPUT_SPEED + output / GEN_SPEED
         */
        std::chrono::milliseconds EstimateSummarizeTime(std::size_t textLength, SummarizeMode mode,
                                                        std::size_t chunkSize, std::size_t docCount);

        /**
         * @brief 单次 LLM 调用耗时估算
         * @param inputChars  输入字符数
         * @param outputChars 预估输出字符数
         * @return 预估耗时（毫秒）
         */
        std::chrono::milliseconds EstimateCallCost(std::size_t inputChars, std::size_t outputChars);

        /**
         * @brief 信息密度因子（文本越长，每块提取信息占比越低）
         * @param textLength 文本总长度
         * @return 密度因子 (0.15 ~ 0.6)
         */
        double EstimateDensity(std::size_t textLength);

        /**
         * @brief 纪要生成提示信息（议题 + 写作风格）
         * @details 将用户传入的议题和从纪要案例中提取的写作风格打包传递，
         *          避免能力层函数参数超过4个。两个字段均可为空：
         *          - topic 为空时由编排层自动提取
         *          - exampleStyle 为空时不施加风格约束
         */
        struct SummaryHints {
            std::string topic;         ///< 会议议题（空则由编排层自动提取）
            std::string exampleStyle;  ///< 从纪要案例提取的写作风格（空则不约束）
        };

        /**
         * @brief 判断会议类型
         * @param ctx 模型上下文
         * @param content 会议片段内容
         * @return 会议类型枚举值
         */
        MeetingType ClassifyMeeting(std::shared_ptr<ModelContext> ctx, std::string_view content);

        /**
         * @brief 从会议内容中提取议题
         * @param ctx 模型上下文
         * @param content 会议片段内容
         * @return 提取到的议题文本
         */
        std::string ExtractTopic(std::shared_ptr<ModelContext> ctx, std::string_view content);

        /**
         * @brief 从纪要范文中提取写作风格特征
         * @param ctx 模型上下文
         * @param content 纪要范文文本
         * @return 风格特征描述文本（Markdown 格式，含6个维度）
         */
        std::string ExtractStyle(std::shared_ptr<ModelContext> ctx, std::string_view content);

        /**
         * @brief 提取关键词
         * @param ctx 模型上下文
         * @param content 会议片段内容
         * @return 解析后的关键词列表
         */
        std::vector<std::string> ExtractKeywords(std::shared_ptr<ModelContext> ctx, std::string_view content);

        /**
         * @brief 信息提取（独立于会议类型的片段级信息提取）
         * @param ctx 模型上下文
         * @param content 会议片段内容
         * @return 提取到的结构化信息文本
         */
        std::string ExtractInformation(std::shared_ptr<ModelContext> ctx, std::string_view content);

        // ---- Map 阶段：按会议类型的片段级信息提取 ----

        /**
         * @brief 片段级信息提取（Map 阶段）
         * @param ctx 模型上下文
         * @param content 片段内容
         * @param type 会议类型
         * @return 该片段的结构化摘要信息
         */
        std::string Map(std::shared_ptr<ModelContext> ctx, std::string_view content, std::string_view topic);

        /**
         * @brief 全局整合（通用输出格式，需要议题和风格做占位符替换）
         * @param ctx 模型上下文
         * @param content 合并后的多片段内容
         * @param hints 纪要生成提示信息（议题 + 写作风格，用于提示词占位符替换）
         * @return 整合后的全局摘要文本
         */
        std::string OverviewGeneral(std::shared_ptr<ModelContext> ctx, std::string_view content,
                                    const SummaryHints& hints);

        // ---- 一次性总结 ----

        /**
         * @brief 极简模式一次性总结
         * @param ctx 模型上下文
         * @param content 用户输入内容
         * @return 简洁的总结文本
         *
         * @note 内部固定不启用思维链，适用于对响应速度要求较高的场景。
         */
        std::string OnceSimpleSummary(std::shared_ptr<ModelContext> ctx, std::string_view content);

        /**
         * @brief 完整模式一次性总结（通用输出格式）
         *
         * 能力层内部自动完成 OnceInvoke 模板的占位符替换，
         * 编排层无需关心提示词细节。
         *
         * @param ctx 模型上下文
         * @param content 用户输入内容
         * @param hints 纪要生成提示信息（议题 + 写作风格，用于提示词占位符替换）
         * @return 完整模式下的通用总结文本
         *
         * @note 内部固定启用思维链（Chain-of-Thought），以获得更高质量的总结。
         */
        std::string OnceSummaryGeneral(std::shared_ptr<ModelContext> ctx, std::string_view content,
                                       const SummaryHints& hints);

        // ---- 增量式总结 ----

        /**
         * @brief 增量式总结首段处理
         *
         * 对会议内容的第一个片段进行总结，作为后续增量更新的基础。
         *
         * @param ctx 模型上下文
         * @param content 首段会议内容
         * @return 首段摘要文本
         */
        std::string RefineFirst(std::shared_ptr<ModelContext> ctx, std::string_view content);

        /**
         * @brief 增量式总结中间段处理
         *
         * 基于前一段的摘要，对当前片段进行增量更新，生成新的摘要。
         *
         * @param ctx 模型上下文
         * @param content 当前片段内容
         * @param previousSummary 前一段的摘要文本
         * @return 更新后的摘要文本
         */
        std::string RefineMiddle(std::shared_ptr<ModelContext> ctx, std::string_view content,
                                 const std::string& previousSummary);

        /**
         * @brief 增量式总结末段处理
         *
         * 基于前一段的摘要，对最后一个片段进行增量更新，生成最终摘要。
         *
         * @param ctx 模型上下文
         * @param content 末段会议内容
         * @param previousSummary 前一段的摘要文本
         * @return 最终摘要文本
         */
        std::string RefineLast(std::shared_ptr<ModelContext> ctx, std::string_view content,
                               const std::string& previousSummary);

        // 替换模板中的 {key} 占位符
        inline std::string ReplacePlaceholders(std::string_view tmpl,
                                               const std::map<std::string_view, std::string_view>& params) {
            fmt::dynamic_format_arg_store<fmt::format_context> store {};
            for (const auto& [key, value] : params) {
                store.push_back(fmt::arg(key.data(), value));
            }

            return fmt::vformat(tmpl, store);
        }

    }  // namespace lms

}  // namespace qifeng

#endif  // LMS_LLM_ABILITY_H
