/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "models/worker/punc_worker.h"

#include "NumCpp.hpp"
#include "NumCpp/Functions/argmax.hpp"
#include "common/config_manager.h"
#include "common/logger.h"
#include "json/json.h"
#include <algorithm>
#include <cstring>
#include <fstream>

namespace qifeng {

    // 定义静态常量
    const int PUNCWorker::mCachePopTriggerLimit;

    // 标点符号列表
    const std::vector<std::string> PuncList = {"<unk>", "_", "，", "。", "？", "、"};
    const int PERIOD = 3;  // 句号索引

    PUNCWorker::PUNCWorker(const std::string& modelPath, int tpuId, IOMode ioMode)
        : ModelWorker(modelPath, tpuId, ioMode), mTokenLoaded(false) {
        try {
            // 从配置文件获取tokenizer路径
            ConfigManager& configMgr = ConfigManager::GetInstance();
            mTokenListPath = configMgr.GetString(
                "models.punc", "tokenizer", "/data/aas/model/punc_ct-transformer_zh-cn-common-vocab272727/tokens.json");

            SLOG_INFO << "PUNCWorker: Initializing punctuation model worker";
            SLOG_INFO << "PUNCWorker: Token list path: " << mTokenListPath;

            LoadToken();
        } catch (const std::exception& e) {
            SLOG_ERROR << "PUNCWorker: Exception in constructor: " << e.what();
        } catch (...) {
            SLOG_ERROR << "PUNCWorker: Unknown exception in constructor";
        }
    }

    PUNCWorker::~PUNCWorker() {
        try {
            SLOG_INFO << "PUNCWorker: Destructor called";
        } catch (const std::exception& e) {
            SLOG_ERROR << "PUNCWorker: Exception in destructor: " << e.what();
        } catch (...) {
            SLOG_ERROR << "PUNCWorker: Unknown exception in destructor";
        }
    }

    void PUNCWorker::LoadToken() {
        std::lock_guard<std::mutex> lock(mTokenMapMutex);
        try {
            std::ifstream file(mTokenListPath);
            if (!file.is_open()) {
                SLOG_ERROR << "PUNCWorker: Failed to open token list file: " << mTokenListPath;
                return;
            }
            Json::Value tokenList;
            Json::Reader reader;

            if (!reader.parse(file, tokenList)) {
                SLOG_ERROR << "PUNCWorker: Failed to parse token list file: " << mTokenListPath;
                return;
            }

            // 将JSON数组转换为token_map
            if (tokenList.isArray()) {
                for (Json::ArrayIndex i = 0; i < tokenList.size(); ++i) {
                    if (tokenList[i].isString()) {
                        std::string token = tokenList[i].asString();
                        mTokenMap[token] = i;
                        mTokenId[i] = token;
                    }
                }
            } else {
                SLOG_ERROR << "PUNCWorker: Token list is not a valid JSON array";
                return;
            }

            // 确保包含未知token
            if (mTokenMap.find("<unk>") == mTokenMap.end()) {
                mTokenMap["<unk>"] = mTokenMap.size();
                mTokenId[mTokenMap.size() - 1] = "<unk>";
            }

            SLOG_INFO << "PUNCWorker: Loaded " << mTokenMap.size() << " tokens from file";
            mTokenLoaded = true;
        } catch (const std::exception& e) {
            SLOG_ERROR << "PUNCWorker: Failed to load token list: " << e.what();
        }
    }

    // UTF-8字符迭代辅助函数
    bool GetNextUtf8Char(const std::string& str, size_t& pos, std::string& outChar) {
        if (pos >= str.size()) {
            return false;
        }

        unsigned char c = static_cast<unsigned char>(str[pos]);
        size_t charLen = 1;

        if (c >= 0xF0 && c <= 0xF7) {
            charLen = 4;
        } else if (c >= 0xE0 && c <= 0xEF) {
            charLen = 3;
        } else if (c >= 0xC0 && c <= 0xDF) {
            charLen = 2;
        }

        // 确保不越界
        if (pos + charLen > str.size()) {
            charLen = 1;
        }

        outChar = str.substr(pos, charLen);
        pos += charLen;
        return true;
    }

