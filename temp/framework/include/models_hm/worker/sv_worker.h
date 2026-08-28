/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_SV_WORKER_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_SV_WORKER_H

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/config_manager.h"
#include "models_hm/worker/worker.h"

namespace qifeng {

    // 声纹识别 Worker (TCIM 版本)
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

        // .hmm 静态 shape 配置（从 config.yaml models.sv 读取）
        int mStaticBatch = 1;     // 静态 batch size
        int mMaxFrames = 298;     // 静态输入维度的帧数
        int mFeatureDim = 80;     // Fbank 特征维度
        int mEmbeddingDim = 512;  // 输出 embedding 维度

        // 线程安全保护
        std::mutex mSpeakersMutex;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_SV_WORKER_H
