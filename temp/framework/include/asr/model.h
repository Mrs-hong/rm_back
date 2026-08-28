/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_ASR_MODEL_H
#define QIFENG_FRAMEWORK_ASR_MODEL_H

#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <variant>

#include "aas/aas_callback.h"
#include "aas/audio_load.h"
#include "bmruntime_interface.h"
namespace qifeng {

    namespace asr {
        // 前向声明
        struct AudioFile;
        struct PcmData;
        struct StreamContent;
        struct StreamEnd;
        struct StreamError;

        class ModelContext;
        class Model;

        /**
         * @brief 语音转写大模型流式生成单次返回内容
         */
        using StreamData = std::variant<StreamContent, qifeng::aas::StreamInferContent, StreamEnd, StreamError>;

        /**
         * @brief 语音转写大模型流式生成回调函数
         * @param[in]  StreamData 流式生成单次返回内容
         * @return 是否继续处理，如果要继续处理，返回true，否则返回false
         */
        using StreamGenerateCallback = std::function<bool(StreamData)>;

        // 正式声明

        /**
         * @brief 语音转写大模型流式生成单次返回内容（正常生成标识）
         */
        struct StreamContent {
            std::string sequence;
        };

        /**
         * @brief 语音转写大模型流式生成单次返回内容（生成结束标识）
         */
        struct StreamEnd {
            enum struct Reason {
                STOPPED,
                EXCEED_TOKEN_LIMIT,
            };
            Reason reason;
        };

        /**
         * @brief 语音转写大模型流式生成单次返回内容（错误标识）
         */
        struct StreamError {
            std::string what;
        };
        /**
         * @brief 流式 ASR 推理配置参数
         */
        struct StreamingConfig {
            float chunkSizeSec = 2.0f;  // 每个 chunk 的时长（秒）
            int unfixedChunkNum = 2;    // 前 N 个 chunk 不使用前缀文本
            int unfixedTokenNum = 5;    // 每次 rollback 截去的 token 数
            int stepMs = 1000;          // 模拟实时推送步长（毫秒）
            int maxNewTokens = 50;      // 每次推理最多生成的 token 数
            std::string language;       // 强制语言（空字符串表示自动检测）
            std::string context;
            // std::string language = "language zh";
            // std::string context
            // ="<|im_start|>system\n<|im_end|>\n<|im_start|>user\n<|audio_start|><|audio_end|><|im_end|>\n<|im_start|>assistant\nlanguage
            // zh<asr_text>"; // 系统 prompt / 上下文提示
        };

        /**
         * @brief 语音转写大模型对话上下文
         */
        class ModelContext {
        public:
            /**
             * @brief 语音转写大模型对话被取消的异常
             */
            class CanceledException : public std::runtime_error {
            public:
                explicit CanceledException(const char* msg);
                explicit CanceledException(const std::string& msg);
            };

            /**
             * @brief 语音转写大模型对话上下文构造函数
             * @param[in]  model 语音转写大模型实例
             */
            ModelContext(std::shared_ptr<Model> model);

            /**
             * @brief 语音转写大模型对话上下文析构函数
             */
            virtual ~ModelContext() = default;

            /**
             * @brief 语音转写大模型生成接口（非流式）
             * @param[in]  request 语音转写大模型请求
             * @return 输出内容
             * @exception CanceledException
             */
            // virtual std::string Generate(const ModelRequest& request, bool streamingInference = false) = 0;
            virtual qifeng::aas::StreamInferContent Generate(const qifeng::aas::Request& request,
                                                             bool streamingInference = false) = 0;

            /**
             * @brief 语音转写大模型生成接口（流式）
             * @param[in]  request 语音转写大模型请求
             * @param[in]  callback 流式生成回调函数
             * @return 输出内容
             * @exception CanceledException
             */
            virtual void StreamGenerate(const qifeng::aas::Request& request, StreamGenerateCallback callback) = 0;

            /**
             * @brief 语音转写大模型终止生成接口。如果此时正在执行语音转写大模型生成过程，则会抛出 CanceledException
             * @return
             */
            virtual void CancelGeneration() = 0;

            /**
             * @brief 语音转写大模型重置对话上下文接口
             * @return
             */
            virtual void Reset() = 0;

        protected:
            std::shared_ptr<Model> mModel;
        };

        /**
         * @brief 语音转写大模型实例
         */
        class Model : public std::enable_shared_from_this<Model> {
        public:
            /**
             * @brief 语音转写大模型对话上下文构造函数
             */
            Model() = default;

            /**
             * @brief 语音转写大模型对话上下文析构函数
             */
            virtual ~Model() = default;

            /**
             * @brief 创建语音转写大模型对话上下文实例接口
             * @return 语音转写大模型对话上下文实例
             */
            virtual std::shared_ptr<ModelContext> CreateContext() = 0;

            /**
             * @brief 获取语音转写大模型模型名称接口
             * @return 语音转写大模型模型名称
             */
            virtual std::string ModelName() const = 0;

            /**
             * @brief 获取语音转写大模型推理引擎名称接口
             * @return 语音转写大模型推理引擎名称
             */
            virtual std::string EngineName() const = 0;
        };
        /**
         * @brief 创建 Qwen3ASR BMRuntime 语音转写大模型实例
         * @param[in]  bmRuntime     BMRuntime 实例
         * @param[in]  tokenizerPath tokenizer 配置文件路径
         * @param[in]  configPath    模型配置文件路径
         * @param[in]  modelPath     模型文件路径
         * @return 语音转写大模型实例
         */
        std::shared_ptr<Model> CreateQwen3ASRBmruntimeModel(bm_handle_t handle, void* bmrt,
                                                            const std::string& tokenizerPath,
                                                            const std::string& configPath,
                                                            const std::string& modelPath);
    }  // namespace asr

}  // namespace qifeng

#endif