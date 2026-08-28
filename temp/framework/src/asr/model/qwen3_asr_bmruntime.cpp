#include <cassert>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string_view>

#include "asr/model/private_include/audio_decoder.h"
#include "asr/model/private_include/fbank_extractor.h"
#include "asr/model/private_include/qwen3_asr_bmruntime.h"
#include "asr/model/private_include/text_processor.h"
#include "common/logger.h"
#include "common/utils/file_utils.h"

#include "aas/audio_load.h"

namespace qifeng {
    namespace asr {

        BMRuntimeQwen3ASRModelContext::BMRuntimeQwen3ASRModelContext(std::shared_ptr<BMRuntimeQwen3ASRModel> model)
            : ModelContext {std::move(model)}, mGenerationCanceled {false} {
        }

        BMRuntimeQwen3ASRModelContext::~BMRuntimeQwen3ASRModelContext() {
        }

        qifeng::aas::StreamInferContent BMRuntimeQwen3ASRModelContext::Generate(const qifeng::aas::Request& request,
                                                                                bool streamingInference) {
            qifeng::aas::StreamInferContent result {};
            StreamGenerateCallback callback = [&result](StreamData data) -> bool {
                if (std::holds_alternative<StreamContent>(data)) {
                    const auto& content = std::get<StreamContent>(data);
                    result.sequence.append(content.sequence);
                } else if (std::holds_alternative<qifeng::aas::StreamInferContent>(data)) {
                    const auto& inferContent = std::get<qifeng::aas::StreamInferContent>(data);
                    result = inferContent;
                } else if (std::holds_alternative<StreamError>(data)) {
                    const auto& error = std::get<StreamError>(data);
                    throw std::runtime_error {error.what};
                }
                return true;
            };

            if (!streamingInference) {
                DoStreamGenerate(request, callback);
            } else {
                DoStreamGenerateInference(request, callback);
            }

            // decode loop 内的周期性检测虽能提前终止，但已通过 callback 输出的重复 token 无法回收
            // 对最终 sequence 做一次 DetectRepetition 兜底去重（minRepeat=10 只捕获真死循环）
            // 与 RunDecodeLoopUnified 内的周期性检测对齐，避免模型死循环时输出大量重复文本
            if (!result.sequence.empty()) {
                std::string deduped;
                if (::qifeng::asr::DetectRepetition(result.sequence, deduped, 3, 2048)) {
                    SLOG_WARN << "[OfflineGenerate] post-dedup triggered, sequenceLen=" << result.sequence.size()
                              << " dedupedLen=" << deduped.size();
                    result.sequence = std::move(deduped);
                }
            }

            return result;
        }

        void BMRuntimeQwen3ASRModelContext::StreamGenerate(const qifeng::aas::Request& request,
                                                           StreamGenerateCallback callback) {
            DoStreamGenerate(request, callback);
        }

        void BMRuntimeQwen3ASRModelContext::CancelGeneration() {
            mGenerationCanceled.store(true, std::memory_order_release);
        }

        void BMRuntimeQwen3ASRModelContext::Reset() {
            mStreamingInitialized = false;
            mStreamingState = StreamingState {};
            mLastPcmData.clear();
        }

        bool BMRuntimeQwen3ASRModelContext::DoStreamGenerate(const qifeng::aas::Request& request,
                                                             StreamGenerateCallback callback) {
            qifeng::aas::PcmData pcm = std::get<qifeng::aas::PcmData>(request);
            std::unique_ptr<std::vector<float>> pcmData = std::make_unique<std::vector<float>>();
            pcmData->resize(static_cast<size_t>(pcm.size) / sizeof(float));
            std::memcpy(pcmData->data(), pcm.raw, static_cast<size_t>(pcm.size));
            auto modelPtr = std::static_pointer_cast<BMRuntimeQwen3ASRModel>(mModel);
            mGenerationCanceled.store(false, std::memory_order_release);
            // 离线路径：根据音频长度动态计算 maxNewTokens
            // 上限为 mOfflineMaxNewTokens
            constexpr float kTokensPerSecond = 20.0f;
            const float audioSeconds = static_cast<float>(pcmData->size()) / static_cast<float>(SAMPLE_RATE);
            const int calculatedMaxNewTokens = static_cast<int>(audioSeconds * kTokensPerSecond);
            const int effectiveMaxNewTokens = (modelPtr->mOfflineMaxNewTokens > 0)
                ? std::min(calculatedMaxNewTokens, modelPtr->mOfflineMaxNewTokens)
                : calculatedMaxNewTokens;
            SLOG_DEBUG << "[OfflineGenerate] audioSeconds=" << audioSeconds
                      << " calculatedMaxNewTokens=" << calculatedMaxNewTokens
                      << " effectiveMaxNewTokens=" << effectiveMaxNewTokens;
            DecodeLoopConfig decodeCfg = BuildDecodeLoopConfig(effectiveMaxNewTokens, true);
            bool ret = modelPtr->NonParallelizableStreamGenerate(BuildPrompt("", "Chinese", ASR_TEXT_TAG),
                                                                 std::move(pcmData), callback, decodeCfg);
            return ret;
        }

        DecodeLoopConfig BMRuntimeQwen3ASRModelContext::BuildDecodeLoopConfig(int maxNewTokens,
                                                                              bool streamCallback) const {
            // 重复检测参数写死（原 RunStreamDecodeLoop 中的硬编码值）
            constexpr int kRepetitionCheckInterval = 16;   // 每 N 个 token 检测一次
            constexpr int kRepetitionMinRepeat = 3;        // 触发检测的最小连续重复次数（阶梯策略在 DedupPatternRepeatsImpl 内部生效）
            constexpr int kRepetitionMaxCheckChars = 2048; // 只检查尾部最多 N 字节（覆盖长重复短语）

            DecodeLoopConfig cfg;
            cfg.maxNewTokens = maxNewTokens;
            cfg.repetitionCheckInterval = kRepetitionCheckInterval;
            cfg.repetitionMinRepeat = kRepetitionMinRepeat;
            cfg.repetitionMaxCheckChars = kRepetitionMaxCheckChars;
            cfg.streamCallback = streamCallback;
            return cfg;
        }

        bool BMRuntimeQwen3ASRModelContext::DoStreamGenerateInference(const qifeng::aas::Request& request,
                                                                      StreamGenerateCallback callback) {
            std::vector<float> pcmData;
            qifeng::aas::PcmData pcm = std::get<qifeng::aas::PcmData>(request);
            pcmData.resize(static_cast<size_t>(pcm.size) / sizeof(float));
            std::memcpy(pcmData.data(), pcm.raw, static_cast<size_t>(pcm.size));
            auto modelPtr = std::static_pointer_cast<BMRuntimeQwen3ASRModel>(mModel);
            const int chunkSizeSamples = std::max(1, static_cast<int>(mStreamingConfig.chunkSizeSec * SAMPLE_RATE));
            WindowShiftResult shiftResult = DetectWindowShift(pcmData, mLastPcmData, chunkSizeSamples);
            if (!mStreamingInitialized || shiftResult.mode == WindowShiftMode::NEW) {
                // 全新段：初始化流式状态
                mStreamingInitialized = true;
                mStreamingConfig.language = "Chinese";
                // std::string forceLanguage = NormalizeLanguage(mStreamingConfig.language);
                mStreamingState = InitStreamingState(mStreamingConfig, chunkSizeSamples, mStreamingConfig.language,
                                                     mStreamingConfig.context);
                // 全量 PCM 作为增量
                if (!FeedDeltaIntoStreamingState(pcmData, modelPtr, callback)) {
                    return false;
                }
            } else if (shiftResult.mode == WindowShiftMode::EXTEND) {
                // 前缀扩展：只取新增尾部
                std::size_t oldSize = mLastPcmData.size();
                if (pcmData.size() > oldSize) {
                    std::vector<float> deltaAudio(pcmData.begin() + static_cast<std::ptrdiff_t>(oldSize),
                                                  pcmData.end());
                    if (!FeedDeltaIntoStreamingState(deltaAudio, modelPtr, callback)) {
                        return false;
                    }
                }
            } else {
                // 滑动窗口右移（SHIFT）：移除 audioAccum 旧头部 + 喂入新增尾部
                int64_t shiftSamples = shiftResult.shiftSamples;
                if (shiftSamples > 0 && static_cast<int64_t>(mStreamingState.audioAccum.size()) >= shiftSamples) {
                    mStreamingState.audioAccum.erase(mStreamingState.audioAccum.begin(),
                                                     mStreamingState.audioAccum.begin() +
                                                         static_cast<std::ptrdiff_t>(shiftSamples));
                }
                int64_t overlap = static_cast<int64_t>(mLastPcmData.size()) - shiftSamples;
                if (overlap >= 0 && static_cast<int64_t>(pcmData.size()) > overlap) {
                    std::vector<float> deltaAudio(pcmData.begin() + static_cast<std::ptrdiff_t>(overlap),
                                                  pcmData.end());
                    if (!FeedDeltaIntoStreamingState(deltaAudio, modelPtr, callback)) {
                        return false;
                    }
                }
            }
            mLastPcmData = std::move(pcmData);
            callback(StreamEnd {StreamEnd::Reason::STOPPED});
            return true;
        }