    // 将字符串拆分为单个字符/字的vector
    std::vector<std::string> SplitString(const std::string& text) {
        std::vector<std::string> chars;
        size_t pos = 0;
        std::string utf8Char;

        while (GetNextUtf8Char(text, pos, utf8Char)) {
            chars.push_back(std::move(utf8Char));
        }

        return chars;
    }

    // 判断输入字符串是单个英文字符还是中国汉字
    bool IsChineseChar(const std::string& ch) {
        if (ch.empty() || ch.size() > 3) {
            return false;
        }

        if (ch.size() != 3) {
            return false;
        }

        unsigned char b1 = static_cast<unsigned char>(ch[0]);
        unsigned char b2 = static_cast<unsigned char>(ch[1]);
        unsigned char b3 = static_cast<unsigned char>(ch[2]);

        bool b1Valid = (b1 >= 0xE4 && b1 <= 0xE9);
        bool b2Valid = (b2 >= 0x80 && b2 <= 0xBF);
        bool b3Valid = (b3 >= 0x80 && b3 <= 0xBF);

        return b1Valid && b2Valid && b3Valid;
    }

    std::vector<std::string> CodeMixSplitWords(const std::string& text) {
        std::vector<std::string> words;
        std::vector<std::string> segs = SplitString(text);

        std::string currentWord = "";
        for (auto& c : segs) {
            if (!IsChineseChar(c)) {
                currentWord += c;
            } else {
                if (!currentWord.empty()) {
                    words.push_back(currentWord);
                    currentWord.clear();
                }
                words.push_back(c);
            }
        }
        if (!currentWord.empty()) {
            words.push_back(currentWord);
            currentWord.clear();
        }
        return words;
    }

    std::vector<int> PUNCWorker::TokensToIds(const std::vector<std::string>& tokens) {
        std::vector<int> ids;
        for (const auto& token : tokens) {
            if (mTokenMap.find(token) != mTokenMap.end()) {
                ids.push_back(mTokenMap[token]);
            } else {
                ids.push_back(mTokenMap["<unk>"]);  // 未知token
            }
        }
        return ids;
    }

    std::string PUNCWorker::IdToToken(int id) {
        if (mTokenId.find(id) != mTokenId.end()) {
            return mTokenId[id];
        }
        return "<unk>";
    }

