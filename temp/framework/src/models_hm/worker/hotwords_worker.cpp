/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "models_hm/worker/hotwords_worker.h"
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <numeric>
#include <sstream>
#include <vector>

#include "common/config_manager.h"
#include "common/logger.h"

namespace qifeng {

    HotwordsWorker::HotwordsWorker(const std::string& modelPath, int tpuId, IOMode ioMode)
        : ModelWorker(modelPath, tpuId, ioMode), mHotwordsEnabled(false), mTokenizerLoaded(false), mBlankId(0),
          mSosId(1), mEosId(2) {
        try {
            SLOG_INFO << "HotwordsWorker: Constructor called";

            // 加载令牌转换器
            LoadTokenizer();

            // 初始化节点名称
            mInNameSpeech = "speech";
            mInNameSpeechLengths = "speech_lengths";

            for (int i = 0; i < 4; ++i) {
                mInNamesCache.push_back("in_cache" + std::to_string(i));
            }
        } catch (const std::exception& e) {
            SLOG_ERROR << "HotwordsWorker: Exception in constructor: " << e.what();
        } catch (...) {
            SLOG_ERROR << "HotwordsWorker: Unknown exception in constructor";
        }
    }

    HotwordsWorker::~HotwordsWorker() {
        try {
            SLOG_INFO << "HotwordsWorker: Destructor called";
        } catch (const std::exception& e) {
            SLOG_ERROR << "HotwordsWorker: Exception in destructor: " << e.what();
        } catch (...) {
            SLOG_ERROR << "HotwordsWorker: Unknown exception in destructor";
        }
    }

    void HotwordsWorker::UpdateHotwords(const std::vector<std::string>& hotwords) {
        try {
            SLOG_INFO << "HotwordsWorker: UpdateHotwords begin, hotword_count=" << hotwords.size();

            // 更新热词列表
            mHotwords = hotwords;
            mHotwordsEnabled = !mHotwords.empty();

            // 清空之前的热词上下文
            mHwSelected.clear();
            mHwSelectedShape.clear();

            if (mHotwordsEnabled) {
                SLOG_INFO << "HotwordsWorker: Hotwords updated successfully";
            } else {
                SLOG_INFO << "HotwordsWorker: Hotwords cleared";
            }
        } catch (const std::exception& e) {
            SLOG_ERROR << "HotwordsWorker: UpdateHotwords exception: " << e.what();
        }
    }

    bool HotwordsWorker::HasHotwordContext() const {
        return mHotwordsEnabled && !mHwSelected.empty();
    }

    bool HotwordsWorker::ApplyHotwordBias(int encoderTimeSteps, const std::vector<float>& preAcousticEmbeds,
                                          const std::vector<float>& decodeHidden, int preTokenLength, int hiddenDim,
                                          const std::vector<float>& decoderPred, int vocabSize,
                                          std::vector<float>& mergedLogits) {
        try {
            SLOG_INFO << "HotwordsWorker: ApplyHotwordBias called";

            // 如果没有热词上下文，直接返回原始的decoder_pred
            if (!HasHotwordContext()) {
                SLOG_WARN << "HotwordsWorker: No hotword context available";
                mergedLogits = decoderPred;
                return true;
            }

            // 简单的实现：直接返回原始的decoder_pred
            // 实际的热词偏置逻辑需要根据具体的模型和算法进行实现
            mergedLogits = decoderPred;

            SLOG_INFO << "HotwordsWorker: ApplyHotwordBias done (simple implementation)";
            return true;
        } catch (const std::exception& e) {
            SLOG_ERROR << "HotwordsWorker: ApplyHotwordBias exception: " << e.what();
            return false;
        }
    }

    void HotwordsWorker::LoadTokenizer() {
        try {
            // 从配置文件获取tokenizer路径
            ConfigManager& configMgr = ConfigManager::GetInstance();
            std::string tokenizerPath = configMgr.GetString(
                "models.asr", "tokenizer",
                "/data/aas/model/speech_seaco_paraformer_large_asr_nat-zh-cn-16k-common-vocab8404-0115/tokens.json");

            // 打开tokenizer文件
            std::ifstream tokenizerFile(tokenizerPath);
            if (!tokenizerFile.is_open()) {
                SLOG_WARN << "HotwordsWorker: Failed to open tokenizer file, using default mapping";
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

            // 读取tokenizer文件
            std::string line;
            int id = 0;
            while (std::getline(tokenizerFile, line)) {
                // 跳过空行和注释
                if (line.empty() || line[0] == '#' || line[0] == ']' || line[0] == '[') {
                    continue;
                }

                // 移除行尾空格
                line.erase(line.find_last_not_of(" \t\n\r") + 1);

                // 移除英文引号和逗号以及空格
                line.erase(std::remove(line.begin(), line.end(), '"'), line.end());
                line.erase(std::remove(line.begin(), line.end(), ','), line.end());
                line.erase(std::remove(line.begin(), line.end(), ' '), line.end());

                // 存储token映射
                mTokenizer[id] = line;
                mTokenToId[line] = id;
                id++;
            }

            tokenizerFile.close();
            mTokenizerLoaded = true;
            SLOG_INFO << "HotwordsWorker: Tokenizer loaded successfully with " << mTokenizer.size() << " tokens";
        } catch (const std::exception& e) {
            SLOG_ERROR << "HotwordsWorker: Exception during tokenizer loading: " << e.what();
            mTokenizerLoaded = false;
        }
    }

    std::string HotwordsWorker::TokensToText(const std::vector<int>& tokens) {
        std::string text;

        for (int token : tokens) {
            // 跳过特殊令牌
            if (token == mBlankId || token == mSosId || token == mEosId) {
                continue;
            }

            // 查找token对应的文本
            auto it = mTokenizer.find(token);
            if (it != mTokenizer.end()) {
                text += it->second;
            } else {
                // 如果找不到token，使用默认值
                text += "<unk>";
            }
        }

        return text;
    }

    std::string HotwordsWorker::PostprocessText(const std::string& text) {
        // 简单的文本后处理
        std::string processed = text;

        // 移除多余的空格
        size_t pos = 0;
        while ((pos = processed.find("  ", pos)) != std::string::npos) {
            processed.replace(pos, 2, " ");
            pos += 1;
        }

        // 移除首尾空格
        size_t start = processed.find_first_not_of(" ");
        if (start != std::string::npos) {
            processed = processed.substr(start);
        }
        size_t end = processed.find_last_not_of(" ");
        if (end != std::string::npos) {
            processed = processed.substr(0, end + 1);
        }

        return processed;
    }

    std::vector<int> HotwordsWorker::TokensToIds(const std::vector<std::string>& tokens) {
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