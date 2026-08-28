/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

/**
 * @file metting.h
 * @brief 会议纪要（Meeting Minutes）模块头文件
 * @details 定义会议信息的数据结构、会议纪要总结的请求/响应类型，
 *          以及会议纪要总结器的抽象接口。
 */

#ifndef QIFENG_FRAMEWORK_LMS_METTING_H
#define QIFENG_FRAMEWORK_LMS_METTING_H

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "lms/model.h"

namespace qifeng {

    namespace lms {

        // 前向声明

        struct BaseMettingInfo;
        struct BriefMettingInfo;
        struct NormalMettingInfo;
        struct DetailedMettingInfo;
        struct SummaryError;
        struct SummaryData;
        struct ProgressStat;

        class MettingSummarizer;

        // 正式声明

        /**
         * @brief 基础会议信息结构体
         * @details 包含会议的基本信息字段，是所有会议信息类型的基类。
         *          派生类（BriefMettingInfo、NormalMettingInfo、DetailedMettingInfo）
         *          通过重写 Validate() 实现不同粒度的校验逻辑。
         */
        struct BaseMettingInfo {
            std::string date;       ///< 会议时间，如 "2025-07-04 14:00"
            std::string location;   ///< 会议地点
            std::string host;       ///< 主持人
            std::string attendees;  ///< 参会人员
            std::string text;       ///< 会议原文（必填）

            /**
             * @brief 构造函数
             * @param dateInput   会议时间字符串
             * @param locationInput 会议地点
             * @param hostInput   主持人
             * @param attendeesInput 参会人员
             * @param textInput   会议原文
             * @param topicInput  会议议题（空则自动提取）
             * @param kindInput   会议类型（空则自动分类）
             * @param noteInput   用户笔记（空则无）
             */
            BaseMettingInfo(const std::string& dateInput, const std::string& locationInput,
                            const std::string& hostInput, const std::string& attendeesInput,
                            const std::string& textInput);

            /**
             * @brief 格式化基础会议信息
             * @return 格式化的基础信息字符串
             */
            std::string FormattedBaseInfo() const;

            /**
             * @brief 校验会议信息字段是否合法
             * @return 如果校验通过返回 std::nullopt，否则返回错误描述字符串
             */
            std::optional<SummaryError> Validate() const;
        };

        /**
         * @brief 简要会议信息结构体
         * @details 继承自 BaseMettingInfo，使用较宽松的校验规则，
         *          适用于只需要基本信息摘要的场景。
         */
        struct BriefMettingInfo : public BaseMettingInfo {
            /**
             * @brief 构造函数
             * @param dateInput   会议时间字符串
             * @param locationInput 会议地点
             * @param hostInput   主持人
             * @param attendeesInput 参会人员
             * @param textInput   会议原文
             * @param topicInput  会议议题（空则自动提取）
             * @param kindInput   会议类型（空则自动分类）
             */
            BriefMettingInfo(const std::string& dateInput, const std::string& locationInput,
                             const std::string& hostInput, const std::string& attendeesInput,
                             const std::string& textInput);

            /**
             * @brief 校验简要会议信息字段是否合法
             * @return 如果校验通过返回 std::nullopt，否则返回错误描述字符串
             */
            std::optional<SummaryError> Validate() const;
        };

        /**
         * @brief 标准会议信息结构体
         * @details 继承自 BaseMettingInfo，使用中等严格的校验规则，
         *          适用于常规会议纪要生成场景。
         */
        struct NormalMettingInfo : public BaseMettingInfo {
            /**
             * @brief 构造函数
             * @param dateInput   会议时间字符串
             * @param locationInput 会议地点
             * @param hostInput   主持人
             * @param attendeesInput 参会人员
             * @param textInput   会议原文
             * @param topicInput  会议议题（空则自动提取）
             * @param kindInput   会议类型（空则自动分类）
             */
            NormalMettingInfo(const std::string& dateInput, const std::string& locationInput,
                              const std::string& hostInput, const std::string& attendeesInput,
                              const std::string& textInput);