    std::string PUNCWorker::PuncInference(const std::string& text) {
        // 整个推理过程需要线程安全保护
        std::lock_guard<std::mutex> lock(mInferMutex);

        int splitSize = 20;

        // 1. 分词
        auto splitText = CodeMixSplitWords(text);

        // 2. 转换为ID
        auto splitTextId = TokensToIds(splitText);

        // 3. 分割成小句子
        auto miniSentences = SplitToMiniSentence(splitText, splitSize);
        auto miniSentencesId = SplitToMiniSentence(splitTextId, splitSize);
        assert(miniSentences.size() == miniSentencesId.size());

        std::vector<std::string> cacheSent;
        std::vector<int> cacheSentId;
        std::string newMiniSentence;
        std::vector<int> newMiniSentencePunc;

        // 4. 处理每个小句子
        for (size_t miniSentenceI = 0; miniSentenceI < miniSentences.size(); miniSentenceI++) {
            auto miniSentence = miniSentences[miniSentenceI];
            auto miniSentenceId = miniSentencesId[miniSentenceI];

            // 合并缓存
            std::vector<std::string> combinedSentence = cacheSent;
            combinedSentence.insert(combinedSentence.end(), miniSentence.begin(), miniSentence.end());

            std::vector<int> combinedSentenceId = cacheSentId;
            combinedSentenceId.insert(combinedSentenceId.end(), miniSentenceId.begin(), miniSentenceId.end());

            // 推理
            std::vector<std::vector<float>> res = Infer(combinedSentenceId, combinedSentenceId.size());

            if (res.empty()) {
                SLOG_WARN << "PUNCWorker: res size is 0";
                continue;
            }

            std::vector<int> punctuations;
            for (auto& vec : res) {
                if (vec.empty()) {
                    SLOG_WARN << "PUNCWorker: empty vector in res";
                    continue;
                }
                try {
                    auto tmp = nc::argmax<float>(vec, nc::Axis::COL);
                    if (tmp.size() == 0) {
                        continue;
                    }
                    punctuations.push_back(tmp[0]);
                } catch (const std::exception& e) {
                    SLOG_ERROR << "PUNCWorker: nc::argmax failed: " << e.what();
                    continue;
                }
            }

            // 检查punctuations大小是否与combinedSentence一致
            if (punctuations.size() != combinedSentence.size()) {
                SLOG_ERROR << "PUNCWorker: punctuations size mismatch. punctuations=" << punctuations.size()
                           << ", combinedSentence=" << combinedSentence.size();
                continue;
            }

            // 搜索句子结束位置
            int sentenceEnd = -1;
            int lastCommaIndex = -1;

            if (miniSentenceI < miniSentences.size() - 1) {
                // 从后往前搜索句号或问号
                for (int i = combinedSentence.size() - 1; i >= 0; i--) {
                    // 检查索引边界
                    if (i < 0 || i >= static_cast<int>(punctuations.size())) {
                        SLOG_ERROR << "PUNCWorker: index out of bounds. i=" << i
                                   << ", punctuations.size()=" << punctuations.size();
                        break;
                    }

                    int puncIndex = punctuations[i];
                    // 检查标点符号索引边界
                    if (puncIndex < 0 || puncIndex >= static_cast<int>(PuncList.size())) {
                        SLOG_ERROR << "PUNCWorker: punctuation index out of bounds. index=" << puncIndex
                                   << ", PuncList.size()=" << PuncList.size();
                        continue;
                    }

                    std::string puncChar = PuncList[puncIndex];
                    if (puncChar == "。" || puncChar == "？") {
                        sentenceEnd = i;
                        break;
                    }
                    if (lastCommaIndex < 0 && puncChar == "，") {
                        lastCommaIndex = i;
                    }
                }

                // 如果句子太长，在逗号处截断
                if (sentenceEnd < 0 && combinedSentence.size() > static_cast<size_t>(mCachePopTriggerLimit) &&
                    lastCommaIndex >= 0) {
                    sentenceEnd = lastCommaIndex;
                    punctuations[sentenceEnd] = PERIOD;
                }

                // 更新缓存
                if (sentenceEnd >= 0) {
                    cacheSent =
                        std::vector<std::string>(combinedSentence.begin() + sentenceEnd + 1, combinedSentence.end());
                    cacheSentId =
                        std::vector<int>(combinedSentenceId.begin() + sentenceEnd + 1, combinedSentenceId.end());
                    combinedSentence =
                        std::vector<std::string>(combinedSentence.begin(), combinedSentence.begin() + sentenceEnd + 1);
                    punctuations = std::vector<int>(punctuations.begin(), punctuations.begin() + sentenceEnd + 1);
                } else {
                    // 如果没有找到句子结束位置，将整个句子添加到结果中，并清空缓存
                    cacheSent.clear();
                    cacheSentId.clear();
                }
            } else {
                // 最后一个句子，清空缓存
                cacheSent.clear();
                cacheSentId.clear();
            }

            // 合并标点符号
            for (int punc : punctuations) {
                newMiniSentencePunc.push_back(punc);
            }

            // 构建带标点的句子
            std::vector<std::string> wordsWithPunc;
            for (size_t i = 0; i < combinedSentence.size(); i++) {
                // 处理空格
                if (i > 0) {
                    // 简化的空格处理逻辑
                    if (combinedSentence[i].size() == 1 && combinedSentence[i - 1].size() == 1) {
                        wordsWithPunc.push_back(" " + combinedSentence[i]);
                        continue;
                    }
                }

                wordsWithPunc.push_back(combinedSentence[i]);

                // 添加标点符号
                if (i < punctuations.size()) {
                    int puncIndex = punctuations[i];
                    if (puncIndex >= 0 && puncIndex < static_cast<int>(PuncList.size())) {
                        if (PuncList[puncIndex] != "_") {
                            wordsWithPunc.push_back(PuncList[puncIndex]);
                        }
                    } else {
                        SLOG_ERROR << "PUNCWorker: punctuation index out of bounds in word processing. index="
                                   << puncIndex << ", PuncList.size()=" << PuncList.size();
                    }
                }
            }

            // 合并成字符串
            std::string currentSegment;
            for (const auto& word : wordsWithPunc) {
                currentSegment += word;
            }

            // 避免重复内容
            if (!currentSegment.empty()) {
                newMiniSentence += currentSegment;
            }

            // 处理句子结尾
            if (miniSentenceI == miniSentences.size() - 1) {
                std::string lastChar = "";
                if (!newMiniSentence.empty()) {
                    // 正确获取最后一个UTF-8字符
                    size_t pos = newMiniSentence.size();
                    // 从后往前找第一个UTF-8起始字节
                    while (pos > 0) {
                        pos--;
                        unsigned char c = static_cast<unsigned char>(newMiniSentence[pos]);
                        // UTF-8起始字节的最高两位不是10
                        if ((c & 0xC0) != 0x80) {
                            lastChar = newMiniSentence.substr(pos);
                            break;
                        }
                    }
                    if (pos == 0 && lastChar.empty()) {
                        lastChar = newMiniSentence.substr(0, 1);
                    }
                }

                if (lastChar == "，" || lastChar == "、") {
                    newMiniSentence = newMiniSentence.substr(0, newMiniSentence.length() - lastChar.length()) + "。";
                    if (!newMiniSentencePunc.empty()) {
                        newMiniSentencePunc.back() = PERIOD;
                    }
                } else if (lastChar != "。" && lastChar != "？") {
                    newMiniSentence += "。";
                    if (!newMiniSentencePunc.empty()) {
                        newMiniSentencePunc.back() = PERIOD;
                    }
                }
            }
        }
        return newMiniSentence;
    }

