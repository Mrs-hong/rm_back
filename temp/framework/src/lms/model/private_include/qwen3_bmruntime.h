#ifndef QIFENG_FRAMEWORK_LMS_MODEL_PRIVATE_INCLUDE_QWEN3_BMRUNTIME_H
#define QIFENG_FRAMEWORK_LMS_MODEL_PRIVATE_INCLUDE_QWEN3_BMRUNTIME_H

#include <atomic>
#include <mutex>
#include <optional>
#include <random>
#include <unordered_set>

#include "bmruntime_interface.h"
#include "json/json.h"
#include "tokenizers_c.h"

#include "lms/model.h"

namespace qifeng {

    namespace lms {

        class BMRuntimeQwen3ModelContext;
        class BMRuntimeQwen3Model;

        class BMRuntimeQwen3ModelContext : public ModelContext {
        public:
            BMRuntimeQwen3ModelContext(std::shared_ptr<BMRuntimeQwen3Model> model);
            ~BMRuntimeQwen3ModelContext();

            std::string Generate(const ModelRequest& request) override;
            void StreamGenerate(const ModelRequest& request, StreamGenerateCallback callback) override;
            void CancelGeneration() override;
            void Reset() override;

        private:
            // ========================================================================
            // 数据结构 — 对应 Jinja 模板中的所有参数
            // ========================================================================

            /**
             * ToolCall — assistant 消息中的工具调用
             *
             * 对应模板中 message.tool_calls 的元素。
             * 模板逻辑:
             *   {%- if tool_call.function %}
             *       {%- set tool_call = tool_call.function %}   <-- 有 function 包装时解包
             *   {%- endif %}
             *   然后使用 tool_call.name 和 tool_call.arguments
             *
             * 构造时已做好解包，所以这里只存平铺后的 name 和 arguments。
             * arguments 用 Json::Value 保存，既可能是 JSON 字符串
             * （如 "{\"city\":\"Beijing\"}"），也可能是 JSON 对象。
             */
            struct ToolCall {
                std::string name;
                Json::Value arguments;  // isString() → 直接输出; 否则 jsonToString()
            };

            /**
             * ChatMessage — 一条对话消息
             *
             * 对应模板中 messages 列表的元素。
             * 字段按 OpenAI Chat Completions API 惯例命名。
             */
            struct ChatMessage {
                std::string role;                             // "system" | "user" | "assistant" | "tool"
                std::string content;                          // 消息正文 (UTF-8)
                std::optional<std::string> reasoningContent;  // assistant 专用: 思考链
                std::vector<ToolCall> toolCalls;              // assistant 专用: 工具调用列表
            };

            /**
             * ChatTemplateInput — 模板渲染的全部输入
             *
             * 对应 Jinja 模板的全局变量:
             *   messages, tools, add_generation_prompt, enable_thinking
             */
            struct ChatTemplateInput {
                std::vector<ChatMessage> messages;
                std::vector<Json::Value> tools;  // 每个 tool 的定义 (JSON 对象)
                bool addGenerationPrompt;
                bool enableThinking;

                ChatTemplateInput();
            };

            friend std::string renderChatTemplate(const ChatTemplateInput& in);

            ChatTemplateInput mTemplateInput;
            std::atomic<bool> mGenerationCanceled;

            bool DoStreamGenerate(const ModelRequest& request, StreamGenerateCallback callback);
        };

        class BMRuntimeQwen3Model : public Model {
        public:
            struct SampleParam {
                float repetitionPenalty;
                float temperature;
                float topP;
                int32_t topK;
            };

            BMRuntimeQwen3Model(const std::string& tokenizerPath, const std::string& configPath,
                                const std::string& modelPath);
            ~BMRuntimeQwen3Model();
            std::shared_ptr<ModelContext> CreateContext() override;
            std::string ModelName() const override;
            std::string EngineName() const override;

            bool NonParallelizableStreamGenerate(const std::string& input, std::optional<SampleParam> param,
                                                 StreamGenerateCallback callback);

        private:
            struct BlockNetwork {
                const bm_net_info_t* block;
                const bm_net_info_t* blockCache;
            };
            struct NetworkInput {
                int32_t tokenLength;
                std::unique_ptr<int32_t[]> tokenBuffer;
                std::unique_ptr<int32_t[]> positionIdBuffer;
                std::unique_ptr<uint16_t[]> attentionMaskBuffer;
            };
            enum struct SampleStrategy {
                GREEDY,
                SAMPLE,
            };

            // 配置与共享不可变状态
            TokenizerHandle mTokenizer;
            std::unordered_set<int32_t> mEosTokens;
            SampleStrategy mSampleStrategy;
            SampleParam mDefaultSampleParam;
            std::mt19937 mGenerator;

            // 模型网络
            bm_net_info_t* mEmbed;
            bm_net_info_t* mEmbedCache;
            bm_net_info_t* mLmHead;
            bm_net_info_t* mGreedyHead;
            bm_net_info_t* mSampleHead;
            uint16_t mBlockerLayerSize;
            std::unique_ptr<BlockNetwork[]> mLayers;

            // 模型参数
            uint32_t mHiddenBytes;
            uint32_t mKVBytes;
            int32_t mMaxInputTokenLength;
            int32_t mMaxTokenLength;
            uint16_t mMaskValue;

            // 模型运行时状态
            std::mutex mMutexNetwork;  // 模型的batch只有1，不支持并发
            std::unique_ptr<NetworkInput> mNetworkInput;

            void Finalize();
            void CleanupAllNetworks();
            void CleanupNetwork(const bm_net_info_t* network);

            // 模型运行时相关函数
            void Prefill(std::optional<SampleParam> param);
            void Decode(std::optional<SampleParam> param);
            void ForwardNetwork(const bm_net_info_t* network);
            void ForwardBlockNetworkWithDynamicLength(uint16_t layerIndex, int32_t actualLength);
            void ForwardBlockCacheNetworkForDecode(uint16_t layerIndex, bm_device_mem_t inputMem);
            int32_t Sample(std::optional<SampleParam> param);
        };

    }  // namespace lms

}  // namespace qifeng

#endif