        // ── DetectWindowShift: 检测窗口位移模式 ──────────────────────────────
        WindowShiftResult BMRuntimeQwen3ASRModelContext::DetectWindowShift(const std::vector<float>& newData,
                                                                           const std::vector<float>& oldData,
                                                                           int chunkSizeSamples) {
            WindowShiftResult result {};
            if (oldData.empty()) {
                result.mode = WindowShiftMode::NEW;
                return result;
            }
            const std::size_t oldSize = oldData.size();
            const std::size_t newSize = newData.size();
            // 模式 1：前缀扩展 — 新数据的前缀完全匹配旧数据
            if (newSize >= oldSize && std::memcmp(newData.data(), oldData.data(), oldSize * sizeof(float)) == 0) {
                result.mode = WindowShiftMode::EXTEND;
                return result;
            }
            // 模式 2：滑动窗口右移 — 新数据前缀匹配旧数据的某个偏移
            for (int shift = chunkSizeSamples; shift < static_cast<int>(oldSize); shift += chunkSizeSamples) {
                int64_t overlap = static_cast<int64_t>(std::min(newSize, oldSize - static_cast<std::size_t>(shift)));
                if (overlap < chunkSizeSamples) {
                    continue;
                }
                if (std::memcmp(newData.data(), oldData.data() + shift,
                                static_cast<std::size_t>(overlap) * sizeof(float)) == 0) {
                    result.mode = WindowShiftMode::SHIFT;
                    result.shiftSamples = static_cast<int64_t>(shift);
                    return result;
                }
            }
            // 模式 3：不匹配 — 视为全新段
            result.mode = WindowShiftMode::NEW;
            return result;
        }

        // ── FeedDeltaIntoStreamingState: 将增量音频喂入持久化状态并触发推理 ─────
        bool BMRuntimeQwen3ASRModelContext::FeedDeltaIntoStreamingState(
            const std::vector<float>& deltaAudio, const std::shared_ptr<BMRuntimeQwen3ASRModel>& modelPtr,
            StreamGenerateCallback callback) {
            if (deltaAudio.empty()) {
                SLOG_INFO << "deltaAudio is empty, skip";
                return true;
            }
            StreamingState& state = mStreamingState;
            if (state.audioAccum.empty()) {
                state.audioAccum = deltaAudio;
            } else {
                state.audioAccum.insert(state.audioAccum.end(), deltaAudio.begin(), deltaAudio.end());
                if (static_cast<int>(state.audioAccum.size()) > MAX_WINDOW_SAMPLES) {
                    state.audioAccum.erase(state.audioAccum.begin(), state.audioAccum.end() - MAX_WINDOW_SAMPLES);
                }
            }
            if (!ProcessStreamChunk(state, modelPtr, mStreamingConfig, callback)) {
                return false;
            }
            return true;
        }

        // ── InitStreamingState: 初始化流式状态 ────────────────────────────────
        BMRuntimeQwen3ASRModelContext::StreamingState
        BMRuntimeQwen3ASRModelContext::InitStreamingState(const StreamingConfig& config, int chunkSizeSamples,
                                                          const std::string& forceLanguage,
                                                          const std::string& context) {
            StreamingState state {};
            state.unfixedChunkNum = config.unfixedChunkNum;
            state.unfixedTokenNum = config.unfixedTokenNum;
            state.chunkSizeSamples = chunkSizeSamples;
            state.chunkId = 0;
            state.promptRaw = BuildPrompt(context, forceLanguage, ASR_TEXT_TAG);
            state.forceLanguage = forceLanguage;
            return state;
        }

        bool
        BMRuntimeQwen3ASRModelContext::UpdateStateWithDecoded(StreamingState& state, const std::string& prefix,
                                                              const std::string& genText,
                                                              const std::shared_ptr<BMRuntimeQwen3ASRModel>& modelPtr) {
            if (genText.empty()) {
                return true;
            }
            // [FIX] 检测 repetition：嘈杂/多人环境下模型易陷入重复输出（如"有多少亩？有多少亩？..."）
            // 检测到重复时，去重 genText 并强制 isDecoded=true 触发 state.clear()，切断自强化循环
            std::string effectiveGenText = genText;
            bool isRepetition = ::qifeng::asr::DetectRepetition(genText, effectiveGenText);
            if (isRepetition) {
                SLOG_WARN << "[StreamInference] repetition detected, genTextLen=" << genText.size()
                          << " dedupLen=" << effectiveGenText.size()
                          << " forcing state clear to break self-reinforcing loop";
            }
            int genTokenCount = 0;
            std::vector<int32_t> genIds;
            TokenizerEncodeResult* encodeResult =
                ::tokenizer_encode(modelPtr->mTokenizer, effectiveGenText.c_str(), false);
            if (encodeResult != nullptr) {
                genTokenCount = static_cast<int>(encodeResult->len);
                if (genTokenCount > 0) {
                    const int32_t* ids = reinterpret_cast<const int32_t*>(encodeResult->token_ids);
                    genIds.assign(ids, ids + genTokenCount);
                }
                ::free_tokenizer_encode_result(encodeResult);
            }
            int K = state.unfixedTokenNum;
            std::string fullDecoded = prefix + effectiveGenText;
            // [DEBUG] 记录 UpdateStateWithDecoded 关键状态，用于排查丢句
            SLOG_DEBUG << "[UpdateStateWithDecoded] chunkId=" << state.chunkId
                       << " genText=" << genText
                       << " effectiveGenText=" << effectiveGenText
                       << " prefix=" << prefix
                       << " fullDecoded=" << fullDecoded
                       << " genTokenCount=" << genTokenCount
                       << " K=" << K
                       << " isRepetition=" << isRepetition;
            // [FIX] 检测到 repetition 时强制 clear，防止重复 prefix 引导下一轮继续重复
            if (isRepetition) {
                state.fixText.clear();
                state.unfixText.clear();
                auto [_, plain] = ParseAsrOutput(effectiveGenText, state.forceLanguage, ASR_TEXT_TAG, LANG_PREFIX);
                if (!state.fixBuffer.empty()) {
                    state.fixText += state.fixBuffer;
                    state.fixBuffer.clear();
                }
                state.fixText += plain;
                return true;
            }
            if (genTokenCount > K) {
                state.rawDecoded = fullDecoded;
                int fixLen = genTokenCount - K;
                TokenizerDecodeResult* decodeResult =
                    ::tokenizer_decode(modelPtr->mTokenizer, reinterpret_cast<const uint32_t*>(genIds.data()),
                                       static_cast<std::size_t>(fixLen), false);

                state.fixText = (decodeResult != nullptr) ? std::string(decodeResult->text) : "";
                if (decodeResult)
                    ::free_tokenizer_decode_result(decodeResult);
                if (!state.fixText.empty()) {
                    auto [_, plain] = ParseAsrOutput(state.fixText, state.forceLanguage, ASR_TEXT_TAG, LANG_PREFIX);
                    state.fixText.clear();
                    if (state.chunkId >= state.unfixedChunkNum - 1) {
                        state.fixText = plain;
                        state.fixBuffer.clear();
                    } else {
                        state.fixBuffer += plain;
                    }
                }
                // unfix_text: 后 K 个 token
                TokenizerDecodeResult* unfixResult =
                    ::tokenizer_decode(modelPtr->mTokenizer, reinterpret_cast<const uint32_t*>(genIds.data() + fixLen),
                                       static_cast<std::size_t>(K), true);
                state.unfixText = (unfixResult != nullptr) ? std::string(unfixResult->text) : "";
                if (unfixResult)
                    ::free_tokenizer_decode_result(unfixResult);
                state.chunkId++;
                SLOG_DEBUG << "[UpdateStateWithDecoded] genTokenCount>K branch, fixLen=" << fixLen
                           << " fixText=" << state.fixText << " unfixText=" << state.unfixText
                           << " fixBuffer=" << state.fixBuffer;
            } else {
                state.fixText.clear();
                auto [_, plain] = ParseAsrOutput(genText, state.forceLanguage, ASR_TEXT_TAG, LANG_PREFIX);
                if (!state.fixBuffer.empty()) {
                    state.fixText += state.fixBuffer;
                    state.fixBuffer.clear();
                }
                state.fixText += plain;
                state.unfixText.clear();
                SLOG_DEBUG << "[UpdateStateWithDecoded] genTokenCount<=K branch, fixText=" << state.fixText
                           << " plain=" << plain;
                return true;
            }
            return false;
        }

        // ── ProcessStreamChunk: 处理单个音频块（prefix rollback / truncate / infer / callback）
        bool BMRuntimeQwen3ASRModelContext::ProcessStreamChunk(StreamingState& state,
                                                               const std::shared_ptr<BMRuntimeQwen3ASRModel>& modelPtr,
                                                               const StreamingConfig& config,
                                                               StreamGenerateCallback callback) {
            std::string prefix;
            if (state.chunkId >= state.unfixedChunkNum && !state.rawDecoded.empty()) {
                prefix = GetPrefixRollback(state.rawDecoded, state.unfixedTokenNum, modelPtr);
            }
            prefix = TruncateAtPunctuation(prefix, 200);
            std::string prompt = state.promptRaw + prefix;
            auto pcmCopy = std::make_unique<std::vector<float>>(state.audioAccum);
            return RunInferenceAndUpdateState(
                state, StreamInferenceParams {prompt, prefix, std::move(pcmCopy), config.maxNewTokens}, callback);
        }

