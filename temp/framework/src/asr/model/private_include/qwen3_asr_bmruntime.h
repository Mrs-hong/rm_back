#ifndef QIFENG_FRAMEWORK_ASR_MODEL_PRIVATE_INCLUDE_QWEN3_ASR_BMRUNTIME_H
#define QIFENG_FRAMEWORK_ASR_MODEL_PRIVATE_INCLUDE_QWEN3_ASR_BMRUNTIME_H

#include <atomic>
#include <mutex>
#include <optional>
#include <unordered_set>

#include "bmruntime_interface.h"
#include "json/json.h"
#include "tokenizers_c.h"

#include "aas/aas_callback.h"
#include "asr/model.h"
#include "asr/model/private_include/audio_decoder.h"

namespace qifeng {

    namespace asr {
        // Text processing constants (also used by Model decode loop)
        constexpr const char* UTF8_FFFD = "\xef\xbf\xbd";
        constexpr const char* ASR_TEXT_TAG = "<asr_text>";
        constexpr const char* LANG_PREFIX = "language ";

        constexpr std::size_t IDX_BLOCK_INPUT_STATES = 0;
        constexpr std::size_t IDX_BLOCK_POSITION_IDS = 1;
        constexpr std::size_t IDX_BLOCK_ATTENTION_MASK = 2;
        constexpr std::size_t IDX_BLOCK_HISTORY_K = 3;
        constexpr std::size_t IDX_BLOCK_HISTORY_V = 4;

        constexpr std::size_t IDX_BLOCK_OUTPUT_STATES = 0;
        constexpr std::size_t IDX_BLOCK_K_CACHE = 1;
        constexpr std::size_t IDX_BLOCK_V_CACHE = 2;

        constexpr uint16_t FLOAT16_MASK_VALUE = 0xF0E2;
        constexpr uint16_t BFLOAT16_MASK_VALUE = 0xC61C;

        // Streaming audio window constants
        constexpr float FRAME_SHIFT_MS = 10.0f;
        constexpr int64_t MAX_WINDOW_SAMPLES = static_cast<int64_t>(FRAME_SHIFT_MS * SAMPLE_RATE);
        static_assert(sizeof(float) == BYTES_PER_SAMPLE);

        // 读取模型配置文件中的参数
        using ConfigHandler = std::function<void(const Json::Value&)>;

        class BMRuntimeQwen3ASRModelContext;
        class BMRuntimeQwen3ASRModel;

        // 统一的 decode 循环配置（离线/实时路径共用）
        struct DecodeLoopConfig {
            int maxNewTokens = 0;               // 最大生成 token 数（<=0 表示仅受 mMaxTokenLength 限制）
            int repetitionCheckInterval = 16;   // 重复检测间隔（每 N 个 token 检测一次，<=0 关闭）
            int repetitionMinRepeat = 3;        // 触发重复检测的最小连续重复次数
            int repetitionMaxCheckChars = 256;  // 重复检测只检查尾部最多 N 字节（0=不限制）
            bool streamCallback = false;        // true=通过 callback 逐 token 输出, false=累积到返回值
        };

        // ── 增量流式推理：窗口位移检测 ──────────────────────────────────────────
        enum class WindowShiftMode {
            NEW,     // 全新音频段，需要初始化状态
            EXTEND,  // 前缀扩展
            SHIFT,   // 滑动窗口右移
        };

        struct WindowShiftResult {
            WindowShiftMode mode;
            int64_t shiftSamples = 0;  // 仅在 SHIFT 模式下有效，被移除的旧头部采样点数
        };

        class BMRuntimeQwen3ASRModelContext : public ModelContext {
        public:
            BMRuntimeQwen3ASRModelContext(std::shared_ptr<BMRuntimeQwen3ASRModel> model);
            ~BMRuntimeQwen3ASRModelContext();

