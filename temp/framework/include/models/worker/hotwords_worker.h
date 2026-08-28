/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_HOTWORDS_WORKER_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_HOTWORDS_WORKER_H

#include <any>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/config_manager.h"
#include "models/worker/worker.h"

namespace qifeng {

    /**
     * @class HotwordsWorker
     * @brief 热词模型工作者类，继承自ModelWorker基类
     */
    class HotwordsWorker : public ModelWorker {
    public:
        HotwordsWorker(const std::string& modelPath, int tpuId = 0, IOMode ioMode = IOMode::SYSIO);
        ~HotwordsWorker();

        // 热词相关方法
        void UpdateHotwords(const std::vector<std::string>& hotwords);

        bool HasHotwordContext() const;
        bool ApplyHotwordBias(int encoderTimeSteps, const std::vector<float>& preAcousticEmbeds,
                              const std::vector<float>& decodeHidden, int preTokenLength, int hiddenDim,
                              const std::vector<float>& decoderPred, int vocabSize, std::vector<float>& mergedLogits);

        /**
         * @brief 将字符串中的每个字符转换为其对应的ID
         * @param tokens 输入的UTF-8编码字符串vector
         * @return 字符ID的向量
         */
        std::vector<int> TokensToIds(const std::vector<std::string>& tokens);

    private:
        // 输入节点名称
        std::string mInNameSpeech;
        std::string mInNameSpeechLengths;
        std::vector<std::string> mInNamesCache;

        // 输出节点名称
        std::string mOutNameText;
        std::string mOutNameEncoderOut;
        std::string mOutNameEncoderOutLens;
        std::vector<std::string> mOutNamesCache;

        // 热词相关
        std::vector<std::string> mHotwords;
        bool mHotwordsEnabled;

        std::vector<float> mHwSelected;
        std::vector<size_t> mHwSelectedShape;

        // 令牌转换器
        std::unordered_map<int, std::string> mTokenizer;
        std::unordered_map<std::string, int> mTokenToId;
        bool mTokenizerLoaded;

        // 特殊令牌ID
        int mBlankId;
        int mSosId;
        int mEosId;

        // 辅助方法
        void LoadTokenizer();
        std::string TokensToText(const std::vector<int>& tokens);
        std::string PostprocessText(const std::string& text);
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_HOTWORDS_WORKER_H