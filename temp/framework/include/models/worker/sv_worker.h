/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_SV_WORKER_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_SV_WORKER_H

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/config_manager.h"
#include "models/worker/worker.h"

namespace qifeng {

    class SVWorker : public ModelWorker {
    public:
        SVWorker(const std::string& modelPath, int tpuId = 0, IOMode ioMode = IOMode::SYSIO);
        ~SVWorker();

        FloatMatrix InferenceBModel(const FloatMatrix& speechFeat);

        // 批量推理：输入多个 Fbank 特征矩阵，每个为 [frames, 80]
        // 内部按 batch=16 分组，短帧补零对齐到组内最大帧数
        // 返回 [N, 512] 的 embedding 矩阵
        FloatMatrix InferenceBModelBatch(const std::vector<FloatMatrix>& speechFeats);

    private:
        // 输入节点名称
        std::string mInNameSpeech;
        std::vector<std::string> mInNamesCache;

        // 输出节点名称
        std::string mOutNameEmbedding;
        std::vector<std::string> mOutNamesCache;

        // 线程安全保护
        std::mutex mSpeakersMutex;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_SV_WORKER_H