            /**
             * @brief 校验标准会议信息字段是否合法
             * @return 如果校验通过返回 std::nullopt，否则返回错误描述字符串
             */
            std::optional<SummaryError> Validate() const;
        };

        /**
         * @brief 详细会议信息结构体
         * @details 继承自 BaseMettingInfo，使用最严格的校验规则，
         *          要求所有字段均完整填写，适用于正式会议纪要场景。
         */
        struct DetailedMettingInfo : public BaseMettingInfo {
            /**
             * @brief 构造函数
             * @param dateInput   会议时间字符串
             * @param locationInput 会议地点
             * @param hostInput   主持人
             * @param attendeesInput 参会人员
             * @param textInput   会议原文
             * @param topicInput  会议议题（空则自动提取）
             * @param kindInput   会议类型（空则自动分类）
             */
            DetailedMettingInfo(const std::string& dateInput, const std::string& locationInput,
                                const std::string& hostInput, const std::string& attendeesInput,
                                const std::string& textInput);

            /**
             * @brief 校验详细会议信息字段是否合法
             * @return 如果校验通过返回 std::nullopt，否则返回错误描述字符串
             */
            std::optional<SummaryError> Validate() const;
        };

        /**
         * @brief 会议信息变体类型
         * @details 使用 std::variant 组合三种不同粒度的会议信息类型，
         *          可根据需要选择 Brief、Normal 或 Detailed 类型。
         */
        using MettingInfo = std::variant<BriefMettingInfo, NormalMettingInfo, DetailedMettingInfo>;

        /**
         * @brief 会议纪要总结错误信息结构体
         * @details 当会议纪要总结流程出现异常时，使用此结构体描述错误状态和原因。
         */
        struct SummaryError {
            /**
             * @brief 错误状态码枚举
             * @details 定义各类错误的数值编码，便于调用方根据错误码进行差异化处理。
             */
            enum struct StateCode : int32_t {
                INVALID_DATA = 905400,     ///< 输入文本校验出错
                SUMMARY_ERROR = 905401,    ///< 会议纪要总结流程内部错误
                INFERENCE_ERROR = 905402,  ///< 推理错误
                INTERRUPTED = 905403,      ///< 用户主动中断
                OUTPUT_TOO_LONG = 905404,  ///< 输出文本太长
                INPUT_TOO_SHORT = 905405,  ///< 输入文本太短
                INPUT_TOO_LONG = 905406,   ///< 输入文本太长
            };
            StateCode code;      ///< 错误状态码
            std::string reason;  ///< 错误原因描述
        };

        /**
         * @brief 会议纪要摘要数据结构体
         * @details 包含会议纪要的概要文本和关键词列表，是会议纪要总结成功的输出结果。
         */
        struct SummaryData {
            std::string overview;               ///< 概要文本
            std::vector<std::string> keywords;  ///< 关键词列表
        };

        /**
         * @brief 会议纪要总结结果变体类型
         * @details 使用 std::variant 组合成功（SummaryData）和失败（SummaryError）两种结果，
         *          调用方可通过 std::holds_alternative 或 std::visit 判断结果类型。
         */
        using SummaryResult = std::variant<SummaryError, SummaryData>;

        /**
         * @brief 会议纪要生成提示信息（用户可选传入）
         * @details 用于支持外部传入议题和自定义模板（纪要范文）：
         *          - topic 非空时，编排层直接使用该议题，跳过 LLM 自动提取；
         *            为空时由编排层从会议原文中自动提取。
         *          - exampleSummary 非空时，编排层会先调用 LLM 从范文中提取写作风格，
         *            再将风格特征注入纪要生成提示词，使输出遵循范文的写作风格。
         *            为空时不施加风格约束。
         */
        struct MettingHints {
            std::string topic;           ///< 会议议题（空则自动提取）
            std::string exampleSummary;  ///< 纪要范文文本（空则不学习风格）
        };