            // qifeng::aas::StreamInferContent Generate(const qifeng::aas::Request& request, bool streamingInference) override;
            qifeng::aas::StreamInferContent Generate(const qifeng::aas::Request& request,
                                                     bool streamingInference) override;
            void StreamGenerate(const qifeng::aas::Request& request, StreamGenerateCallback callback) override;
            void CancelGeneration() override;
            void Reset() override;

            // 设置流式推理配置（由 QwenASRWorker 从 config.yaml 加载后注入）
            void SetStreamingConfig(const StreamingConfig& config) {
                mStreamingConfig = config;
            }

        private:
            // ── 流式 ASR 内部状态 ──────────────────────────────────────────────
            struct StreamingState {
                int unfixedChunkNum;
                int unfixedTokenNum;
                int chunkSizeSamples;
                int chunkId;
                std::vector<float> buffer;      // 待消费的 PCM 缓冲区
                std::vector<float> audioAccum;  // 全量累积音频
                std::string promptRaw;          // 基础 prompt（含 chat template）
                std::string forceLanguage;      // 强制语言（空字符串表示自动检测）
                std::string language;           // 当前识别语言
                std::string rawDecoded;         // 上轮完整解码文本（用于 rollback prefix 计算）
                std::string unfixText;          // 本轮未确定的文本，下轮可能被 rollback 修正
                std::string fixText;            // 本轮已确定的文本
                std::string fixBuffer;          // 缓存的确定文本
                void clear() {
                    chunkId = 0;
                    buffer.clear();
                    audioAccum.clear();
                    rawDecoded.clear();
                    unfixText.clear();
                    fixText.clear();
                }
            };

            std::atomic<bool> mGenerationCanceled;

            // ── 跨调用持久化的增量流式状态 ────────────────────────────────────
            bool mStreamingInitialized = false;
            StreamingState mStreamingState;
            StreamingConfig mStreamingConfig;
            std::vector<float> mLastPcmData;  // 上一次的完整 PCM 数据，用于窗口位移检测

            bool DoStreamGenerate(const qifeng::aas::Request& request, StreamGenerateCallback callback);
            bool DoStreamGenerateInference(const qifeng::aas::Request& request, StreamGenerateCallback callback);

            // 统一构造 DecodeLoopConfig：maxNewTokens 由调用方传入（离线/实时不同），
            // 重复检测参数复用 mStreamingConfig，streamCallback 控制输出模式
            DecodeLoopConfig BuildDecodeLoopConfig(int maxNewTokens, bool streamCallback) const;

            // ── 流式 ASR 内部方法 ──────────────────────────────────────────────
            std::string GetPrefixRollback(const std::string& rawDecoded, int unfixedTokenNum,
                                          const std::shared_ptr<BMRuntimeQwen3ASRModel>& model);
            static StreamingState InitStreamingState(const StreamingConfig& config, int chunkSizeSamples,
                                                     const std::string& forceLanguage, const std::string& context);
            static bool UpdateStateWithDecoded(StreamingState& state, const std::string& prefix,
                                               const std::string& genText,
                                               const std::shared_ptr<BMRuntimeQwen3ASRModel>& modelPtr);
            bool ProcessStreamChunk(StreamingState& state, const std::shared_ptr<BMRuntimeQwen3ASRModel>& modelPtr,
                                    const StreamingConfig& config, StreamGenerateCallback callback);
            struct StreamInferenceParams {
                std::string prompt;
                std::string prefix;
                std::unique_ptr<std::vector<float>> audioAccum;
                int maxNewTokens;
                // bool isFinish;
            };
            bool RunInferenceAndUpdateState(StreamingState& state, StreamInferenceParams&& params,
                                            StreamGenerateCallback callback);

            // ── 增量流式推理辅助方法 ──────────────────────────────────────
            static WindowShiftResult DetectWindowShift(const std::vector<float>& newData,
                                                       const std::vector<float>& oldData, int chunkSizeSamples);
            bool FeedDeltaIntoStreamingState(const std::vector<float>& deltaAudio,
                                             const std::shared_ptr<BMRuntimeQwen3ASRModel>& modelPtr,
                                             StreamGenerateCallback callback);
        };

