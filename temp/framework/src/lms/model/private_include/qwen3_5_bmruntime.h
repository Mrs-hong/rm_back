#ifndef QIFENG_FRAMEWORK_LMS_MODEL_PRIVATE_INCLUDE_QWEN3_5_BMRUNTIME_H
#define QIFENG_FRAMEWORK_LMS_MODEL_PRIVATE_INCLUDE_QWEN3_5_BMRUNTIME_H

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

        //读取模型配置文件中的参数
        using ConfigHandler = std::function<void(const Json::Value&)>;

        class BMRuntimeQwen35ModelContext;
        class BMRuntimeQwen35Model;

        class BMRuntimeQwen35ModelContext : public ModelContext {
        public:
            BMRuntimeQwen35ModelContext(std::shared_ptr<BMRuntimeQwen35Model> model);
            ~BMRuntimeQwen35ModelContext();

            std::string Generate(const ModelRequest& request) override;
            void StreamGenerate(const ModelRequest& request, StreamGenerateCallback callback) override;
            void CancelGeneration() override;
            void Reset() override;

        private:
            struct ToolCall {
                std::string name;
                Json::Value arguments;
            };

            struct ChatMessage {
                std::string role;
                std::string content;
                std::optional<std::string> reasoningContent;
                std::vector<ToolCall> toolCalls;
            };

            struct ChatTemplateInput {
                std::vector<ChatMessage> messages;
                std::vector<Json::Value> tools;
                bool addGenerationPrompt;
                bool enableThinking;

                ChatTemplateInput();
            };

            friend std::string renderChatTemplate(const ChatTemplateInput& in);

            ChatTemplateInput mTemplateInput;
            std::atomic<bool> mGenerationCanceled;

            bool DoStreamGenerate(const ModelRequest& request, StreamGenerateCallback callback);
        };

        class BMRuntimeQwen35Model : public Model {
        public:
            struct SampleParam {
                float repetitionPenalty;
                float temperature;
                float topP;
                int32_t topK;
            };

            enum struct BlockType {
                STANDARD_ATTENTION,  // 标准 attention: position_ids + attention_mask + history_k + history_v
                LINEAR_ATTENTION,    // 线性 attention:  conv_state + recurrent_state
            };

            struct BlockNetwork {
                const bm_net_info_t* block;
                const bm_net_info_t* blockCache;
                BlockType type;
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

            BMRuntimeQwen35Model(const std::string& tokenizerPath, const std::string& configPath,
                                 const std::string& modelPath);
            ~BMRuntimeQwen35Model();
            std::shared_ptr<ModelContext> CreateContext() override;
            std::string ModelName() const override;
            std::string EngineName() const override;

            bool NonParallelizableStreamGenerate(const std::string& input, std::optional<SampleParam> param,
                                                 StreamGenerateCallback callback);

        private:
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

            // 第一个标准 attention 层的索引（用于共享 position_ids / attention_mask）
            uint16_t mFirstStandardLayerIndex;

            // 模型参数
            uint32_t mHiddenBytes;
            uint32_t mKVBytes;    // 标准 attention 单层 KV cache 字节数（单 token）
            uint32_t mConvBytes;  // 线性 attention conv_state 字节数
            int32_t mMaxInputTokenLength;
            int32_t mMaxTokenLength;
            uint16_t mMaskValue;

            // 模型运行时状态
            std::mutex mMutexNetwork;
            std::unique_ptr<NetworkInput> mNetworkInput;

            // 配置参数处理
            std::unordered_map<std::string, ConfigHandler> mConfigHandlers;
            void InitConfigHandlers();
            void Finalize();
            void CleanupAllNetworks();
            void CleanupNetwork(const bm_net_info_t* network);

            // 模型运行时相关函数
            void Prefill(std::optional<SampleParam> param);
            void Decode(std::optional<SampleParam> param);
            void ForwardNetwork(const bm_net_info_t* network);
            void ForwardBlockNetworkWithDynamicLength(uint16_t layerIndex, int32_t actualLength);
            void ForwardBlockCacheNetworkForDecode(uint16_t layerIndex, bm_device_mem_t inputMem,
                                                   const int32_t* positionIds3D, int32_t kvOffset);
            int32_t Sample(std::optional<SampleParam> param);
        };

    }  // namespace lms

}  // namespace qifeng

#endif