        // ── RunInferenceAndUpdateState: 单轮推理并更新状态
        bool BMRuntimeQwen3ASRModelContext::RunInferenceAndUpdateState(StreamingState& state,
                                                                       StreamInferenceParams&& params,
                                                                       StreamGenerateCallback callback) {
            auto modelPtr = std::static_pointer_cast<BMRuntimeQwen3ASRModel>(mModel);
            std::string genText;
            const auto startTime = std::chrono::steady_clock::now();
            try {
                // 实时路径：使用 params.maxNewTokens，重复检测参数写死在 BuildDecodeLoopConfig
                DecodeLoopConfig decodeCfg = BuildDecodeLoopConfig(params.maxNewTokens, false);
                genText =
                    modelPtr->NonParallelizableStreamInference(params.prompt, std::move(params.audioAccum), decodeCfg);

            } catch (const ModelContext::CanceledException&) {
                throw;
            } catch (const std::exception& e) {
                SLOG_ERROR << "[StreamInference]"
                           << " inference error: " << e.what();
                // [FIX] 错误路径强制 clear state，避免膨胀 state 持续触发错误
                SLOG_WARN << "[StreamInference] forcing state.clear() on inference error to avoid stale state";
                state.clear();
                callback(StreamError {e.what()});
                return false;
            }
            const auto endTime = std::chrono::steady_clock::now();
            bool isDecoded = UpdateStateWithDecoded(state, params.prefix, genText, modelPtr);
            qifeng::aas::StreamInferContent content;
            content.fixText = state.fixText;
            content.unfixText = state.unfixText;
            content.isDecoded = isDecoded;
            if (isDecoded) {
                state.clear();
            }
            std::string isReset = isDecoded ? "reset" : "continue";
            const auto duration = std::chrono::duration<double>(endTime - startTime).count();
            SLOG_DEBUG << "[StreamInference]" << (" chunk " + std::to_string(state.chunkId))
                      << " language=" << state.language;
            SLOG_DEBUG << "prefix= " << params.prefix << " isReset= " << isReset;
            SLOG_DEBUG << "genText= " << genText << " fixText= " << content.fixText
                      << " unfixText= " << content.unfixText;
            SLOG_DEBUG << ("第" + std::to_string(state.chunkId) + "次") << " time: " << duration << " s";
            return callback(content);
        }

        // ── GetPrefixRollback: 用上一轮输出截去最后 N 个 token 作为前缀 ────────
        std::string
        BMRuntimeQwen3ASRModelContext::GetPrefixRollback(const std::string& rawDecoded, int unfixedTokenNum,
                                                         const std::shared_ptr<BMRuntimeQwen3ASRModel>& model) {
            if (rawDecoded.empty()) {
                return "";
            }
            std::string prefix = GetRollbackPrefix(rawDecoded, unfixedTokenNum, model->mTokenizer);
            return prefix;
        }

        // NonParallelizableStreamInference: 单轮全量重推推理
        std::string BMRuntimeQwen3ASRModel::NonParallelizableStreamInference(
            const std::string& prompt, std::unique_ptr<std::vector<float>> pcmData, const DecodeLoopConfig& decodeCfg) {
            if (pcmData == nullptr || pcmData->empty()) {
                throw std::runtime_error {"audio input is empty"};
            }
            std::unique_lock<std::mutex> lock {mMutexNetwork};
            InitNetworkInputIfNeeded();

            if (auto err = SetupNetworkInput(prompt, std::move(pcmData)); err) {
                throw std::runtime_error {err.value()};
            }
            CleanupAllNetworks();
            auto prefillStartTime = std::chrono::steady_clock::now();
            Prefill();
            auto decodeStartTime = std::chrono::steady_clock::now();
            const int32_t inputTokenCount = mNetworkInput->tokenLength;

            // 实时路径：使用统一 decode 循环，累积到 rawText 返回
            // 重复检测参数由 decodeCfg 传入（写死在 BuildDecodeLoopConfig）
            std::string rawText;
            RunDecodeLoopUnified(inputTokenCount, nullptr, decodeCfg, rawText);

            const auto decodeEndTime = std::chrono::steady_clock::now();
            const auto prefillTime =
                std::chrono::duration_cast<std::chrono::milliseconds>(decodeStartTime - prefillStartTime).count();
            const auto decodeTime =
                std::chrono::duration_cast<std::chrono::milliseconds>(decodeEndTime - decodeStartTime).count();
            const int32_t outputTokenCount = mNetworkInput->tokenLength - inputTokenCount;
            const float decodeSpeed =
                decodeTime == 0 ? 0.0f
                                : static_cast<float>(outputTokenCount) * 1000.0f / static_cast<float>(decodeTime);
            SLOG_DEBUG << "[StreamInference] prefill time=" << prefillTime << "ms "
                      << "decode: outputToken=" << outputTokenCount << " decodeTime=" << decodeTime << "ms"
                      << " decodeSpeed=" << decodeSpeed << " token/s"
                      << " rawText=" << rawText;
            return rawText;
        }

        // ── RunDecodeLoopUnified: 统一 decode 循环 ─────────────────────────────
        // 替代旧的 RunDecodeLoop（实时路径，累积返回）和 RunStreamDecodeLoop（离线路径，callback 输出）
        // 内置周期性 repetition 检测，检测到重复时提前终止，避免 TPU Decode 资源浪费
        bool BMRuntimeQwen3ASRModel::RunDecodeLoopUnified(int32_t inputTokenCount, StreamGenerateCallback callback,
                                                          const DecodeLoopConfig& cfg, std::string& rawText) {
            std::vector<int32_t> outputTokens {};
            outputTokens.reserve(8);
            std::string accumulatedText;  // 累积已输出文本，供 repetition 检测
            accumulatedText.reserve(256);
            int decodeTokenCount = 0;

            while (mNetworkInput->tokenLength <= mMaxTokenLength) {
                int32_t currentToken =
                    mNetworkInput->tokenBuffer[static_cast<std::size_t>(mNetworkInput->tokenLength - 1)];
                if (mEosTokens.find(currentToken) != mEosTokens.end()) {
                    SLOG_DEBUG << "[DecodeLoop] Hit EOS token " << currentToken;
                    break;
                }
                outputTokens.push_back(currentToken);

                // token 解码
                bool isComplete = false;
                std::string word;
                {
                    TokenizerDecodeResult* result = ::tokenizer_decode(
                        mTokenizer, reinterpret_cast<const uint32_t*>(outputTokens.data()), outputTokens.size(),
                        !cfg.streamCallback);  // 实时路径 skip_special_tokens=true
                    if (result == nullptr) {
                        const char* err = ::tokenizer_get_last_error_in_handle(mTokenizer);
                        SLOG_ERROR << "[DecodeLoop] tokenizer_decode failed, clearing outputTokens, "
                                   << "err=" << (err == nullptr ? "unknown" : err)
                                   << " outputTokens.size=" << outputTokens.size();
                        outputTokens.clear();
                        if (cfg.streamCallback) {
                            callback(StreamError {err == nullptr
                                                      ? "Tokenizer decode failed."
                                                      : "Tokenizer decode failed, reason = " + std::string {err}});
                        }
                        return false;
                    }
                    std::string_view wordView(result->text);
                    isComplete = (wordView.find(UTF8_FFFD) == std::string_view::npos);
                    if (isComplete) {
                        word.assign(result->text);
                    }
                    ::free_tokenizer_decode_result(result);
                }
                if (!isComplete) {
                    // token 不完整（UTF-8 跨 token），保留 outputTokens 继续累积
                    // 防御性保护：若 outputTokens 持续不完整（如模型持续输出含 U+FFFD 的 token），
                    // 限制其大小避免内存膨胀。超过上限时清空并丢弃未完成序列，
                    // 避免滑动窗口破坏 UTF-8 序列导致后续 decode 产生乱码。
                    constexpr int kMaxIncompleteTokens = 16;
                    if (static_cast<int>(outputTokens.size()) > kMaxIncompleteTokens) {
                        SLOG_WARN << "[DecodeLoop] outputTokens overflow, clearing incomplete tokens, "
                                  << "currentToken=" << currentToken
                                  << " decodeTokenCount=" << decodeTokenCount
                                  << " outputTokens.size=" << outputTokens.size();
                        outputTokens.clear();
                    }
                } else {
                    if (cfg.streamCallback && outputTokens.size() == 1) {
                        std::string preWord = word;
                        int32_t pairTokens[2] = {currentToken, currentToken};
                        TokenizerDecodeResult* pairResult =
                            ::tokenizer_decode(mTokenizer, reinterpret_cast<const uint32_t*>(pairTokens), 2, false);
                        if (pairResult != nullptr) {
                            std::string_view pairView(pairResult->text);
                            if (pairView.size() >= preWord.size()) {
                                word.assign(pairView.substr(preWord.size()));
                            }
                            ::free_tokenizer_decode_result(pairResult);
                        }
                    }

                    // 输出 token
                    if (cfg.streamCallback) {
                        if (word == "<asr_text>") {
                            if (!callback(StreamContent {std::string {"\n"}})) {
                                return false;
                            }
                        } else {
                            accumulatedText += word;
                            if (!callback(StreamContent {std::move(word)})) {
                                return false;
                            }
                        }
                    } else {
                        // 实时路径：累积到 rawText
                        if (word == "<asr_text>") {
                            rawText += "\n";
                        } else {
                            rawText += word;
                            accumulatedText += word;
                        }
                    }
                    outputTokens.clear();
                }

                decodeTokenCount++;
                // maxNewTokens 限制
                if (cfg.maxNewTokens > 0 && decodeTokenCount >= cfg.maxNewTokens) {
                    SLOG_DEBUG << "[DecodeLoop] Reached maxNewTokens=" << cfg.maxNewTokens;
                    break;
                }
                // 周期性 repetition 检测（accumulatedText >= 64 字节才检测，减少误触发）
                if (cfg.repetitionCheckInterval > 0 && decodeTokenCount % cfg.repetitionCheckInterval == 0 &&
                    accumulatedText.size() >= 64) {
                    std::string dedupText;
                    if (::qifeng::asr::DetectRepetition(accumulatedText, dedupText, cfg.repetitionMinRepeat,
                                                        static_cast<std::size_t>(cfg.repetitionMaxCheckChars))) {
                        SLOG_WARN << "[DecodeLoop] repetition detected, terminating early. "
                                  << "accumulatedLen=" << accumulatedText.size() << " dedupLen=" << dedupText.size()
                                  << " decodeTokenCount=" << decodeTokenCount
                                  << " streamCallback=" << cfg.streamCallback
                                  << " accumulatedText=" << accumulatedText;
                        break;
                    }
                }
                if (mNetworkInput->tokenLength == mMaxTokenLength) {
                    SLOG_DEBUG << "[DecodeLoop] Reached maxTokenLength=" << mMaxTokenLength;
                    break;
                }
                Decode();
            }
            return true;
        }