    // 标点符号推理类
    std::vector<std::vector<float>> PUNCWorker::Infer(const std::vector<int>& textIds, int textLength) {
        // 准备数据
        ModelInput input;
        ModelOutput output;

        int actualLength = textLength;
        std::vector<int> expectTextIds(textIds);

        // 确保不超过200个token
        if (actualLength > 200) {
            SLOG_ERROR << "PUNCWorker: text length exceeds maximum. actualLength=" << actualLength << ", max=200";
            actualLength = 200;
            expectTextIds.resize(200);
        }

        // 填充到200个token
        for (int i = 0; i < 200 - actualLength; ++i) {
            expectTextIds.push_back(0);
        }
        int expectLength = expectTextIds.size();

        // 设置输入数据 - 避免使用const_cast，直接使用数据指针
        input.data["inputs"] = expectTextIds.data();
        input.shapes["inputs"] = {1, static_cast<int>(expectLength)};  // 假设batch_size=1

        input.data["text_lengths"] = &expectLength;
        input.shapes["text_lengths"] = {1};

        // 准备输出缓冲区
        // 根据模型输出示例，输出是logits_Add，形状为[batch_size, sequence_length, num_classes]
        int numClasses = 6;  // 从输出示例可以看到有6个类别
        size_t outputSize = 1 * expectLength * numClasses;

        // 分配输出缓冲区内存
        std::vector<float> logits(outputSize);

        // 设置输出数据指针
        output.data["logits_Add"] = logits.data();
        // 设置输出形状
        output.shapes["logits_Add"] = {1, static_cast<int>(expectLength), numClasses};

        // 调用模型推理
        Process(input, output);

        // 检查logits大小
        if (logits.size() < static_cast<size_t>(actualLength * numClasses)) {
            SLOG_ERROR << "PUNCWorker: logits size insufficient. logits.size()=" << logits.size()
                       << ", required=" << actualLength * numClasses;
            return {};
        }

        // 需要把logits取出对应的数据然后转换成对应的格式
        std::vector<std::vector<float>> res;
        for (int i = 0; i < actualLength; ++i) {
            std::vector<float> tmp(logits.begin() + i * numClasses, logits.begin() + (i + 1) * numClasses);
            res.push_back(tmp);
        }

        return res;
    }

}  // namespace qifeng