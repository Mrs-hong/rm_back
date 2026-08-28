/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "models_hm/worker/asr_worker.h"
#include "models_hm/worker/hotwords_worker.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <sstream>
#include <vector>

#include "NumCpp.hpp"
#include "NumCpp/Functions/argmax.hpp"
#include "NumCpp/NdArray/NdArrayCore.hpp"
#include "onnxruntime_c_api.h"
#include "onnxruntime_cxx_api.h"

#include "common/config_manager.h"
#include "common/logger.h"

namespace qifeng {

    ASRWorker::ASRWorker(const std::string& encoderPath, const std::string& decoderPath,
                         const std::string& predictorPath, int tpuId, IOMode ioMode)
        : ModelWorker(encoderPath, tpuId, ioMode), mHotwordsEnabled(false), mTokenizerLoaded(false), mBlankId(0),
          mSosId(1), mEosId(2), mOnnxInitialized(false) {
        try {
            SLOG_INFO << "ASRWorker: Constructor called, encoder=" << encoderPath << " decoder=" << decoderPath
                      << " predictor=" << predictorPath;

            // 基类构造函数已将 encoder 加载为默认图（图名=文件名）
            // 注册 "encoder" 别名指向同一 Module，避免重复加载
            mModules["encoder"] = mModule;
            mGraphInputNames["encoder"] = mGraphInputNames[mDefaultGraphName];
            mGraphOutputNames["encoder"] = mGraphOutputNames[mDefaultGraphName];
            mGraphNames.push_back("encoder");

            // 加载 decoder.hmm，注册为 "decoder" 图
            if (!LoadGraph("decoder", decoderPath)) {
                SLOG_ERROR << "ASRWorker: Failed to load decoder model: " << decoderPath;
            }
            // "decoder_output" 复用 decoder 模块（decoder.hmm 内含投影层）
            mModules["decoder_output"] = mModules["decoder"];
            mGraphInputNames["decoder_output"] = mGraphInputNames["decoder"];
            mGraphOutputNames["decoder_output"] = mGraphOutputNames["decoder"];
            mGraphNames.push_back("decoder_output");

            // 加载令牌转换器
            LoadTokenizer();

            // 初始化节点名称
            mInNameSpeech = "speech";
            mInNameSpeechLengths = "speech_lengths";

            for (int i = 0; i < 4; ++i) {
                mInNamesCache.push_back("in_cache" + std::to_string(i));
            }

            // 初始化ONNX Runtime
            if (!InitOnnxRuntime(predictorPath)) {
                SLOG_ERROR << "ASRWorker: Failed to initialize ONNX Runtime";
            }
        } catch (const std::exception& e) {
            SLOG_ERROR << "ASRWorker: Exception in constructor: " << e.what();
        } catch (...) {
            SLOG_ERROR << "ASRWorker: Unknown exception in constructor";
        }
    }

    ASRWorker::~ASRWorker() {
        try {
            SLOG_INFO << "ASRWorker: Destructor called";
            CleanupOnnxResources();
        } catch (const std::exception& e) {
            SLOG_ERROR << "ASRWorker: Exception in destructor: " << e.what();
        } catch (...) {
            SLOG_ERROR << "ASRWorker: Unknown exception in destructor";
        }
    }

    void log_softmax_3d(const float* input, float* output, int batchSize, int seqLen, int vocabSize, int axis = -1) {
        // 只支持在最后一个维度(axis=-1或axis=2)应用log_softmax
        if (axis != -1 && axis != 2) {
            throw std::invalid_argument("Only axis=-1 or axis=2 is supported");
        }

        int totalTokens = batchSize * seqLen;

        for (int i = 0; i < totalTokens; ++i) {
            // 找到当前token在vocab上的最大值（数值稳定性）
            float maxVal = -std::numeric_limits<float>::infinity();
            for (int j = 0; j < vocabSize; ++j) {
                float val = input[i * vocabSize + j];
                if (val > maxVal) {
                    maxVal = val;
                }
            }

            // 计算 exp(x - max) 的和
            float sumExp = 0.0f;
            for (int j = 0; j < vocabSize; ++j) {
                float expVal = std::exp(input[i * vocabSize + j] - maxVal);
                sumExp += expVal;
                // 可以在这里缓存 expVal 用于后续计算，避免重复计算
                output[i * vocabSize + j] = input[i * vocabSize + j] - maxVal;
            }

            // 计算 log_softmax: x - max - log(sum_exp)
            float logSumExp = std::log(sumExp);
            for (int j = 0; j < vocabSize; ++j) {
                output[i * vocabSize + j] -= logSumExp;
            }
        }
    }

