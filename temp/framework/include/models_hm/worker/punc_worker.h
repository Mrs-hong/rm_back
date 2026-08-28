/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_PUNC_WORKER_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_PUNC_WORKER_H

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "common/config_manager.h"
#include "models_hm/worker/worker.h"

namespace qifeng {

    // 标点预测 Worker (TCIM 版本)
    class PUNCWorker : public ModelWorker {
    public:
        PUNCWorker(const std::string& modelPath, int tpuId = 0, IOMode ioMode = IOMode::SYSIO);
        ~PUNCWorker();

        std::string PuncInference(const std::string& text);

    private:
        // 重写基类方法
        std::vector<std::vector<float>> Infer(const std::vector<int>& textIds, int textLength);
        void LoadToken();
        std::vector<int> TokensToIds(const std::vector<std::string>& tokens);
        std::string IdToToken(int id);

    private:
        std::mutex mTokenMapMutex;
        std::mutex mInferMutex;  // 保护Infer函数调用
        std::string mTokenListPath;
        std::map<std::string, int> mTokenMap;
        std::map<int, std::string> mTokenId;
        bool mTokenLoaded = false;
        const static int mCachePopTriggerLimit = 200;
        int mModelSeqLen = -1;  // 模型输入序列长度（从模型输入 shape 查询，punc 为 220），-1 表示未查询

        // 查询并缓存模型输入 "text" 的序列长度（shape[1]），失败返回 -1
        int GetModelSeqLen();
    };

    // 辅助函数
    // 模拟Python的split_to_mini_sentence函数
    template <typename T>
    std::vector<std::vector<T>> SplitToMiniSentence(const std::vector<T>& input, int splitSize) {
        std::vector<std::vector<T>> result;

        for (size_t i = 0; i < input.size(); i += splitSize) {
            size_t end = std::min(i + splitSize, input.size());
            std::vector<T> chunk(input.begin() + i, input.begin() + end);
            result.push_back(chunk);
        }

        return result;
    }

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_PUNC_WORKER_H
