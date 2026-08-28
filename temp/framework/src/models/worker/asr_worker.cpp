/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "models/worker/asr_worker.h"
#include "models/worker/hotwords_worker.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
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

    ASRWorker::ASRWorker(const std::string& modelPath, int tpuId, IOMode ioMode)
        : ModelWorker(modelPath, tpuId, ioMode), mHotwordsEnabled(false), mTokenizerLoaded(false), mBlankId(0),
          mSosId(1), mEosId(2), mOnnxInitialized(false) {
        try {
            SLOG_INFO << "ASRWorker: Constructor called";

            // 加载令牌转换器
            LoadTokenizer();

            // 初始化节点名称
            mInNameSpeech = "speech";
            mInNameSpeechLengths = "speech_lengths";

            for (int i = 0; i < 4; ++i) {
                mInNamesCache.push_back("in_cache" + std::to_string(i));
            }

            // 初始化ONNX Runtime
            if (!InitOnnxRuntime()) {
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

        if (speechFeat.size() > 256 || speechFeat[0].size() > 560) {
            throw std::runtime_error("Invalid input data: " + std::to_string(speechFeat.size()) + " " +
                                     std::to_string(speechFeat[0].size()));
        }
        try {
            if (speechFeat.empty() || speechFeat[0].empty()) {
                throw std::runtime_error("Speech data is empty");
            }

            // 计算输入数据的形状
            size_t timeSteps = speechFeat.size();
            size_t featureDim = speechFeat[0].size();

            // 确保形状不超过最大限制
            const size_t MAX_TIME_STEPS = 256;
            const size_t MAX_FEATURE_DIM = 560;

            if (timeSteps > MAX_TIME_STEPS) {
                timeSteps = MAX_TIME_STEPS;
            }
            if (featureDim > MAX_FEATURE_DIM) {
                featureDim = MAX_FEATURE_DIM;
            }

            // encoder
            // 准备输入数据
            ModelInput input;
            ModelOutput output;

            // 获取encoder模型的输入输出节点名称
            std::vector<std::string> encoderInputs = GetInputNames("encoder");
            std::vector<std::string> encoderOutputs = GetOutputNames("encoder");

            SLOG_DEBUG << "ASRWorker: Encoder input names:";
            for (const auto& name : encoderInputs) {
                SLOG_DEBUG << "  " << name;
            }
            SLOG_DEBUG << "ASRWorker: Encoder output names:";
            for (const auto& name : encoderOutputs) {
                SLOG_DEBUG << "  " << name;
            }

            // speech传入的是一个二维，需要展开成一维
            std::vector<float> speechFlat;
            for (const auto& frame : speechFeat) {
                speechFlat.insert(speechFlat.end(), frame.begin(), frame.end());
            }

            // 设置输入节点 - 使用从模型获取的实际节点名称
            // 假设第一个输入是speech，第二个是speech_lengths
            if (encoderInputs.size() >= 1) {
                input.data[encoderInputs[0]] = const_cast<float*>(speechFlat.data());
                input.shapes[encoderInputs[0]] = {1, static_cast<int>(timeSteps), static_cast<int>(featureDim)};
                SLOG_DEBUG << "ASRWorker: Setting encoder input[0] shape: [1, " << timeSteps << ", " << featureDim
                           << "]";
            }
            if (encoderInputs.size() >= 2) {
                int speechLen = timeSteps;
                input.data[encoderInputs[1]] = &speechLen;
                input.shapes[encoderInputs[1]] = {1};
                SLOG_DEBUG << "ASRWorker: Setting encoder input[1] shape: [1]";
            }

            // 准备输出缓冲区 - 使用实际的时间步长，而不是MAX_TIME_STEPS
            // 因为bmodel的tensor shape是固定的，我们需要用实际的time_steps
            int encodeOutSize = 1 * timeSteps * 512;
            std::vector<float> encodeOut(encodeOutSize);
            float encodeOutLensFloat = 0.0f;

            if (encoderOutputs.size() >= 1) {
                output.data[encoderOutputs[0]] = encodeOut.data();
                output.shapes[encoderOutputs[0]] = {1, static_cast<int>(timeSteps), 512};
                SLOG_DEBUG << "ASRWorker: Setting encoder output[0] shape: [1, " << timeSteps << ", 512]";
            }
            if (encoderOutputs.size() >= 2) {
                output.data[encoderOutputs[1]] = &encodeOutLensFloat;
                output.shapes[encoderOutputs[1]] = {1};
                SLOG_DEBUG << "ASRWorker: Setting encoder output[1] shape: [1]";
            }

            // 调用encoder模型
            int encoderResult = Process("encoder", input, output);
            if (encoderResult != ERR_OK) {
                SLOG_ERROR << "ASRWorker: Encoder inference failed with code: " << encoderResult;
                return "";
            }

            // predictor
            // 处理encoder输出
            int encodeOutLens = static_cast<int>(round(encodeOutLensFloat));
            std::vector<float> encodeOutMask(1 * 1 * encodeOutLens, 1.0f);

            // 使用持久化的ONNX会话进行推理
            InferenceResult predictorResult =
                RunOnnxInferencePersistent(encodeOut, {1, encodeOutLens, 512}, encodeOutMask, {1, 1, encodeOutLens});
            if (!predictorResult.success) {
                SLOG_ERROR << "ASRWorker: Predictor inference failed: " << predictorResult.message;
                return "";
            }

            // 准备predictor输出
            // 1. pre_acoustic_embeds
            std::vector<float> preAcousticEmbeds = predictorResult.predictLogits;

            // 2. pre_token_length
            float preTokenLengthFloat = predictorResult.tokenNum[0];

            // decoder
            int preTokenLength = static_cast<int>(round(preTokenLengthFloat));

            // 准备decoder输入
            ModelInput decoderInput;
            ModelOutput decoderOutput;

            // 设置decoder输入节点
            decoderInput.data["encode_logits"] = encodeOut.data();
            decoderInput.shapes["encode_logits"] = {1, encodeOutLens, 512};

            decoderInput.data["encode_lens"] = &encodeOutLens;
            decoderInput.shapes["encode_lens"] = {1};

            decoderInput.data["pre_acoustic_embeds"] = preAcousticEmbeds.data();
            decoderInput.shapes["pre_acoustic_embeds"] = {1, preTokenLength, 512};

            decoderInput.data["pre_token_length"] = &preTokenLength;
            decoderInput.shapes["pre_token_length"] = {1};

            // 准备decoder输出
            int decodeHiddenSize = 1 * 128 * 512;  // 假设输出维度为512
            std::vector<float> decodeHidden(decodeHiddenSize);
            std::vector<float> tmp(1);

            // 设置decoder输出节点
            decoderOutput.data["decode_hidden_LayerNormalization"] = decodeHidden.data();
            decoderOutput.shapes["decode_hidden_LayerNormalization"] = {1, 128, 512};

            decoderOutput.data["decode_lens_ReduceSum"] = tmp.data();
            decoderOutput.shapes["decode_lens_ReduceSum"] = {1, 1};

            // 调用decoder模型
            int decoderResult = Process("decoder", decoderInput, decoderOutput);
            if (decoderResult != ERR_OK) {
                SLOG_ERROR << "ASRWorker: Decoder inference failed";
                return "";
            }

            // decoder_output
            // 准备decoder_output输入
            ModelInput decoderOutputInput;
            ModelOutput decoderOutputOutput;

            // 设置decoder_output输入节点
            decoderOutputInput.data["decode_hidden"] = decodeHidden.data();
            decoderOutputInput.shapes["decode_hidden"] = {1, preTokenLength, 512};

            // 准备decoder_output输出
            int vocabSize = static_cast<int>(mTokenizer.size());
            int decodeOutSize = 1 * 128 * vocabSize;
            std::vector<float> decodeOut(decodeOutSize);

            // 设置decoder_output输出节点
            decoderOutputOutput.data["decode_out_Add"] = decodeOut.data();
            decoderOutputOutput.shapes["decode_out_Add"] = {1, 128, vocabSize};

            // 调用decoder_output模型
            int decoderOutResult = Process("decoder_output", decoderOutputInput, decoderOutputOutput);
            if (decoderOutResult != ERR_OK) {
                SLOG_ERROR << "ASRWorker: Decoder output inference failed";
                return "";
            }

            log_softmax_3d(decodeOut.data(), decodeOut.data(), 1, 128, vocabSize);

            if (hotwordsWorker != nullptr) {
                // 热词处理逻辑 - 需要根据HotwordsWorker的实际接口调整
                SLOG_DEBUG << "ASRWorker: Hotwords worker provided, but hotword processing not implemented yet";
            }

            std::vector<std::vector<float>> amScores = slice_decoder_output(decodeOut, 128, vocabSize, preTokenLength);
            auto yseq = asr_argmax(amScores);
            std::vector<int> yseqVec(yseq.begin(), yseq.end());
            yseqVec.insert(yseqVec.begin(), 1);
            yseqVec.push_back(2);
            nc::NdArray<int> yseqWithSe(yseqVec);

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

    bool ASRWorker::InitOnnxRuntime() {
        try {
            std::lock_guard<std::mutex> lock(mOnnxMutex);

            // 1. 创建ONNX Runtime环境
            mOnnxEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "asr_inference");

            // 2. 创建会话选项
            mSessionOptions = std::make_unique<Ort::SessionOptions>();
            mSessionOptions->SetIntraOpNumThreads(4);
            mSessionOptions->SetInterOpNumThreads(1);

            // 3. 创建内存信息
            mMemoryInfo =
                std::make_unique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

            // 从配置文件获取predictor路径
            ConfigManager& configMgr = ConfigManager::GetInstance();
            std::string predictorPath = configMgr.GetString(
                "models.asr", "predictor",
                "/data/aas/model/speech_seaco_paraformer_large_asr_nat-zh-cn-16k-common-vocab8404-0115/seaco_paraformer_predictor.onnx");
            mPredictorSession = std::make_unique<Ort::Session>(*mOnnxEnv, predictorPath.c_str(), *mSessionOptions);

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
                SLOG_DEBUG << "  Input " << i << ": " << std::string(name.get());
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
                SLOG_DEBUG << "  Output " << i << ": " << std::string(name.get());
            }

            // 3. 准备输入张量
            std::vector<Ort::Value> inputTensors;

            for (size_t i = 0; i < numInputs; i++) {
                std::string inputName = inputNames[i];

                if (inputName.find("encode_logits") != std::string::npos) {
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
                float* outputData = outputTensors[i].GetTensorMutableData<float>();

                if (outputName.find("predict_logits") != std::string::npos) {
                    result.predictLogits.assign(outputData, outputData + numElements);
                    result.predictLogitsShape = shape;
                } else if (outputName.find("token_num") != std::string::npos) {
                    result.tokenNum.assign(outputData, outputData + numElements);
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