    std::vector<std::vector<float>> slice_decoder_output(const std::vector<float>& decoderOut, int timeSteps,
                                                         int vocabSize, int preTokenLength) {
        int validLength = std::min(preTokenLength, timeSteps);
        std::vector<std::vector<float>> sliced;
        sliced.reserve(validLength * vocabSize);

        // decoder_out 布局: [batch_size][time_steps][vocab_size] = [1][time_steps][vocab_size]
        // 我们只需要前 valid_length 个时间步
        for (int t = 0; t < validLength; ++t) {
            std::vector<float> tmp;
            for (int v = 0; v < vocabSize; ++v) {
                // 展平索引: batch_idx * time_steps * vocab_size + time_idx * vocab_size + vocab_idx
                // 这里 batch_idx 为 0，所以简化为 t * vocab_size + v
                tmp.push_back(decoderOut[t * vocabSize + v]);
            }
            sliced.push_back(tmp);
        }

        return sliced;
    }

    std::vector<float> asr_argmax(const std::vector<std::vector<float>>& amScores) {
        std::vector<float> res;
        for (auto score : amScores) {
            auto yseq = nc::argmax<float>(score, nc::Axis::COL);
            res.push_back(yseq[0]);
        }
        return res;
    }

    std::string ASRWorker::Inference(const std::vector<std::vector<float>>& speechFeat,
                                     HotwordsWorker* hotwordsWorker) {
        SLOG_INFO << "ASRWorker start inference";
        if (!IsReady()) {
            throw std::runtime_error("ASR model not ready");
        }

        if (speechFeat.empty() || speechFeat[0].empty()) {
            throw std::runtime_error("Speech data is empty");
        }
        try {
            // 计算输入数据的形状
            size_t timeSteps = speechFeat.size();
            size_t featureDim = speechFeat[0].size();

            // 查询 encoder 模型对 speech 的期望 shape（静态 shape，如 [1, 334, 560]）
            tcim::TensorInfo speechInfo = GetInputInfo("encoder", "speech");
            int modelTimeSteps = static_cast<int>(timeSteps);
            int modelFeatureDim = static_cast<int>(featureDim);
            if (speechInfo.Shape().size() >= 3) {
                int64_t dim1 = speechInfo.Shape()[1];
                int64_t dim2 = speechInfo.Shape()[2];
                if (dim1 > 0)
                    modelTimeSteps = static_cast<int>(dim1);
                if (dim2 > 0)
                    modelFeatureDim = static_cast<int>(dim2);
            }

            // 启用调试追踪（根据配置是否保存 encoder/decoder/predictor 的 npy）
            // InitTraceSaving();  // 测试用，已注释

            // speech 展开成一维并 pad 到 [1, modelTimeSteps, modelFeatureDim]
            std::vector<float> speechFlat(1 * modelTimeSteps * modelFeatureDim, 0.0f);
            for (size_t t = 0; t < timeSteps && t < static_cast<size_t>(modelTimeSteps); ++t) {
                for (size_t d = 0; d < featureDim && d < static_cast<size_t>(modelFeatureDim); ++d) {
                    speechFlat[t * modelFeatureDim + d] = speechFeat[t][d];
                }
            }

            // ===== 1. encoder =====
            ModelInput encInput;
            ModelOutput encOutput;

            // speech: [1, modelTimeSteps, modelFeatureDim]
            encInput.data["speech"] = speechFlat.data();
            encInput.shapes["speech"] = {1, modelTimeSteps, modelFeatureDim};

            // speech_mask: [1, modelTimeSteps]（模型期望 2D），前 timeSteps 为 1，其余为 0
            std::vector<float> speechMask(1 * modelTimeSteps, 0.0f);
            for (size_t i = 0; i < timeSteps && i < static_cast<size_t>(modelTimeSteps); ++i) {
                speechMask[i] = 1.0f;
            }
            encInput.data["speech_mask"] = speechMask.data();
            encInput.shapes["speech_mask"] = {1, modelTimeSteps};

            // 准备 encoder 输出缓冲区（用较大容量，Process 会回写实际 shape）
            const int kEncDim = 512;
            const int kMaxEncLen = 2048;
            std::vector<float> encOut(kMaxEncLen * kEncDim, 0.0f);
            float encLensVal = 0.0f;
            encOutput.data["enc"] = encOut.data();
            encOutput.shapes["enc"] = {1, modelTimeSteps, kEncDim};
            encOutput.data["enc_lens"] = &encLensVal;
            encOutput.shapes["enc_lens"] = {1};

            int encRet = Process("encoder", encInput, encOutput);
            if (encRet != ERR_OK) {
                SLOG_ERROR << "ASRWorker: Encoder inference failed with code: " << encRet;
                return "";
            }

            // 从回写 shape 读取实际 enc_len（模型输出的固定时间维）
            auto encShapeIt = encOutput.shapes.find("enc");
            int encLen = static_cast<int>(timeSteps);
            if (encShapeIt != encOutput.shapes.end() && encShapeIt->second.size() >= 2) {
                encLen = encShapeIt->second[1];
            }
            // 实际有效 encoder 输出长度（由模型计算，通常 = ceil(time_steps/2)）
            int encValidLen = static_cast<int>(std::round(encLensVal));

            // ===== 2. predictor (ONNX) =====
            // predictor 接受动态长度，传入有效部分 enc（裁剪到 encValidLen，避免 padding 干扰）
            int predictorEncLen = std::min(encValidLen, encLen);
            std::vector<float> encActual(encOut.begin(), encOut.begin() + predictorEncLen * kEncDim);
            // enc_mask: [1, 1, predictorEncLen]，全部为 1
            std::vector<float> encMask(1 * 1 * predictorEncLen, 1.0f);

            InferenceResult predictorResult =
                RunOnnxInferencePersistent(encActual, {1, predictorEncLen, kEncDim}, encMask, {1, 1, predictorEncLen});
            if (!predictorResult.success) {
                SLOG_ERROR << "ASRWorker: Predictor inference failed: " << predictorResult.message;
                return "";
            }

            std::vector<float> preAcousticEmbeds = predictorResult.predictLogits;
            if (predictorResult.tokenNum.empty()) {
                SLOG_ERROR << "ASRWorker: predictor did not output token_num/pre_token_length";
                return "";
            }
            int preTokenLength = static_cast<int>(std::round(predictorResult.tokenNum[0]));
            if (preTokenLength <= 0) {
                SLOG_WARN << "ASRWorker: No tokens detected (pre_token_length=" << preTokenLength << ")";
                return "";
            }

            // predictor 输出 shape 通常是 [1, pre_token_length, 512]
            int preAcousticDim = 512;
            if (!predictorResult.predictLogitsShape.empty()) {
                int computed = 1;
                for (size_t i = 2; i < predictorResult.predictLogitsShape.size(); ++i) {
                    computed *= static_cast<int>(predictorResult.predictLogitsShape[i]);
                }
                if (computed > 0) {
                    preAcousticDim = computed;
                }
            }

            // ===== 3. decoder =====
            // decoder 输入: enc, enc_mask, pre_acoustic_embeds, pre_token_mask
            // 模型 pre_acoustic_embeds/pre_token_mask 的时间维可能是固定的 max_token_len
            // 查询 decoder 模型对 pre_acoustic_embeds 的期望 shape
            tcim::TensorInfo preAcousticInfo = GetInputInfo("decoder", "pre_acoustic_embeds");
            int maxTokenLen = preTokenLength;
            if (!preAcousticInfo.Shape().empty()) {
                // 第 1 维（index 1）是 token 维度
                int64_t dim1 = preAcousticInfo.Shape()[1];
                if (dim1 > 0) {
                    maxTokenLen = static_cast<int>(dim1);
                }
            }

            // pad_or_trim pre_acoustic_embeds 到 maxTokenLen
            std::vector<float> preAcousticPadded(1 * maxTokenLen * preAcousticDim, 0.0f);
            int copyTokens = std::min(preTokenLength, maxTokenLen);
            for (int t = 0; t < copyTokens; ++t) {
                for (int d = 0; d < preAcousticDim; ++d) {
                    preAcousticPadded[t * preAcousticDim + d] = preAcousticEmbeds[t * preAcousticDim + d];
                }
            }

            // pre_token_mask: [1, max_token_len]，前 copyTokens 个为 1
            std::vector<float> preTokenMask(1 * maxTokenLen, 0.0f);
            for (int t = 0; t < copyTokens; ++t) {
                preTokenMask[t] = 1.0f;
            }

            // decoder 模型为静态 shape，期望 enc=[1, encLen, 512], enc_mask=[1, 1, encLen]
            // 使用完整的 enc 输出（包含 padding），mask 仅前 encValidLen 为 1
            std::vector<float> decEncFull(encOut.begin(), encOut.begin() + encLen * kEncDim);
            std::vector<float> decEncMask(1 * 1 * encLen, 0.0f);
            for (int i = 0; i < encValidLen && i < encLen; ++i) {
                decEncMask[i] = 1.0f;
            }

            ModelInput decInput;
            ModelOutput decOutput;

            decInput.data["enc"] = decEncFull.data();
            decInput.shapes["enc"] = {1, encLen, kEncDim};

            decInput.data["enc_mask"] = decEncMask.data();
            decInput.shapes["enc_mask"] = {1, 1, encLen};

            decInput.data["pre_acoustic_embeds"] = preAcousticPadded.data();
            decInput.shapes["pre_acoustic_embeds"] = {1, maxTokenLen, preAcousticDim};

            decInput.data["pre_token_mask"] = preTokenMask.data();
            decInput.shapes["pre_token_mask"] = {1, maxTokenLen};

            // 准备 decoder 输出: decoder_out [1, max_token_len, vocab_size]
            int vocabSize = static_cast<int>(mTokenizer.size());
            if (vocabSize <= 0) {
                SLOG_ERROR << "ASRWorker: tokenizer not loaded, vocabSize=0";
                return "";
            }
            std::vector<float> decoderOut(1 * maxTokenLen * vocabSize, 0.0f);
            decOutput.data["decoder_out"] = decoderOut.data();
            decOutput.shapes["decoder_out"] = {1, maxTokenLen, vocabSize};

            int decRet = Process("decoder", decInput, decOutput);
            if (decRet != ERR_OK) {
                SLOG_ERROR << "ASRWorker: Decoder inference failed with code: " << decRet;
                return "";
            }

            // 读取实际 decoder_out shape（时间维可能被模型截断）
            auto decShapeIt = decOutput.shapes.find("decoder_out");
            int decTimeSteps = maxTokenLen;
            if (decShapeIt != decOutput.shapes.end() && decShapeIt->second.size() >= 2) {
                decTimeSteps = decShapeIt->second[1];
            }

            // log_softmax
            log_softmax_3d(decoderOut.data(), decoderOut.data(), 1, decTimeSteps, vocabSize);

            if (hotwordsWorker != nullptr) {
                SLOG_DEBUG << "ASRWorker: Hotwords worker provided, but hotword processing not implemented yet";
            }

            // 取前 preTokenLength 个时间步做 argmax
            std::vector<std::vector<float>> amScores =
                slice_decoder_output(decoderOut, decTimeSteps, vocabSize, copyTokens);
            auto yseq = asr_argmax(amScores);

            std::vector<int> tokenInt;
            for (auto token : yseq) {
                if (token == 0 || token == 1 || token == 2) {
                    continue;
                }
                tokenInt.push_back(static_cast<int>(token));
            }

            // 整数ID -> token字符串列表(对齐FunASR ids2tokens)
            std::vector<std::string> tokens = TokensToText(tokenInt);

            // 后处理: 处理@@子词连接和空格分隔(对齐FunASR sentence_postprocess)
            return SentencePostprocess(tokens);
        } catch (const std::exception& e) {
            SLOG_ERROR << "ASRWorker: Exception during inference: " << e.what();
            throw;
        } catch (...) {
            SLOG_ERROR << "ASRWorker: Unknown exception during inference";
            throw std::runtime_error("Unknown exception during inference");
        }
    }