        class BMRuntimeQwen3ASRModel : public Model {
        public:
            BMRuntimeQwen3ASRModel(bm_handle_t handle, void* bmrt, const std::string& tokenizerPath,
                                   const std::string& configPath, const std::string& modelPath);
            ~BMRuntimeQwen3ASRModel();
            std::shared_ptr<ModelContext> CreateContext() override;
            std::string ModelName() const override;
            std::string EngineName() const override;

            bool NonParallelizableStreamGenerate(const std::string& input, std::unique_ptr<std::vector<float>> pcmData,
                                                 StreamGenerateCallback callback, const DecodeLoopConfig& decodeCfg);

            // 流式单轮推理：全量重推一帧，返回原始生成文本（每次调用前清 KV Cache）
            // decodeCfg 控制解码行为（maxNewTokens、重复检测参数等）
            std::string NonParallelizableStreamInference(const std::string& prompt,
                                                         std::unique_ptr<std::vector<float>> pcmData,
                                                         const DecodeLoopConfig& decodeCfg);

        private:
            friend class BMRuntimeQwen3ASRModelContext;
            // 离线转写 maxNewTokens 上限（从 config.json 的 offline_max_new_tokens 加载）
            // DoStreamGenerate 中根据音频长度动态计算：audioSeconds * 30 token/s
            // 此值作为上限约束，防止模型死循环时无限生成
            int mOfflineMaxNewTokens = 4000;
            struct BlockNetwork {
                const bm_net_info_t* block;
                const bm_net_info_t* blockCache;
            };
            struct NetworkInput {
                int32_t tokenLength;
                std::unique_ptr<int32_t[]> tokenBuffer;
                std::unique_ptr<int32_t[]> positionIdBuffer;
                std::unique_ptr<uint16_t[]> attentionMaskBuffer;
                std::vector<float> audioFeatures;
                std::vector<std::size_t> audioSegmentOffsets;
            };
            // 配置与共享不可变状态
            TokenizerHandle mTokenizer;
            std::unordered_set<int32_t> mEosTokens;
            int32_t mAudioBosToken;
            int32_t mAudioEosToken;

            // BMRuntime 相关
            bm_handle_t mHandle;
            void* mpBmrt;

            // 模型网络
            bm_net_info_t* mAudio;
            bm_net_info_t* mEmbed;
            bm_net_info_t* mEmbedCache;
            bm_net_info_t* mLmHead;
            uint16_t mBlockerLayerSize;
            std::unique_ptr<BlockNetwork[]> mLayers;

            // 模型参数
            bool mIsDynamic;
            uint32_t mAudioSegmentTokenLength;
            uint32_t mAudioSegmentFeatureSize;
            uint32_t mAudioSegmentFrameLength;
            uint32_t mHiddenBytes;
            uint32_t mKVBytes;
            int32_t mMaxInputTokenLength;
            int32_t mMaxTokenLength;
            uint16_t mMaskValue;

            // 模型运行时状态
            std::mutex mMutexNetwork;  // 模型的batch只有1，不支持并发
            std::unique_ptr<NetworkInput> mNetworkInput;

            // 复用 tensor 数组避免每次 ForwardNetwork 堆分配
            std::vector<bm_tensor_t> mForwardInputTensors;
            std::vector<bm_tensor_t> mForwardOutputTensors;

            // 缓存 causal attention mask 和 inputIds，避免每次重新分配
            std::vector<uint16_t> mCausalAttentionMask;
            int32_t mCachedMaskTokenLength {-1};    // mask 对应的 tokenLength，-1 表示未缓存
            std::vector<int32_t> mEmbedInputIds;    // embedding 网络输入
            std::vector<int32_t> mBasePositionIds;  // Prefill Step1: 基础 position_ids 复用