        /**
         * @brief 进度统计信息结构体
         * @details 用于向调用方报告会议纪要总结任务的实时进度，
         *          包含已耗时间和预估剩余时间。
         */
        struct ProgressStat {
            std::chrono::milliseconds spentTime;              ///< 已消耗的时间
            std::chrono::milliseconds estimateRemainingTime;  ///< 预估的剩余时长
        };

        /**
         * @brief 会议纪要总结器抽象基类
         * @details 定义会议纪要总结的统一接口，支持同步执行、进度回调和中止操作。
         *          具体的实现类应继承此类并实现所有纯虚函数。
         */
        class MettingSummarizer {
        public:
            /**
             * @brief 获取当前会议信息
             * @return 会议信息变体（MettingInfo）
             */
            virtual MettingInfo Info() = 0;

            /**
             * @brief 同步执行会议纪要总结（无进度回调）
             * @return 会议纪要总结结果（SummaryResult）
             * @note 此方法为便捷封装，默认调用带进度回调的重载版本并忽略进度。
             */
            SummaryResult SynchronouslyExecute();

            /**
             * @brief 同步执行会议纪要总结（带进度回调）
             * @param progressObserver 进度观察回调函数，接收 ProgressStat 参数，
             *                         会在总结过程中被周期性调用以报告进度
             * @return 会议纪要总结结果（SummaryResult）
             */
            virtual SummaryResult SynchronouslyExecute(std::function<void(ProgressStat)> progressObserver) = 0;

            /**
             * @brief 检查总结任务是否正在运行
             * @return true 表示正在执行中，false 表示空闲
             */
            virtual bool Running() = 0;

            /**
             * @brief 手动中止正在执行的总结任务
             * @details 调用后 Running() 应返回 false，SynchronouslyExecute 应尽快返回 INTERRUPTED 错误
             */
            virtual void Stop() = 0;

            /**
             * @brief 终止当前正在执行的总结任务并重新开始
             * @param progressObserver 进度观察回调函数，接收 ProgressStat 参数
             * @return 重新执行后的会议纪要总结结果（SummaryResult）
             * @details 当 SynchronouslyExecute 因模型幻觉等原因卡住时，
             *          可从另一线程调用此方法中止当前任务并重新开始总结。
             *          此方法会阻塞直到新的总结任务完成。
             */
            virtual SummaryResult Restart(std::function<void(ProgressStat)> progressObserver) = 0;

        protected:
            /**
             * @brief 默认构造函数（受保护，禁止直接实例化）
             */
            MettingSummarizer() = default;
        };

        /**
         * @brief 创建会议纪要总结器的工厂函数
         * @param model 语言模型指针，用于执行文本生成推理
         * @param mettingInfo 会议信息，指定总结的会议内容和粒度（Brief/Normal/Detailed）
         * @return 会议纪要总结器的共享指针
         */
        std::shared_ptr<MettingSummarizer> CreateMettingSummarizer(std::shared_ptr<Model> model,
                                                                   const MettingInfo& mettingInfo);

        /**
         * @brief 创建会议纪要总结器的工厂函数（带议题和自定义模板）
         * @param model 语言模型指针，用于执行文本生成推理
         * @param mettingInfo 会议信息，指定总结的会议内容和粒度（Brief/Normal/Detailed）
         * @param hints 纪要生成提示信息（议题 + 纪要范文）
         * @return 会议纪要总结器的共享指针
         */
        std::shared_ptr<MettingSummarizer> CreateMettingSummarizer(std::shared_ptr<Model> model,
                                                                   const MettingInfo& mettingInfo,
                                                                   const MettingHints& hints);

    }  // namespace lms
}  // namespace qifeng

#endif