    void ASRWorker::LoadTokenizer() {
        try {
            // 从配置文件获取tokenizer路径
            ConfigManager& configMgr = ConfigManager::GetInstance();
            std::string tokenizerPath = configMgr.GetString(
                "models.asr", "tokenizer",
                "/data/aas/model/speech_seaco_paraformer_large_asr_nat-zh-cn-16k-common-vocab8404-0115/tokens.json");

            // 打开tokenizer文件
            std::ifstream tokenizerFile(tokenizerPath);
            if (!tokenizerFile.is_open()) {
                SLOG_WARN << "ASRWorker: Failed to open tokenizer file, using default mapping";
                // 使用默认映射（仅用于测试）
                mTokenizer[0] = "<blank>";
                mTokenizer[1] = "<sos>";
                mTokenizer[2] = "<eos>";
                mTokenToId["<blank>"] = 0;
                mTokenToId["<sos>"] = 1;
                mTokenToId["<eos>"] = 2;
                for (int i = 3; i < 100; ++i) {
                    mTokenizer[i] = std::string(1, 'a' + (i - 3) % 26);
                    mTokenToId[mTokenizer[i]] = i;
                }
                mTokenizerLoaded = true;
                return;
            }

            // 读取tokenizer文件(FunASR tokens.json为JSON数组, 逐行解析)
            std::string line;
            int id = 0;
            while (std::getline(tokenizerFile, line)) {
                // 跳过空行和JSON数组边界
                if (line.empty() || line[0] == '#' || line[0] == ']' || line[0] == '[') {
                    continue;
                }

                // 仅trim行首尾空白, 移除JSON的引号和逗号, 保留token内部内容(含▁词边界)
                auto firstNotSpace = line.find_first_not_of(" \t\n\r");
                if (firstNotSpace == std::string::npos) {
                    continue;
                }
                auto lastNotSpace = line.find_last_not_of(" \t\n\r");
                line = line.substr(firstNotSpace, lastNotSpace - firstNotSpace + 1);
                line.erase(std::remove(line.begin(), line.end(), '"'), line.end());
                line.erase(std::remove(line.begin(), line.end(), ','), line.end());
                // 再次trim, 去掉移除引号逗号后可能暴露的首尾空格
                firstNotSpace = line.find_first_not_of(" \t");
                if (firstNotSpace == std::string::npos) {
                    continue;
                }
                lastNotSpace = line.find_last_not_of(" \t");
                line = line.substr(firstNotSpace, lastNotSpace - firstNotSpace + 1);

                if (line.empty()) {
                    continue;
                }

                // 存储token映射
                mTokenizer[id] = line;
                mTokenToId[line] = id;
                id++;
            }

            tokenizerFile.close();
            mTokenizerLoaded = true;
        } catch (const std::exception& e) {
            SLOG_ERROR << "ASRWorker: Exception during tokenizer loading: " << e.what();
            mTokenizerLoaded = false;
        }
    }