        BMRuntimeQwen3ASRModel::BMRuntimeQwen3ASRModel(bm_handle_t handle, void* bmrt, const std::string& tokenizerPath,
                                                       const std::string& configPath, const std::string& modelPath)
            : mTokenizer {nullptr}, mEosTokens {}, mAudioBosToken {-1}, mAudioEosToken {-1}, mHandle {handle},
              mpBmrt {bmrt}, mAudio {nullptr}, mEmbed {nullptr}, mEmbedCache {nullptr}, mLmHead {nullptr},
              mBlockerLayerSize {0}, mLayers {nullptr}, mAudioSegmentTokenLength {0}, mAudioSegmentFeatureSize {0},
              mAudioSegmentFrameLength {0}, mHiddenBytes {0}, mKVBytes {0}, mMaxInputTokenLength {0},
              mMaxTokenLength {0}, mMaskValue {0}, mMutexNetwork {}, mNetworkInput {nullptr} {
            try {
                InitTokenizer(tokenizerPath);
                InitConfigHandlers();
                if (!configPath.empty()) {
                    LoadConfig(configPath);
                    LoadBModel(modelPath);
                }
            } catch (...) {
                Finalize();
                throw;
            }
        }

        void BMRuntimeQwen3ASRModel::InitTokenizer(const std::string& tokenizerPath) {
            std::filesystem::path tokenizerU8path = std::filesystem::path(tokenizerPath);
            std::string tokenizerJson = common::utils::LoadAllContentFromFile(tokenizerU8path);
            mTokenizer = ::new_tokenizer_from_json(tokenizerJson.c_str());
            if (mTokenizer == nullptr) {
                const char* err = ::tokenizer_get_last_error();
                throw std::runtime_error {err == nullptr ? "Tokenizer init failed." : err};
            }
        }
        void BMRuntimeQwen3ASRModel::InitConfigHandlers() {
            mConfigHandlers = {{"audio_start_token_id", [this](const Json::Value& v) { mAudioBosToken = v.asInt(); }},
                               {"audio_end_token_id", [this](const Json::Value& v) { mAudioEosToken = v.asInt(); }},
                               {"eos_token_id",
                                [this](const Json::Value& v) {
                                    if (v.isArray()) {
                                        for (const auto& t : v)
                                            mEosTokens.insert(t.asInt());
                                    } else {
                                        mEosTokens.insert(v.asInt());
                                    }
                                }},
                               {"offline_max_new_tokens", [this](const Json::Value& v) {
                                    int val = v.asInt();
                                    if (val > 0) {
                                        mOfflineMaxNewTokens = val;
                                    }
                                }}};
        }

        void BMRuntimeQwen3ASRModel::LoadConfig(const std::string& configPath) {
            std::unordered_set<std::string> missingKeys;

            // offline_max_new_tokens 是可选配置，不加入 missingKeys 检查
            for (const auto& item : mConfigHandlers) {
                if (item.first == "offline_max_new_tokens") {
                    continue;
                }
                missingKeys.insert(item.first);
            }

            if (!configPath.empty()) {
                TryParseConfigFile(configPath, missingKeys);
            }

            if (!missingKeys.empty()) {
                TryParseConfigFile(std::filesystem::path(configPath).parent_path() / "generation_config.json",
                                   missingKeys);
            }

            if (!missingKeys.empty()) {
                std::string missingList;

                for (const auto& key : missingKeys) {
                    if (!missingList.empty()) {
                        missingList += ", ";
                    }
                    missingList += key;
                }

                throw std::runtime_error("Required config keys not found: " + missingList);
            }

            // 单独尝试解析可选配置项 offline_max_new_tokens
            if (!configPath.empty()) {
                std::unordered_set<std::string> optionalKeys {"offline_max_new_tokens"};
                TryParseConfigFile(configPath, optionalKeys);
                if (!optionalKeys.empty()) {
                    TryParseConfigFile(std::filesystem::path(configPath).parent_path() / "generation_config.json",
                                       optionalKeys);
                }
            }
        }

        void BMRuntimeQwen3ASRModel::TryParseConfigFile(const std::filesystem::path& path,
                                                        std::unordered_set<std::string>& missingKeys) {
            if (!std::filesystem::exists(path)) {
                return;
            }
            std::string jsonStr = common::utils::LoadAllContentFromFile(path);
            Json::Reader reader;
            Json::Value json;

            if (!reader.parse(jsonStr, json)) {
                throw std::runtime_error("Failed to parse " + path.string() + ": " +
                                         reader.getFormattedErrorMessages());
            }

            auto resolveKey = [&json](const std::string& key) -> const Json::Value* {
                if (json.isMember(key)) {
                    return &json[key];
                }
                if (json.isMember("thinker_config")) {
                    const auto& thinker = json["thinker_config"];
                    if (thinker.isObject() && thinker.isMember(key)) {
                        return &thinker[key];
                    }
                }
                return nullptr;
            };

            for (auto it = missingKeys.begin(); it != missingKeys.end();) {
                const Json::Value* value = resolveKey(*it);
                if (value == nullptr) {
                    ++it;
                    continue;
                }
                mConfigHandlers.at (*it)(*value);
                it = missingKeys.erase(it);
            }
        }

        void BMRuntimeQwen3ASRModel::LoadBModel(const std::string& modelPath) {
            bool ret = ::bmrt_load_bmodel(mpBmrt, modelPath.c_str());
            if (!ret) {
                throw std::runtime_error {"Failed to load bmodel."};
            }
            InitNetworks();
            InitNetworkParams();
        }

        void BMRuntimeQwen3ASRModel::InitNetworks() {
            int netSize = ::bmrt_get_network_number(mpBmrt);
            if (netSize <= 4) {
                throw std::runtime_error {"Net size of bmodel is unexpected."};
            }
            mAudio = const_cast<bm_net_info_t*>(::bmrt_get_network_info(mpBmrt, "audio"));
            if (mAudio == nullptr) {
                throw std::runtime_error {"Net architecture of bmodel is unexpected."};
            }
            mEmbed = const_cast<bm_net_info_t*>(::bmrt_get_network_info(mpBmrt, "embedding"));
            if (mEmbed == nullptr) {
                throw std::runtime_error {"Net architecture of bmodel is unexpected."};
            }
            mEmbedCache = const_cast<bm_net_info_t*>(::bmrt_get_network_info(mpBmrt, "embedding_cache"));
            if (mEmbedCache == nullptr) {
                throw std::runtime_error {"Net architecture of bmodel is unexpected."};
            }
            mLmHead = const_cast<bm_net_info_t*>(::bmrt_get_network_info(mpBmrt, "lm_head"));
            if (mLmHead == nullptr) {
                throw std::runtime_error {"Net architecture of bmodel is unexpected."};
            }
            if (mLmHead->stages[0].output_shapes[0].dims[1] != 1) {
                throw std::runtime_error {"Net architecture of bmodel is unexpected."};
            }
            int blockSize = netSize - 4;
            if (blockSize <= 0 || blockSize % 2 != 0) {
                throw std::runtime_error {"Net architecture of bmodel is unexpected."};
            }
            mBlockerLayerSize = static_cast<uint16_t>(blockSize / 2);
            mLayers = std::make_unique<BlockNetwork[]>(mBlockerLayerSize);
            for (uint16_t layerIndex = 0; layerIndex < mBlockerLayerSize; layerIndex++) {
                std::string blockName {"block_" + std::to_string(layerIndex)};
                std::string blockCacheName {"block_cache_" + std::to_string(layerIndex)};
                const bm_net_info_t* block = ::bmrt_get_network_info(mpBmrt, blockName.c_str());
                if (block == nullptr) {
                    throw std::runtime_error {"Net architecture of bmodel is unexpected."};
                }
                const bm_net_info_t* blockCache = ::bmrt_get_network_info(mpBmrt, blockCacheName.c_str());
                if (blockCache == nullptr) {
                    throw std::runtime_error {"Net architecture of bmodel is unexpected."};
                }
                mLayers[layerIndex] = BlockNetwork {block, blockCache};
            }
        }

        void BMRuntimeQwen3ASRModel::InitNetworkParams() {
            mAudioSegmentTokenLength = static_cast<uint32_t>(mAudio->stages[0].output_shapes[0].dims[1]);
            mAudioSegmentFeatureSize = static_cast<uint32_t>(mAudio->stages[0].input_shapes[0].dims[2]);
            mAudioSegmentFrameLength = static_cast<uint32_t>(mAudio->stages[0].input_shapes[0].dims[3]);
            mHiddenBytes = static_cast<uint32_t>(
                ::bm_mem_get_device_size(mLayers[0].blockCache->stages[0].output_mems[IDX_BLOCK_OUTPUT_STATES]));
            mKVBytes = static_cast<uint32_t>(
                ::bm_mem_get_device_size(mLayers[0].blockCache->stages[0].output_mems[IDX_BLOCK_K_CACHE]));
            mMaxInputTokenLength = static_cast<int32_t>(mEmbed->stages[0].input_shapes[0].dims[1]);
            mMaxTokenLength =
                static_cast<int32_t>(mLayers[0].blockCache->stages[0].input_shapes[IDX_BLOCK_HISTORY_K].dims[1]);
            if (mMaxInputTokenLength > mMaxTokenLength) {
                throw std::runtime_error {"Net architecture of bmodel is unexpected."};
            }
            if (mEmbedCache->output_dtypes[0] == BM_FLOAT16) {
                mMaskValue = FLOAT16_MASK_VALUE;
            } else if (mEmbedCache->output_dtypes[0] == BM_BFLOAT16) {
                mMaskValue = BFLOAT16_MASK_VALUE;
            } else {
                throw std::runtime_error {
                    "Invalid attention dtype. Supported dtype are 'BM_FLOAT16' or 'BM_BFLOAT16'."};
            }
            mIsDynamic = mLayers[0].block->is_dynamic;
        }