            // D复用 attention mask，避免每次 Decode 都堆分配
            std::vector<uint16_t> mDecodeAttentionMask;
            int32_t mDecodeMaskValidKvLen {-1};    // 已填充的有效长度，-1 表示未初始化
            bool mDecodeMaskDeviceSynced {false};  // 设备内存是否已同步（首次需全量传输）

            // 配置参数处理
            std::unordered_map<std::string, ConfigHandler> mConfigHandlers;

            void InitConfigHandlers();
            void InitTokenizer(const std::string& tokenizerPath);
            void LoadConfig(const std::string& configPath);
            void TryParseConfigFile(const std::filesystem::path& path, std::unordered_set<std::string>& keys);
            void LoadBModel(const std::string& modelPath);
            void InitNetworks();
            void InitNetworkParams();
            void Finalize();
            void CleanupAllNetworks();
            void InitNetworkInputIfNeeded();

            // 模型运行时相关函数
            void Prefill();
            void Decode();
            // 统一 decode 循环：支持回调输出和累积返回两种模式，内置周期性 repetition 检测
            // streamCallback=true 时通过 callback 逐 token 输出（离线路径）
            // streamCallback=false 时累积到返回值 rawText（实时路径）
            // 检测到重复时提前终止，避免 TPU Decode 资源浪费
            bool RunDecodeLoopUnified(int32_t inputTokenCount, StreamGenerateCallback callback,
                                      const DecodeLoopConfig& cfg, std::string& rawText);

            // Prefill 子步骤（提取为独立方法以保持 Prefill 简洁）
            void ComputeMropePositionIds(int32_t tokenLength);
            void ForwardEmbedding(int32_t tokenLength);
            void ForwardAudioSegments();
            void BuildCausalAttentionMask(int32_t tokenLength);
            bm_device_mem_t ForwardBlockLayers(int32_t tokenLength);
            void ForwardBlockLayerAndCache(uint16_t layerIndex, int32_t tokenLength, bm_device_mem_t& currentStatesMem);
            void SaveKVCache(uint16_t layerIndex, int32_t tokenLength);
            void ForwardLmHeadAndSample(int32_t tokenLength, bm_device_mem_t currentStatesMem);

            // Decode 子步骤
            void PrepareDecodeInputs(int32_t tokenLength, int32_t currentToken, int32_t positionIds[3]);
            bm_device_mem_t ForwardEmbedCache(int32_t currentToken);
            bm_device_mem_t ForwardBlockCacheLayers(int32_t tokenLength, bm_device_mem_t embedOutMem,
                                                    const int32_t positionIds[3]);

            // ForwardBlockCacheDecode 子步骤
            void PrepareBlockCacheInputsLayer0(uint16_t layerIndex, const int32_t positionIds[3], int32_t attnMaskLen,
                                               int32_t validKvLen);
            void SetupBlockCacheOutputs(uint16_t layerIndex, int32_t kvOffset);

            void ForwardNetwork(const bm_net_info_t* network);
            void ForwardBlockNetworkWithDynamicLength(uint16_t layerIndex, int32_t actualLength);
            void ForwardBlockCacheNetworkForDecode(uint16_t layerIndex, int32_t totalLength);
            // 对照 Python net_launch_decode: 零拷贝传递 hidden states 和 KV cache
            void ForwardBlockCacheDecode(uint16_t layerIndex, bm_device_mem_t& inputStatesMem,
                                         const int32_t* positionIds, int32_t tokenLength);
            int32_t Sample();
            std::optional<std::string> SetupNetworkInput(const std::string& input,
                                                         std::unique_ptr<std::vector<float>> pcmData);
            bool FindAudioSegmentOffsets(const int32_t* tokenIds, int32_t len, std::size_t segmentSize,
                                         std::vector<std::size_t>& offsets);
            void FillNetworkInputBuffers(const int32_t* tokenIds, int32_t len);
            // 提取音频特征
            std::vector<float> ExtractFbankFeatures(std::unique_ptr<std::vector<float>> pcmData);
        };

    }  // namespace asr

}  // namespace qifeng

#endif