    std::vector<std::string> ASRWorker::TokensToText(const std::vector<int>& tokens) {
        // 整数ID -> token字符串列表, 对齐FunASR BaseTokenizer.ids2tokens
        std::vector<std::string> tokenList;
        tokenList.reserve(tokens.size());
        for (int token : tokens) {
            auto it = mTokenizer.find(token);
            if (it != mTokenizer.end()) {
                tokenList.push_back(it->second);
            } else {
                tokenList.push_back("<unk>");
            }
        }
        return tokenList;
    }

    // 去除token中所有空白字符, 用于后续类型判定
    static std::string StripSpaces(std::string s) {
        s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
        return s;
    }

    // 判断单个字符是否为中文/数字/ '@' (对齐 nlp.isChinese)
    static bool IsChineseChar(const std::string& s) {
        if (s.empty() || s.size() > 3) {
            return false;
        }
        // ASCII数字或'@'
        if (s.size() == 1) {
            unsigned char c = static_cast<unsigned char>(s[0]);
            return (c >= '0' && c <= '9') || c == '@';
        }
        // UTF-8中文(常用汉字区U+4E00~U+9FFF: 首字节E4~E9)
        unsigned char c0 = static_cast<unsigned char>(s[0]);
        return c0 >= 0xE4 && c0 <= 0xE9;
    }