        BMRuntimeQwen3ASRModel::~BMRuntimeQwen3ASRModel() {
            Finalize();
        }

        std::shared_ptr<ModelContext> BMRuntimeQwen3ASRModel::CreateContext() {
            return std::make_shared<BMRuntimeQwen3ASRModelContext>(
                std::static_pointer_cast<BMRuntimeQwen3ASRModel>(shared_from_this()));
        }
        std::string BMRuntimeQwen3ASRModel::ModelName() const {
            return "qwen3_asr";
        }
        std::string BMRuntimeQwen3ASRModel::EngineName() const {
            return "bmruntime";
        }

        bool BMRuntimeQwen3ASRModel::NonParallelizableStreamGenerate(const std::string& input,
                                                                     std::unique_ptr<std::vector<float>> pcmData,
                                                                     StreamGenerateCallback callback,
                                                                     const DecodeLoopConfig& decodeCfg) {
            assert(callback != nullptr);
            if (pcmData == nullptr || pcmData->empty()) {
                callback(StreamError {"audio input is empty"});
                return false;
            }
            std::unique_lock<std::mutex> lock {mMutexNetwork};
            InitNetworkInputIfNeeded();

            if (auto err = SetupNetworkInput(input, std::move(pcmData)); err) {
                callback(StreamError {err.value()});
                return false;
            }
            mDecodeMaskDeviceSynced = false;
            CleanupAllNetworks();
            auto prefillStartTime = std::chrono::steady_clock::now();
            Prefill();
            auto decodeStartTime = std::chrono::steady_clock::now();
            const int32_t inputTokenCount = mNetworkInput->tokenLength;

            // 离线路径：使用统一 decode 循环，通过 callback 逐 token 输出
            // decodeCfg 由调用方通过 BuildDecodeLoopConfig 构造，重复检测参数写死
            std::string rawText;  // 离线路径不使用返回值
            if (!RunDecodeLoopUnified(inputTokenCount, callback, decodeCfg, rawText)) {
                return false;
            }
            auto decodeEndTime = std::chrono::steady_clock::now();
            const int32_t outputTokenCount = mNetworkInput->tokenLength - inputTokenCount;
            const auto prefillTimeSec = std::chrono::duration<double>(decodeStartTime - prefillStartTime).count();
            const auto decodeTimeSec = std::chrono::duration<double>(decodeEndTime - decodeStartTime).count();
            SLOG_DEBUG << "NonParallelizableStreamGenerate finished: "
                      << "totalTime = " << (prefillTimeSec + decodeTimeSec) << " s\n"
                      << "inputToken = " << inputTokenCount << " , "
                      << "prefillTime = " << prefillTimeSec << " s "
                      << "prefillSpeed = " << (prefillTimeSec == 0.0 ? 0.0f : inputTokenCount / prefillTimeSec)
                      << " token/s \n"
                      << "outputToken = " << outputTokenCount << " , "
                      << "decodeTime = " << decodeTimeSec << " s "
                      << "decodeSpeed = " << (decodeTimeSec == 0.0 ? 0.0f : outputTokenCount / decodeTimeSec)
                      << " token/s \n";
            return true;
        }

        void BMRuntimeQwen3ASRModel::Finalize() {
            if (mTokenizer != nullptr) {
                ::free_tokenizer(mTokenizer);
                mTokenizer = nullptr;
            }
            // qwen_asr worker会析构
            // if (mpBmrt) {
            //     bmrt_destroy(mpBmrt);
            //     mpBmrt = nullptr;
            // }

            // if (mHandle) {
            //     bm_dev_free(mHandle);
            //     mHandle = nullptr;
            // }
        }

        void BMRuntimeQwen3ASRModel::InitNetworkInputIfNeeded() {
            if (mNetworkInput != nullptr) {
                return;
            }
            mNetworkInput = std::make_unique<NetworkInput>();
            mNetworkInput->tokenLength = 0;
            mNetworkInput->tokenBuffer = std::make_unique<int32_t[]>(static_cast<std::size_t>(mMaxTokenLength));
            mNetworkInput->positionIdBuffer =
                std::make_unique<int32_t[]>(static_cast<std::size_t>(3 * mMaxInputTokenLength));
            mNetworkInput->attentionMaskBuffer =
                std::make_unique<uint16_t[]>(static_cast<std::size_t>(mMaxTokenLength + 1));
        }

        void BMRuntimeQwen3ASRModel::CleanupAllNetworks() {
            // 优化：只清零输入内存，跳过输出内存
            // 输入内存的 padding 区域可能不被覆盖（如 hidden states、KV cache），需要清零
            // 输出内存会被 TPU 推理完全写入，无需清零
            bm_status_t ret {};
            int value = 0;
            auto cleanupInputs = [&](const bm_net_info_t* network) {
                for (int i = 0; i < network->input_num; i++) {
                    ret = ::bm_memset_device_ext(mHandle, &value, 1, network->stages[0].input_mems[i]);
                    if (ret != BM_SUCCESS) {
                        throw std::runtime_error {"Error occurred during network cleanup."};
                    }
                }
            };
            // cleanupInputs(mAudio);
            // cleanupInputs(mEmbed);
            // cleanupInputs(mEmbedCache);
            // cleanupInputs(mLmHead);
            for (uint16_t layerIndex = 0; layerIndex < mBlockerLayerSize; layerIndex++) {
                cleanupInputs(mLayers[layerIndex].block);
                cleanupInputs(mLayers[layerIndex].blockCache);
            }
        }

        void BMRuntimeQwen3ASRModel::Prefill() {
            const int32_t tokenLength = mNetworkInput->tokenLength;
            ComputeMropePositionIds(tokenLength);
            ForwardEmbedding(tokenLength);
            ForwardAudioSegments();
            BuildCausalAttentionMask(tokenLength);
            bm_device_mem_t statesMem = ForwardBlockLayers(tokenLength);
            ForwardLmHeadAndSample(tokenLength, statesMem);
        }

        void BMRuntimeQwen3ASRModel::ComputeMropePositionIds(int32_t tokenLength) {
            const uint16_t* attentionMask = mNetworkInput->attentionMaskBuffer.get();
            if (static_cast<int32_t>(mBasePositionIds.size()) < tokenLength) {
                mBasePositionIds.resize(static_cast<std::size_t>(tokenLength));
            }
            int32_t cumsum = 0;
            for (int32_t i = 0; i < tokenLength; i++) {
                if (attentionMask[static_cast<std::size_t>(i)] == 0) {
                    mBasePositionIds[static_cast<std::size_t>(i)] = cumsum++;
                } else {
                    mBasePositionIds[static_cast<std::size_t>(i)] = 1;  // masked_fill_ with 1
                }
            }
            int32_t* positionIds = mNetworkInput->positionIdBuffer.get();
            if (mIsDynamic) {
                for (int32_t dim = 0; dim < 3; dim++) {
                    std::copy(mBasePositionIds.begin(), mBasePositionIds.begin() + tokenLength,
                              positionIds + static_cast<std::size_t>(dim) * static_cast<std::size_t>(tokenLength));
                }
            } else {
                std::copy(mBasePositionIds.begin(), mBasePositionIds.begin() + tokenLength, positionIds);
                std::fill_n(positionIds + tokenLength, mMaxInputTokenLength - tokenLength, 0);
            }
        }

        void BMRuntimeQwen3ASRModel::ForwardEmbedding(int32_t tokenLength) {
            if (static_cast<int>(mEmbedInputIds.size()) < mMaxInputTokenLength) {
                mEmbedInputIds.resize(static_cast<std::size_t>(mMaxInputTokenLength), 0);
            } else {
                std::fill_n(mEmbedInputIds.begin() + tokenLength, mMaxInputTokenLength - tokenLength, 0);
            }
            std::copy(mNetworkInput->tokenBuffer.get(), mNetworkInput->tokenBuffer.get() + tokenLength,
                      mEmbedInputIds.begin());

            auto& embedInMem = mEmbed->stages[0].input_mems[0];
            auto& embedOutMem = mEmbed->stages[0].output_mems[0];

            bm_status_t ret = ::bm_memcpy_s2d(mHandle, embedInMem, static_cast<void*>(mEmbedInputIds.data()));
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Failed to copy token IDs to embedding network input."};
            }
            ForwardNetwork(mEmbed);

