/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_LMS_MODEL_H
#define QIFENG_FRAMEWORK_LMS_MODEL_H

#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "bmruntime_interface.h"
#include "common/logger.h"
namespace qifeng {

    namespace lms {
        // 前向声明

        struct ModelRequest;
        struct StreamContent;
        struct StreamEnd;
        struct StreamError;

        class ModelContext;
        class Model;

        /**
         * @brief 语言大模型流式生成单次返回内容
         */
        using StreamData = std::variant<StreamContent, StreamEnd, StreamError>;

        /**
         * @brief 语言大模型流式生成回调函数
         * @param[in]  StreamData 流式生成单次返回内容
         * @return 是否继续处理，如果要继续处理，返回true，否则返回false
         */
        using StreamGenerateCallback = std::function<bool(StreamData)>;

        // 正式声明

        /**
         * @brief 语言大模型请求
         */
        struct ModelRequest {
            struct Tool {
                std::string name;           // 工具名称
                std::string argumentsJson;  // 工具参数描述
            };
            std::string systemPrompt;               // 系统提示词
            std::string userPrompt;                 // 用户提示词
            bool enableThinking;                    // 是否启用think
            std::optional<std::string> configJson;  // 其他配置（可选）
            std::vector<Tool> tools;                // 可用的工具
        };

        /**
         * @brief 语言大模型流式生成单次返回内容（正常生成标识）
         */
        struct StreamContent {
            std::string_view sequence;
        };

        /**
         * @brief 语言大模型流式生成单次返回内容（生成结束标识）
         */
        struct StreamEnd {
            enum struct Reason {
                STOPPED,
                EXCEED_TOKEN_LIMIT,
            };
            Reason reason;
        };

        /**
         * @brief 语言大模型流式生成单次返回内容（错误标识）
         */
        struct StreamError {
            std::string what;
        };
        struct TpuMemUsage {
            uint64_t mTotal;
            uint64_t mUsed;
            uint64_t mFree;
        };
        /**
         * @brief 语言大模型对话上下文(线程不安全)
         */
        class ModelContext {
        public:
            /**
             * @brief 语言大模型对话被取消的异常
             */
            class CanceledException : public std::runtime_error {
            public:
                explicit CanceledException(const char* msg);
                explicit CanceledException(const std::string& msg);
            };

            /**
             * @brief 语言大模型对话上下文构造函数
             * @param[in]  model 语言大模型实例
             */
            ModelContext(std::shared_ptr<Model> model);

            /**
             * @brief 语言大模型对话上下文析构函数
             */
            virtual ~ModelContext() = default;

            /**
             * @brief 语言大模型生成接口（非流式）
             * @param[in]  request 语言大模型请求
             * @return 输出内容
             * @exception CanceledException
             */
            virtual std::string Generate(const ModelRequest& request) = 0;

            /**
             * @brief 语言大模型生成接口（流式）
             * @param[in]  request 语言大模型请求
             * @param[in]  callback 流式生成回调函数
             * @return 输出内容
             * @exception CanceledException
             */
            virtual void StreamGenerate(const ModelRequest& request, StreamGenerateCallback callback) = 0;

            /**
             * @brief 语言大模型终止生成接口。如果此时正在执行语言大模型生成过程，则会抛出 CanceledException
             * @return
             */
            virtual void CancelGeneration() = 0;

            /**
             * @brief 语言大模型重置对话上下文接口
             * @return
             */
            virtual void Reset() = 0;

        protected:
            std::shared_ptr<Model> mModel;
        };

        /**
         * @brief 语言大模型实例
         */
        class Model : public std::enable_shared_from_this<Model> {
        public:
            /**
             * @brief 语音转写大模型对话上下文构造函数
             */
            Model() {
                if (!InitBMRuntime()) {
                    throw std::runtime_error {"Model InitBMRuntime failed."};
                }
            }

            /**
             * @brief 语音转写大模型对话上下文析构函数
             */
            virtual ~Model() {
                // 释放 BMRuntime 资源
                if (mpBmrt) {
                    bmrt_destroy(mpBmrt);
                    mpBmrt = nullptr;
                }
                if (mHandle) {
                    bm_dev_free(mHandle);
                    mHandle = nullptr;
                }
            }

            /**
             * @brief 创建语言大模型对话上下文实例接口
             * @return 语言大模型对话上下文实例
             */
            virtual std::shared_ptr<ModelContext> CreateContext() = 0;

            /**
             * @brief 获取语言大模型模型名称接口
             * @return 语言大模型模型名称
             */
            virtual std::string ModelName() const = 0;

            /**
             * @brief 获取语言大模型推理引擎名称接口
             * @return 语言大模型推理引擎名称
             */
            virtual std::string EngineName() const = 0;

            bool InitBMRuntime(int deviceId = 0) {
                bm_status_t ret = bm_dev_request(&mHandle, deviceId);
                if (ret != BM_SUCCESS) {
                    SLOG_ERROR << "bm_dev_request failed with ret: " << ret;
                    return false;
                }
                mpBmrt = bmrt_create(mHandle);
                if (!mpBmrt) {
                    SLOG_ERROR << "bmrt_create failed with handle:";
                    return false;
                }
                return true;
            }

            TpuMemUsage GetTpuUsage() noexcept {
                TpuMemUsage usage = {0};
                try {
                    unsigned int n = 0;
                    int r = bm_get_gmem_total_heap_num(mHandle, &n);
                    if (r != 0) {
                        return TpuMemUsage {0};
                    }
                    for (unsigned i = 0; i < n; ++i) {
                        bm_heap_stat_byte_t s {};
                        r = bm_get_gmem_heap_stat_byte_by_id(mHandle, &s, i);
                        if (r != 0) {
                            return TpuMemUsage {0};
                        }
                        usage.mTotal += s.mem_total;
                        usage.mUsed += s.mem_used;
                        usage.mFree += s.mem_avail;
                    }
                } catch (...) {
                    return TpuMemUsage {0};
                }
                return usage;
            }

        protected:
            bm_handle_t mHandle = nullptr;
            void* mpBmrt = nullptr;
        };

        /**
         * @brief 创建 Qwen3 BMRuntime 语言大模型实例
         * @param[in]  bmRuntime     BMRuntime 实例
         * @param[in]  tokenizerPath tokenizer 配置文件路径
         * @param[in]  configPath    模型配置文件路径
         * @param[in]  modelPath     模型文件路径
         * @return 语言大模型实例
         */
        std::shared_ptr<Model> CreateQwen3BmruntimeModel(const std::string& tokenizerPath,
                                                         const std::string& configPath, const std::string& modelPath);

        /**
         * @brief 创建 Qwen3.5 BMRuntime 语言大模型实例 (支持混合 attention: 标准 attention + 线性 attention)
         * @param[in]  bmRuntime     BMRuntime 实例
         * @param[in]  tokenizerPath tokenizer 配置文件路径
         * @param[in]  configPath    模型配置文件路径
         * @param[in]  modelPath     模型文件路径
         * @return 语言大模型实例
         */
        std::shared_ptr<Model> CreateQwen35BmruntimeModel(const std::string& tokenizerPath,
                                                          const std::string& configPath, const std::string& modelPath);
    }  // namespace lms

}  // namespace qifeng

#endif