    // 判断字符是否为英文字母或撇号(对齐 nlp.isAllAlpha 的字符级判断)
    static bool IsAsciiAlphaChar(const std::string& s) {
        for (unsigned char c : s) {
            bool alphaOrApostrophe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'';
            // 中文UTF-8首字节范围视为非英文
            if (!alphaOrApostrophe || (c >= 0xE4 && c <= 0xE9)) {
                return false;
            }
        }
        return !s.empty();
    }

    // 去除子词连接符@@并累加到wordItem
    static void AppendSubword(std::string& wordItem, const std::string& ch) {
        std::string s = ch;
        size_t pos = 0;
        while ((pos = s.find("@@", pos)) != std::string::npos) {
            s.erase(pos, 2);
        }
        wordItem += s;
    }

    // 完成一个英文单词的累积: 入wordLists并追加空格分隔
    static void FlushWord(std::vector<std::string>& wordLists, std::string& wordItem) {
        wordLists.push_back(wordItem);
        wordLists.emplace_back(" ");
        wordItem.clear();
    }

    // 把已累积的wordLists拼接成句子, 并规整首尾与连续空格
    static std::string BuildSentence(const std::vector<std::string>& wordLists) {
        std::string sentence;
        sentence.reserve(wordLists.size() * 4);
        for (const auto& w : wordLists) {
            sentence += w;
        }
        size_t start = sentence.find_first_not_of(" ");
        if (start != std::string::npos) {
            sentence = sentence.substr(start);
        }
        size_t end = sentence.find_last_not_of(" ");
        if (end != std::string::npos) {
            sentence = sentence.substr(0, end + 1);
        }
        // 合并连续多空格为单空格
        size_t pos = 0;
        while ((pos = sentence.find("  ", pos)) != std::string::npos) {
            sentence.replace(pos, 2, " ");
            pos += 1;
        }
        return sentence;
    }