            auto& blockInputMem = mLayers[0].block->stages[0].input_mems[IDX_BLOCK_INPUT_STATES];
            std::size_t hiddenStatesCopyBytes = static_cast<std::size_t>(tokenLength) * mHiddenBytes;
            ret = ::bm_memcpy_d2d_byte(mHandle, blockInputMem, 0, embedOutMem, 0, hiddenStatesCopyBytes);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Failed to copy embedding output to block network input."};
            }
        }

        void BMRuntimeQwen3ASRModel::ForwardAudioSegments() {
            auto& audioFeatures = mNetworkInput->audioFeatures;
            auto& audioSegmentOffsets = mNetworkInput->audioSegmentOffsets;
            const std::size_t numSegments = audioSegmentOffsets.size();

            auto& audioInMem = mAudio->stages[0].input_mems[0];
            auto& audioOutMem = mAudio->stages[0].output_mems[0];
            auto audioInBytes = ::bm_mem_get_device_size(audioInMem);
            auto audioOutBytes = ::bm_mem_get_device_size(audioOutMem);
            auto audioInFloatsPerSegment = audioInBytes / sizeof(float);

            auto& blockInputMem = mLayers[0].block->stages[0].input_mems[IDX_BLOCK_INPUT_STATES];

            for (std::size_t i = 0; i < numSegments; i++) {
                std::size_t inOffset = i * audioInFloatsPerSegment;
                bm_status_t ret =
                    ::bm_memcpy_s2d(mHandle, audioInMem, static_cast<void*>(audioFeatures.data() + inOffset));
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Failed to copy audio features to audio network input."};
                }

                ForwardNetwork(mAudio);

                std::size_t dstOffset = audioSegmentOffsets[i] * mHiddenBytes;
                ret = ::bm_memcpy_d2d_byte(mHandle, blockInputMem, dstOffset, audioOutMem, 0, audioOutBytes);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Failed to copy audio output to hidden states."};
                }
            }
        }

        void BMRuntimeQwen3ASRModel::BuildCausalAttentionMask(int32_t tokenLength) {
            int32_t maskSize = mIsDynamic ? tokenLength : mMaxInputTokenLength;
            if (mCachedMaskTokenLength != tokenLength) {
                mCausalAttentionMask.assign(static_cast<std::size_t>(maskSize) * static_cast<std::size_t>(maskSize),
                                            mMaskValue);
                for (int32_t i = 0; i < tokenLength; i++) {
                    for (int32_t j = 0; j <= i; j++) {
                        mCausalAttentionMask[static_cast<std::size_t>(i) * static_cast<std::size_t>(maskSize) +
                                             static_cast<std::size_t>(j)] = 0;
                    }
                }
                mCachedMaskTokenLength = tokenLength;
            }
        }

        bm_device_mem_t BMRuntimeQwen3ASRModel::ForwardBlockLayers(int32_t tokenLength) {
            bm_device_mem_t currentStatesMem = mLayers[0].block->stages[0].input_mems[IDX_BLOCK_INPUT_STATES];

            for (uint16_t layerIndex = 0; layerIndex < mBlockerLayerSize; layerIndex++) {
                ForwardBlockLayerAndCache(layerIndex, tokenLength, currentStatesMem);
            }

            return currentStatesMem;
        }

        void BMRuntimeQwen3ASRModel::ForwardBlockLayerAndCache(uint16_t layerIndex, int32_t tokenLength,
                                                               bm_device_mem_t& currentStatesMem) {
            const bm_net_info_t* block = mLayers[layerIndex].block;
            int32_t* positionIds = mNetworkInput->positionIdBuffer.get();
            std::size_t hiddenStatesCopyBytes =
                mIsDynamic ? static_cast<std::size_t>(tokenLength) * mHiddenBytes
                           : ::bm_mem_get_device_size(mLayers[0].block->stages[0].input_mems[IDX_BLOCK_INPUT_STATES]);
            int32_t maskSize = mIsDynamic ? tokenLength : mMaxInputTokenLength;

            auto& inStatesMem = block->stages[0].input_mems[IDX_BLOCK_INPUT_STATES];
            auto& inPosMem = block->stages[0].input_mems[IDX_BLOCK_POSITION_IDS];
            auto& inAttnMem = block->stages[0].input_mems[IDX_BLOCK_ATTENTION_MASK];

            if (layerIndex > 0) {
                bm_status_t ret =
                    ::bm_memcpy_d2d_byte(mHandle, inStatesMem, 0, currentStatesMem, 0, hiddenStatesCopyBytes);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Failed to copy hidden states to block " + std::to_string(layerIndex) +
                                              " input."};
                }
            }

            if (layerIndex == 0) {
                int32_t posDataLen = mIsDynamic ? tokenLength : mMaxInputTokenLength;
                std::size_t posCopyBytes =
                    static_cast<std::size_t>(posDataLen) * (mIsDynamic ? 3 : 1) * sizeof(int32_t);
                std::size_t posDevBytes = ::bm_mem_get_device_size(inPosMem);
                SLOG_DEBUG << "[DEBUG] mIsDynamic=" << mIsDynamic << " mMaxInputTokenLength=" << mMaxInputTokenLength
                           << " tokenLength=" << tokenLength << " posDataLen=" << posDataLen
                           << " posCopyBytes=" << posCopyBytes << " posDevBytes=" << posDevBytes;
                bm_status_t ret = ::bm_memcpy_s2d_partial(mHandle, inPosMem, static_cast<void*>(positionIds),
                                                          static_cast<unsigned int>(posCopyBytes));
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Failed to copy position IDs to block 0 input."};
                }
                ret = ::bm_memcpy_s2d_partial(
                    mHandle, inAttnMem, static_cast<void*>(mCausalAttentionMask.data()),
                    static_cast<unsigned int>(static_cast<std::size_t>(maskSize) * static_cast<std::size_t>(maskSize) *
                                              sizeof(uint16_t)));
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Failed to copy attention mask to block 0 input."};
                }
            }

            ForwardNetwork(block);

            SaveKVCache(layerIndex, tokenLength);

            currentStatesMem = block->stages[0].output_mems[IDX_BLOCK_OUTPUT_STATES];
        }

        void BMRuntimeQwen3ASRModel::SaveKVCache(uint16_t layerIndex, int32_t tokenLength) {
            const bm_net_info_t* block = mLayers[layerIndex].block;
            const bm_net_info_t* blockCache = mLayers[layerIndex].blockCache;
            std::size_t kvCopyBytes = static_cast<std::size_t>(tokenLength) * mKVBytes;

            auto& blockKOut = block->stages[0].output_mems[IDX_BLOCK_K_CACHE];
            auto& blockVOut = block->stages[0].output_mems[IDX_BLOCK_V_CACHE];
            auto& cacheKIn = blockCache->stages[0].input_mems[IDX_BLOCK_HISTORY_K];
            auto& cacheVIn = blockCache->stages[0].input_mems[IDX_BLOCK_HISTORY_V];

            bm_status_t ret = ::bm_memcpy_d2d_byte(mHandle, cacheKIn, 0, blockKOut, 0, kvCopyBytes);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Failed to copy KV cache (K) for block " + std::to_string(layerIndex) + "."};
            }
            ret = ::bm_memcpy_d2d_byte(mHandle, cacheVIn, 0, blockVOut, 0, kvCopyBytes);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Failed to copy KV cache (V) for block " + std::to_string(layerIndex) + "."};
            }
        }

        void BMRuntimeQwen3ASRModel::ForwardLmHeadAndSample(int32_t tokenLength, bm_device_mem_t currentStatesMem) {
            auto& lmInMem = mLmHead->stages[0].input_mems[0];
            bm_status_t ret = ::bm_memcpy_d2d_byte(
                mHandle, lmInMem, 0, currentStatesMem,
                static_cast<std::size_t>(static_cast<uint32_t>(tokenLength - 1) * mHiddenBytes), mHiddenBytes);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Failed to copy hidden state to lm_head input."};
            }

            ForwardNetwork(mLmHead);

            int32_t firstToken = Sample();

            SLOG_DEBUG << "[Prefill] firstToken=" << firstToken;

            mNetworkInput->tokenBuffer[static_cast<std::size_t>(tokenLength)] = firstToken;
            mNetworkInput->tokenLength = tokenLength + 1;
        }

        void BMRuntimeQwen3ASRModel::Decode() {
            const int32_t tokenLength = mNetworkInput->tokenLength;
            int32_t currentToken = mNetworkInput->tokenBuffer[static_cast<std::size_t>(tokenLength - 1)];

            int32_t positionIds[3];
            PrepareDecodeInputs(tokenLength, currentToken, positionIds);

            bm_device_mem_t embedOutMem = ForwardEmbedCache(currentToken);

            bm_device_mem_t currentStatesMem = ForwardBlockCacheLayers(tokenLength, embedOutMem, positionIds);

            {
                auto& lmInMem = mLmHead->stages[0].input_mems[0];
                bm_status_t ret = ::bm_memcpy_d2d_byte(mHandle, lmInMem, 0, currentStatesMem, 0, mHiddenBytes);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Failed to copy hidden state to lm_head input."};
                }
                ForwardNetwork(mLmHead);
                int32_t nextToken = Sample();
                mNetworkInput->tokenBuffer[static_cast<std::size_t>(tokenLength)] = nextToken;
                mNetworkInput->tokenLength = tokenLength + 1;
                mNetworkInput->attentionMaskBuffer[static_cast<std::size_t>(tokenLength)] = 0;
            }
        }

        void BMRuntimeQwen3ASRModel::PrepareDecodeInputs(int32_t tokenLength, int32_t currentToken,
                                                         int32_t positionIds[3]) {
            (void)currentToken;
            const int32_t currentPosition = tokenLength - 1;
            positionIds[0] = currentPosition;
            positionIds[1] = currentPosition;
            positionIds[2] = currentPosition;

            int32_t attnMaskLen = mIsDynamic ? tokenLength : mMaxTokenLength;
            int32_t validKvLen = tokenLength - 1;
            if (!mDecodeMaskDeviceSynced) {
                if (static_cast<int>(mDecodeAttentionMask.size()) < attnMaskLen) {
                    mDecodeAttentionMask.resize(static_cast<std::size_t>(attnMaskLen));
                }
                std::fill_n(mDecodeAttentionMask.begin(), attnMaskLen, mMaskValue);
                std::fill_n(mDecodeAttentionMask.begin(), validKvLen, static_cast<uint16_t>(0));
            } else if (validKvLen > mDecodeMaskValidKvLen) {
                mDecodeAttentionMask[static_cast<std::size_t>(validKvLen - 1)] = 0;
            }
            mDecodeMaskValidKvLen = validKvLen;
        }

        bm_device_mem_t BMRuntimeQwen3ASRModel::ForwardEmbedCache(int32_t currentToken) {
            auto& embedCacheInMem = mEmbedCache->stages[0].input_mems[0];
            bm_status_t ret = ::bm_memcpy_s2d(mHandle, embedCacheInMem, static_cast<void*>(&currentToken));
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Failed to copy token ID to embedding cache network input."};
            }
            ForwardNetwork(mEmbedCache);
            return mEmbedCache->stages[0].output_mems[0];
        }

        bm_device_mem_t BMRuntimeQwen3ASRModel::ForwardBlockCacheLayers(int32_t tokenLength,
                                                                        bm_device_mem_t embedOutMem,
                                                                        const int32_t positionIds[3]) {
            bm_device_mem_t currentStatesMem = embedOutMem;
            for (uint16_t layerIndex = 0; layerIndex < mBlockerLayerSize; layerIndex++) {
                ForwardBlockCacheDecode(layerIndex, currentStatesMem, positionIds, tokenLength);
                currentStatesMem = mLayers[layerIndex].blockCache->stages[0].output_mems[IDX_BLOCK_OUTPUT_STATES];
            }
            mDecodeMaskDeviceSynced = true;
            return currentStatesMem;
        }

        void BMRuntimeQwen3ASRModel::ForwardNetwork(const bm_net_info_t* network) {
            // 复用成员变量避免每次调用都堆分配 bm_tensor_t 数组
            if (static_cast<int>(mForwardInputTensors.size()) < network->input_num) {
                mForwardInputTensors.resize(static_cast<std::size_t>(network->input_num));
            }
            if (static_cast<int>(mForwardOutputTensors.size()) < network->output_num) {
                mForwardOutputTensors.resize(static_cast<std::size_t>(network->output_num));
            }
            bm_tensor_t* inputTensors = mForwardInputTensors.data();
            bm_tensor_t* outputTensors = mForwardOutputTensors.data();

            for (int i = 0; i < network->input_num; i++) {
                ::bmrt_tensor_with_device(&(inputTensors[i]), network->stages[0].input_mems[i],
                                          network->input_dtypes[i], network->stages[0].input_shapes[i]);
            }
            for (int i = 0; i < network->output_num; i++) {
                ::bmrt_tensor_with_device(&(outputTensors[i]), network->stages[0].output_mems[i],
                                          network->output_dtypes[i], network->stages[0].output_shapes[i]);
            }

            bool ok = ::bmrt_launch_tensor_ex(mpBmrt, network->name, inputTensors, network->input_num, outputTensors,
                                              network->output_num, true, false);
            if (!ok) {
                throw std::runtime_error {"Error occurred when forward network " + std::string {network->name} + "."};
            }
        }

        void BMRuntimeQwen3ASRModel::ForwardBlockNetworkWithDynamicLength(uint16_t layerIndex, int32_t actualLength) {
            const bm_net_info_t* network = mLayers[layerIndex].block;

            if (static_cast<int>(mForwardInputTensors.size()) < network->input_num) {
                mForwardInputTensors.resize(static_cast<std::size_t>(network->input_num));
            }
            if (static_cast<int>(mForwardOutputTensors.size()) < network->output_num) {
                mForwardOutputTensors.resize(static_cast<std::size_t>(network->output_num));
            }
            bm_tensor_t* inputTensors = mForwardInputTensors.data();
            bm_tensor_t* outputTensors = mForwardOutputTensors.data();

            for (int i = 0; i < network->input_num; i++) {
                ::bmrt_tensor_with_device(&inputTensors[i], network->stages[0].input_mems[i], network->input_dtypes[i],
                                          network->stages[0].input_shapes[i]);
            }
            for (int i = 0; i < network->output_num; i++) {
                ::bmrt_tensor_with_device(&outputTensors[i], network->stages[0].output_mems[i],
                                          network->output_dtypes[i], network->stages[0].output_shapes[i]);
            }

            inputTensors[IDX_BLOCK_INPUT_STATES].shape.dims[1] = actualLength;
            inputTensors[IDX_BLOCK_POSITION_IDS].shape.dims[1] = actualLength;
            inputTensors[IDX_BLOCK_ATTENTION_MASK].shape.dims[2] = actualLength;
            inputTensors[IDX_BLOCK_ATTENTION_MASK].shape.dims[3] = actualLength;

            bool ok = ::bmrt_launch_tensor_ex(mpBmrt, network->name, inputTensors, network->input_num, outputTensors,
                                              network->output_num, true, false);
            if (!ok) {
                throw std::runtime_error {"Error occurred when forward block network " + std::string {network->name} +
                                          "."};
            }

            bm_status_t ret = ::bm_thread_sync(mHandle);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred when forward block network " + std::string {network->name} +
                                          "."};
            }
        }

        void BMRuntimeQwen3ASRModel::ForwardBlockCacheNetworkForDecode(uint16_t layerIndex, int32_t totalLength) {
            const bm_net_info_t* network = mLayers[layerIndex].blockCache;

            if (static_cast<int>(mForwardInputTensors.size()) < network->input_num) {
                mForwardInputTensors.resize(static_cast<std::size_t>(network->input_num));
            }
            if (static_cast<int>(mForwardOutputTensors.size()) < network->output_num) {
                mForwardOutputTensors.resize(static_cast<std::size_t>(network->output_num));
            }
            bm_tensor_t* inputTensors = mForwardInputTensors.data();
            bm_tensor_t* outputTensors = mForwardOutputTensors.data();

            for (int i = 0; i < network->input_num; i++) {
                ::bmrt_tensor_with_device(&inputTensors[i], network->stages[0].input_mems[i], network->input_dtypes[i],
                                          network->stages[0].input_shapes[i]);
            }
            for (int i = 0; i < network->output_num; i++) {
                ::bmrt_tensor_with_device(&outputTensors[i], network->stages[0].output_mems[i],
                                          network->output_dtypes[i], network->stages[0].output_shapes[i]);
            }

            // 设置动态维度 (对应 Python net_launch_block_cache_dyn)
            // input[0] states:        (1, 1, hidden)          →
            // 无动态维度 input[1] position_ids:  (3, 1) → 无动态维度
            // input[2] attention_mask:(1, 1, 1, seq_len)      → dims[3]
            // = totalLength input[3] history K:     (1, n_head, seq,
            // h_dim) → dims[2] = totalLength - 1 input[4] history V: (1,
            // n_head, seq, h_dim) → dims[2] = totalLength - 1
            inputTensors[IDX_BLOCK_ATTENTION_MASK].shape.dims[3] = totalLength;
            inputTensors[IDX_BLOCK_HISTORY_K].shape.dims[2] = totalLength - 1;
            inputTensors[IDX_BLOCK_HISTORY_V].shape.dims[2] = totalLength - 1;

            bool ok = ::bmrt_launch_tensor_ex(mpBmrt, network->name, inputTensors, network->input_num, outputTensors,
                                              network->output_num, true, false);
            if (!ok) {
                throw std::runtime_error {"Error occurred when forward block cache network " +
                                          std::string {network->name} + "."};
            }

            bm_status_t ret = ::bm_thread_sync(mHandle);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred when forward block cache network " +
                                          std::string {network->name} + "."};
            }
        }

        int32_t BMRuntimeQwen3ASRModel::Sample() {
            bm_status_t ret {};
            int32_t token {0};

            // lm_head 的输出只有一个token
            ret = ::bm_memcpy_d2s_partial_offset(mHandle, static_cast<void*>(&token), mLmHead->stages[0].output_mems[0],
                                                 sizeof(decltype(token)), 0);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred during sample."};
            }
            return token;
        }

        void BMRuntimeQwen3ASRModel::ForwardBlockCacheDecode(uint16_t layerIndex, bm_device_mem_t& inputStatesMem,
                                                             const int32_t* positionIds, int32_t tokenLength) {
            int32_t kvOffset = static_cast<int32_t>(static_cast<unsigned int>(tokenLength - 1) * mKVBytes);
            int32_t attnMaskLen = mIsDynamic ? tokenLength : mMaxTokenLength;
            int32_t validKvLen = tokenLength - 1;
            const bm_net_info_t* net = mLayers[layerIndex].blockCache;
            if (static_cast<int>(mForwardInputTensors.size()) < net->input_num)
                mForwardInputTensors.resize(static_cast<std::size_t>(net->input_num));
            if (static_cast<int>(mForwardOutputTensors.size()) < net->output_num)
                mForwardOutputTensors.resize(static_cast<std::size_t>(net->output_num));
            bm_tensor_t* inTensors = mForwardInputTensors.data();
            ::bmrt_tensor_with_device(&inTensors[IDX_BLOCK_INPUT_STATES], inputStatesMem, net->input_dtypes[0],
                                      net->stages[0].input_shapes[0]);
            if (layerIndex == 0) {
                PrepareBlockCacheInputsLayer0(layerIndex, positionIds, attnMaskLen, validKvLen);
                ::bmrt_tensor_with_device(&inTensors[IDX_BLOCK_POSITION_IDS],
                                          net->stages[0].input_mems[IDX_BLOCK_POSITION_IDS], net->input_dtypes[1],
                                          net->stages[0].input_shapes[1]);
                ::bmrt_tensor_with_device(&inTensors[IDX_BLOCK_ATTENTION_MASK],
                                          net->stages[0].input_mems[IDX_BLOCK_ATTENTION_MASK], net->input_dtypes[2],
                                          net->stages[0].input_shapes[2]);
            } else {
                ::bmrt_tensor_with_device(&inTensors[IDX_BLOCK_POSITION_IDS],
                                          mLayers[0].blockCache->stages[0].input_mems[IDX_BLOCK_POSITION_IDS],
                                          net->input_dtypes[1], net->stages[0].input_shapes[1]);
                ::bmrt_tensor_with_device(&inTensors[IDX_BLOCK_ATTENTION_MASK],
                                          mLayers[0].blockCache->stages[0].input_mems[IDX_BLOCK_ATTENTION_MASK],
                                          net->input_dtypes[2], net->stages[0].input_shapes[2]);
            }

            ::bmrt_tensor_with_device(&inTensors[IDX_BLOCK_HISTORY_K], net->stages[0].input_mems[IDX_BLOCK_HISTORY_K],
                                      net->input_dtypes[3], net->stages[0].input_shapes[3]);
            ::bmrt_tensor_with_device(&inTensors[IDX_BLOCK_HISTORY_V], net->stages[0].input_mems[IDX_BLOCK_HISTORY_V],
                                      net->input_dtypes[4], net->stages[0].input_shapes[4]);
            if (mIsDynamic) {
                int32_t totalLength = validKvLen + 1;
                inTensors[IDX_BLOCK_ATTENTION_MASK].shape.dims[3] = totalLength;
                inTensors[IDX_BLOCK_HISTORY_K].shape.dims[2] = totalLength - 1;
                inTensors[IDX_BLOCK_HISTORY_V].shape.dims[2] = totalLength - 1;
            }

            SetupBlockCacheOutputs(layerIndex, kvOffset);
            bm_tensor_t* outTensors = mForwardOutputTensors.data();
            bool ok = ::bmrt_launch_tensor_ex(mpBmrt, net->name, inTensors, net->input_num, outTensors, net->output_num,
                                              true, false);
            if (!ok)
                throw std::runtime_error {"Error occurred when forward block cache decode " +
                                          std::to_string(layerIndex) + "."};
        }

        void BMRuntimeQwen3ASRModel::PrepareBlockCacheInputsLayer0(uint16_t layerIndex, const int32_t positionIds[3],
                                                                   int32_t attnMaskLen, int32_t validKvLen) {
            const bm_net_info_t* net = mLayers[layerIndex].blockCache;
            auto& inPosMem = net->stages[0].input_mems[IDX_BLOCK_POSITION_IDS];
            auto& inAttnMem = net->stages[0].input_mems[IDX_BLOCK_ATTENTION_MASK];

            std::size_t posCopyBytes = (mIsDynamic ? 3 : 1) * sizeof(int32_t);
            bm_status_t ret =
                ::bm_memcpy_s2d_partial(mHandle, inPosMem, const_cast<void*>(static_cast<const void*>(positionIds)),
                                        static_cast<unsigned int>(posCopyBytes));
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Failed to copy position IDs to block cache 0 input."};
            }

            if (mDecodeMaskDeviceSynced) {
                uint16_t zeroVal = 0;
                ret = ::bm_memcpy_s2d_partial_offset(
                    mHandle, inAttnMem, &zeroVal, sizeof(uint16_t),
                    static_cast<unsigned int>(static_cast<unsigned long>(validKvLen - 1) * sizeof(uint16_t)));
            } else {
                ret = ::bm_memcpy_s2d_partial(
                    mHandle, inAttnMem, const_cast<void*>(static_cast<const void*>(mDecodeAttentionMask.data())),
                    static_cast<unsigned int>(static_cast<unsigned long>(attnMaskLen) * sizeof(uint16_t)));
            }
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Failed to copy attention mask to block cache 0 input."};
            }
        }

        void BMRuntimeQwen3ASRModel::SetupBlockCacheOutputs(uint16_t layerIndex, int32_t kvOffset) {
            const bm_net_info_t* net = mLayers[layerIndex].blockCache;
            bm_tensor_t* outTensors = mForwardOutputTensors.data();

            ::bmrt_tensor_with_device(&outTensors[IDX_BLOCK_OUTPUT_STATES],
                                      net->stages[0].output_mems[IDX_BLOCK_OUTPUT_STATES], net->output_dtypes[0],
                                      net->stages[0].output_shapes[0]);

            auto& cacheKIn = net->stages[0].input_mems[IDX_BLOCK_HISTORY_K];
            auto& cacheVIn = net->stages[0].input_mems[IDX_BLOCK_HISTORY_V];
            bm_device_mem_t kOutMem =
                ::bm_mem_from_device(cacheKIn.u.device.device_addr + static_cast<unsigned long>(kvOffset), mKVBytes);
            bm_device_mem_t vOutMem =
                ::bm_mem_from_device(cacheVIn.u.device.device_addr + static_cast<unsigned long>(kvOffset), mKVBytes);
            ::bmrt_tensor_with_device(&outTensors[IDX_BLOCK_K_CACHE], kOutMem, net->output_dtypes[1],
                                      net->stages[0].output_shapes[1]);
            ::bmrt_tensor_with_device(&outTensors[IDX_BLOCK_V_CACHE], vOutMem, net->output_dtypes[2],
                                      net->stages[0].output_shapes[2]);
        }

        std::optional<std::string>
        BMRuntimeQwen3ASRModel::SetupNetworkInput(const std::string& input,
                                                  std::unique_ptr<std::vector<float>> pcmData) {
            constexpr std::string_view audioBosTag {"<|audio_start|>"};
            std::size_t bosEosPos = input.find("<|audio_start|><|audio_end|>");
            if (bosEosPos == std::string::npos) {
                return "prompt is illegal.";
            }
            std::size_t audioBosEndPos = bosEosPos + audioBosTag.size();

            mNetworkInput->audioFeatures = ExtractFbankFeatures(std::move(pcmData));
            std::size_t segmentSize =
                mNetworkInput->audioFeatures.size() / (mAudioSegmentFeatureSize * mAudioSegmentFrameLength);
            std::string formattedInput =
                BuildFormattedInput(input, audioBosEndPos, segmentSize, mAudioSegmentTokenLength);

            TokenizerEncodeResult* result {nullptr};
            try {
                result = ::tokenizer_encode(mTokenizer, formattedInput.c_str(), true);
                if (result == nullptr) {
                    const char* err = ::tokenizer_get_last_error_in_handle(mTokenizer);
                    return err == nullptr ? "Tokenizer encode failed."
                                          : "Tokenizer encode failed, reason = " + std::string {err};
                }
                if (result->len == 0) {
                    ::free_tokenizer_encode_result(result);
                    return "Input is empty.";
                }
                if (static_cast<int32_t>(result->len) >= mMaxInputTokenLength) {
                    ::free_tokenizer_encode_result(result);
                    return "Input is too long.";
                }
                if (!FindAudioSegmentOffsets(reinterpret_cast<const int32_t*>(result->token_ids),
                                             static_cast<int32_t>(result->len), segmentSize,
                                             mNetworkInput->audioSegmentOffsets)) {
                    ::free_tokenizer_encode_result(result);
                    return "Encode result of tokenizer is illegal.";
                }
                FillNetworkInputBuffers(reinterpret_cast<const int32_t*>(result->token_ids),
                                        static_cast<int32_t>(result->len));
                ::free_tokenizer_encode_result(result);
                return std::nullopt;
            } catch (...) {
                ::free_tokenizer_encode_result(result);
                throw;
            }
        }

        bool BMRuntimeQwen3ASRModel::FindAudioSegmentOffsets(const int32_t* tokenIds, int32_t len,
                                                             std::size_t segmentSize,
                                                             std::vector<std::size_t>& offsets) {
            std::size_t audioBosIndex {};
            std::size_t audioEosIndex {};
            if (auto it = std::find(tokenIds, tokenIds + len, mAudioBosToken); it != tokenIds + len) {
                audioBosIndex = static_cast<std::size_t>(it - tokenIds);
            } else {
                return false;
            }
            if (auto it = std::find(tokenIds, tokenIds + len, mAudioEosToken); it != tokenIds + len) {
                audioEosIndex = static_cast<std::size_t>(it - tokenIds);
            } else {
                return false;
            }
            if (audioEosIndex <= audioBosIndex) {
                return false;
            }
            std::size_t audioPadTokenSize = audioEosIndex - 1 - audioBosIndex;
            if (audioPadTokenSize % mAudioSegmentTokenLength != 0 ||
                audioPadTokenSize / mAudioSegmentTokenLength != segmentSize) {
                return false;
            }

            offsets.clear();
            offsets.reserve(segmentSize);
            for (std::size_t i = 0; i < segmentSize; i++) {
                offsets.push_back(audioBosIndex + 1 + static_cast<std::size_t>(mAudioSegmentTokenLength) * i);
            }
            return true;
        }

        void BMRuntimeQwen3ASRModel::FillNetworkInputBuffers(const int32_t* tokenIds, int32_t len) {
            auto& netInput = *mNetworkInput;
            netInput.tokenLength = len;
            std::copy(reinterpret_cast<const int32_t*>(tokenIds), reinterpret_cast<const int32_t*>(tokenIds + len),
                      netInput.tokenBuffer.get());
            std::fill_n(netInput.tokenBuffer.get() + len, static_cast<std::size_t>(mMaxTokenLength - len), 0);

            std::fill_n(netInput.positionIdBuffer.get(), static_cast<std::size_t>(3 * mMaxInputTokenLength), 0);

            std::fill_n(netInput.attentionMaskBuffer.get(), static_cast<std::size_t>(len), 0);
            netInput.attentionMaskBuffer[static_cast<std::size_t>(mMaxTokenLength)] = 0;
            std::fill_n(netInput.attentionMaskBuffer.get() + len, static_cast<std::size_t>(mMaxTokenLength - len),
                        mMaskValue);
        }

        std::vector<float> BMRuntimeQwen3ASRModel::ExtractFbankFeatures(std::unique_ptr<std::vector<float>> pcmData) {
            int64_t maxAudioSeconds = static_cast<int64_t>(std::max(0, mMaxInputTokenLength - 64)) /
                                      static_cast<int64_t>(mAudioSegmentTokenLength);
            AlignPcmToSegment(pcmData, maxAudioSeconds, SAMPLE_RATE);
            auto result = ComputeMelSpectrogram(pcmData->data(), static_cast<int32_t>(pcmData->size()),
                                                mAudioSegmentFeatureSize, mAudioSegmentFrameLength, SAMPLE_RATE);
            PostProcessFbankFeatures(result);
            return result;
        }

        std::shared_ptr<Model> CreateQwen3ASRBmruntimeModel(bm_handle_t handle, void* bmrt,
                                                            const std::string& tokenizerPath,
                                                            const std::string& configPath,
                                                            const std::string& modelPath) {
            return std::make_shared<BMRuntimeQwen3ASRModel>(handle, bmrt, tokenizerPath, configPath, modelPath);
        }

    }  // namespace asr

}  // namespace qifeng