    std::string ASRWorker::SentencePostprocess(const std::vector<std::string>& words) {
        // 处理@@子词连接、中文无空格拼接、英文空格分词
        std::vector<std::string> middleLists;
        for (const auto& w : words) {
            if (w == "<s>" || w == "</s>" || w == "<unk>" || w == "<OOV>") {
                continue;
            }
            middleLists.push_back(w);
        }

        std::vector<std::string> wordLists;
        std::string wordItem;

        // 统一走混合分支: 纯中文/纯英文是该分支的特例(IsChineseChar/IsAsciiAlphaChar互斥)
        bool alphaBlank = false;
        for (const auto& ch : middleLists) {
            if (IsChineseChar(ch)) {
                if (alphaBlank) {
                    wordLists.pop_back();
                }
                wordLists.push_back(StripSpaces(ch));
                alphaBlank = false;
            } else if (ch.find("@@") != std::string::npos) {
                AppendSubword(wordItem, ch);
                alphaBlank = false;
            } else if (IsAsciiAlphaChar(ch)) {
                wordItem += ch;
                FlushWord(wordLists, wordItem);
                alphaBlank = true;
            } else {
                wordLists.push_back(ch);
            }
        }

        return BuildSentence(wordLists);
    }

    bool ASRWorker::InitOnnxRuntime(const std::string& modelPath) {
        try {
            std::lock_guard<std::mutex> lock(mOnnxMutex);

            // 1. 创建ONNX Runtime环境
            // 仅使用 CPU EP，将日志级别设为 ERROR 以屏蔽 ORT 的 GPU 设备探测告警
            mOnnxEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "asr_inference");

            // 2. 创建会话选项
            mSessionOptions = std::make_unique<Ort::SessionOptions>();
            mSessionOptions->SetIntraOpNumThreads(4);
            mSessionOptions->SetInterOpNumThreads(1);

            // 3. 创建内存信息
            mMemoryInfo =
                std::make_unique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

            mPredictorSession = std::make_unique<Ort::Session>(*mOnnxEnv, modelPath.c_str(), *mSessionOptions);

            SLOG_INFO << "ASRWorker: ONNX Runtime initialized successfully";
            mOnnxInitialized = true;
            return true;

        } catch (const Ort::Exception& e) {
            SLOG_ERROR << "ASRWorker: ONNX Runtime initialization failed: " << e.what();
            CleanupOnnxResources();
            return false;
        } catch (const std::exception& e) {
            SLOG_ERROR << "ASRWorker: Standard exception during ONNX initialization: " << e.what();
            CleanupOnnxResources();
            return false;
        }
    }

    void ASRWorker::CleanupOnnxResources() {
        std::lock_guard<std::mutex> lock(mOnnxMutex);

        try {
            // 按正确顺序清理资源
            mPredictorSession.reset();
            mMemoryInfo.reset();
            mSessionOptions.reset();
            mOnnxEnv.reset();

            mOnnxInitialized = false;
            SLOG_DEBUG << "ASRWorker: ONNX resources cleaned up";
        } catch (const std::exception& e) {
            SLOG_ERROR << "ASRWorker: Exception during ONNX cleanup: " << e.what();
        }
    }

    bool ASRWorker::ValidateInputShape(const std::vector<int64_t>& shape, const std::string& shapeName) {
        if (shape.empty()) {
            SLOG_ERROR << "ASRWorker: " << shapeName << " shape is empty";
            return false;
        }

        // 检查形状维度 - 放宽要求，允许1-4维
        if (shape.size() < 1 || shape.size() > 4) {
            SLOG_WARN << "ASRWorker: " << shapeName << " shape dimensions should be 1-4, got " << shape.size()
                      << " dimensions, continuing anyway";
        }

        // 检查每个维度的大小 - 放宽要求，允许大于等于0
        for (size_t i = 0; i < shape.size(); ++i) {
            if (shape[i] < 0) {
                SLOG_ERROR << "ASRWorker: " << shapeName << " shape dimension " << i << " must be non-negative, got "
                           << shape[i];
                return false;
            }
        }

        // 检查batch size应该为1 - 放宽为警告
        if (shape.size() >= 1 && shape[0] != 1) {
            SLOG_WARN << "ASRWorker: " << shapeName << " batch size should be 1, got " << shape[0]
                      << ", continuing anyway";
        }

        return true;
    }

    bool ASRWorker::ValidateEncodeLogits(const std::vector<float>& data, const std::vector<int64_t>& shape) {
        if (!ValidateInputShape(shape, "encode_logits")) {
            return false;
        }

        // 检查数据大小是否与形状匹配
        // 注意：bmodel输出可能是固定shape（如256帧），但我们实际可能只需要部分数据
        // 所以这里放宽检查，只验证数据大小不超出tensor容量
        size_t tensorCapacity = 1;
        for (const auto& dim : shape) {
            tensorCapacity *= static_cast<size_t>(dim);
        }

        if (data.size() > tensorCapacity) {
            SLOG_ERROR << "ASRWorker: encode_logits data size exceeds tensor capacity. Capacity: " << tensorCapacity
                       << ", data size: " << data.size();
            return false;
        }

        // 检查特征维度必须为512（如果shape有第三维）
        if (shape.size() >= 3 && shape[2] != 512) {
            SLOG_ERROR << "ASRWorker: encode_logits feature dimension must be 512, got " << shape[2];
            return false;
        }

        // 检查时间步长不能为0
        if (shape.size() >= 2 && shape[1] == 0) {
            SLOG_ERROR << "ASRWorker: encode_logits time steps cannot be 0";
            return false;
        }

        return true;
    }

    bool ASRWorker::ValidateMaskData(const std::vector<float>& data, const std::vector<int64_t>& shape) {
        if (!ValidateInputShape(shape, "mask")) {
            return false;
        }

        // 检查数据大小是否与形状匹配 - 放宽要求
        size_t tensorCapacity = 1;
        for (const auto& dim : shape) {
            tensorCapacity *= static_cast<size_t>(dim);
        }

        if (data.size() > tensorCapacity) {
            SLOG_ERROR << "ASRWorker: mask data size exceeds tensor capacity. Capacity: " << tensorCapacity
                       << ", data size: " << data.size();
            return false;
        }

        // 检查mask的特定形状要求 - 放宽到可选检查
        if (shape.size() >= 2 && shape[1] != 1) {
            SLOG_WARN << "ASRWorker: mask shape[1] should be 1, got " << shape[1] << ", continuing anyway";
        }

        // 检查所有mask值必须为1.0 - 这个检查可以保留，但用warn而不是error
        for (size_t i = 0; i < data.size(); ++i) {
            if (std::abs(data[i] - 1.0f) > 1e-6f) {
                SLOG_WARN << "ASRWorker: mask value at index " << i << " should be 1.0, got " << data[i]
                          << ", continuing anyway";
            }
        }

        return true;
    }

    ASRWorker::InferenceResult ASRWorker::RunOnnxInferencePersistent(const std::vector<float>& encodeLogitsData,
                                                                     const std::vector<int64_t>& encodeLogitsShape,
                                                                     const std::vector<float>& maskData,
                                                                     const std::vector<int64_t>& maskShape) {
        InferenceResult result;
        result.success = false;

        // 检查ONNX Runtime是否已初始化
        if (!mOnnxInitialized) {
            result.message = "ONNX Runtime not initialized";
            return result;
        }

        std::lock_guard<std::mutex> lock(mOnnxMutex);

        try {
            // 1. 数据校验
            if (!ValidateEncodeLogits(encodeLogitsData, encodeLogitsShape)) {
                result.message = "encode_logits validation failed";
                return result;
            }

            if (!ValidateMaskData(maskData, maskShape)) {
                result.message = "mask validation failed";
                return result;
            }

            // 2. 获取会话信息
            Ort::AllocatorWithDefaultOptions allocator;

            // 获取输入名称
            size_t numInputs = mPredictorSession->GetInputCount();
            std::vector<const char*> inputNames;
            std::map<std::string, int> inputMap;

            SLOG_DEBUG << "ASRWorker: Predictor input count: " << numInputs;
            for (size_t i = 0; i < numInputs; i++) {
                auto name = mPredictorSession->GetInputNameAllocated(i, allocator);
                inputNames.push_back(strdup(name.get()));
                inputMap[std::string(name.get())] = i;
            }

            // 获取输出名称
            size_t numOutputs = mPredictorSession->GetOutputCount();
            std::vector<const char*> outputNames;
            std::map<std::string, int> outputMap;

            SLOG_DEBUG << "ASRWorker: Predictor output count: " << numOutputs;
            for (size_t i = 0; i < numOutputs; i++) {
                auto name = mPredictorSession->GetOutputNameAllocated(i, allocator);
                outputNames.push_back(strdup(name.get()));
                outputMap[std::string(name.get())] = i;
            }

            // 3. 准备输入张量
            std::vector<Ort::Value> inputTensors;

            for (size_t i = 0; i < numInputs; i++) {
                std::string inputName = inputNames[i];

                if (inputName.find("encode_logits") != std::string::npos || inputName == "enc") {
                    inputTensors.push_back(Ort::Value::CreateTensor<float>(
                        *mMemoryInfo, const_cast<float*>(encodeLogitsData.data()), encodeLogitsData.size(),
                        encodeLogitsShape.data(), encodeLogitsShape.size()));
                } else if (inputName.find("mask") != std::string::npos) {
                    inputTensors.push_back(
                        Ort::Value::CreateTensor<float>(*mMemoryInfo, const_cast<float*>(maskData.data()),
                                                        maskData.size(), maskShape.data(), maskShape.size()));
                } else {
                    result.message = "Unknown input name: " + inputName;

                    // 清理资源
                    for (auto& name : inputNames) {
                        free(const_cast<char*>(name));
                    }
                    for (auto& name : outputNames) {
                        free(const_cast<char*>(name));
                    }

                    return result;
                }
            }

            // 4. 执行推理
            auto startTime = std::chrono::high_resolution_clock::now();

            auto outputTensors =
                mPredictorSession->Run(Ort::RunOptions {nullptr}, inputNames.data(), inputTensors.data(),
                                       inputTensors.size(), outputNames.data(), outputNames.size());

            auto endTime = std::chrono::high_resolution_clock::now();
            result.inferenceTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

            // 5. 处理输出结果
            for (size_t i = 0; i < outputTensors.size(); i++) {
                std::string outputName = outputNames[i];
                auto tensorInfo = outputTensors[i].GetTensorTypeAndShapeInfo();
                auto shape = tensorInfo.GetShape();
                size_t numElements = tensorInfo.GetElementCount();
                auto tensorType = tensorInfo.GetElementType();

                if (outputName.find("predict_logits") != std::string::npos ||
                    outputName.find("pre_acoustic_embeds") != std::string::npos) {
                    // pre_acoustic_embeds 通常是 float
                    float* outputData = outputTensors[i].GetTensorMutableData<float>();
                    result.predictLogits.assign(outputData, outputData + numElements);
                    result.predictLogitsShape = shape;
                    SLOG_DEBUG << "ASRWorker: predictor output[" << outputName << "] elements=" << numElements;
                } else if (outputName.find("token_num") != std::string::npos ||
                           outputName.find("pre_token_length") != std::string::npos) {
                    // pre_token_length 可能是 int64 / int32 / float
                    if (tensorType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
                        int64_t* outputData = outputTensors[i].GetTensorMutableData<int64_t>();
                        result.tokenNum.assign(outputData, outputData + numElements);
                    } else if (tensorType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
                        // 某些模型（如 seaco_paraformer predictor）声明 pre_token_length 为 int32
                        int32_t* outputData = outputTensors[i].GetTensorMutableData<int32_t>();
                        result.tokenNum.assign(outputData, outputData + numElements);
                    } else {
                        // 某些模型声明 float 但实际是 int，按 float 读取
                        float* outputData = outputTensors[i].GetTensorMutableData<float>();
                        result.tokenNum.assign(outputData, outputData + numElements);
                    }
                    result.tokenNumShape = shape;
                }
            }

            // 6. 清理资源
            for (auto& name : inputNames) {
                free(const_cast<char*>(name));
            }
            for (auto& name : outputNames) {
                free(const_cast<char*>(name));
            }

            result.success = true;
            result.message = "Inference successful";

        } catch (const Ort::Exception& e) {
            result.message = "ONNX Runtime error: " + std::string(e.what());
            SLOG_ERROR << "ASRWorker: " << result.message;
        } catch (const std::exception& e) {
            result.message = "Standard error: " + std::string(e.what());
            SLOG_ERROR << "ASRWorker: " << result.message;
        }

        return result;
    }

    std::vector<int> ASRWorker::TokensToIds(const std::vector<std::string>& tokens) {
        std::vector<int> ids;
        ids.reserve(tokens.size());

        for (const auto& token : tokens) {
            auto it = mTokenToId.find(token);
            if (it != mTokenToId.end()) {
                ids.push_back(it->second);
            } else {
                // 如果找不到token，使用未知token的ID（通常为3）
                ids.push_back(3);
            }
        }

        return ids;
    }

}  // namespace qifeng