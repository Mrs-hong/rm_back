/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_QWEN_ASR_WORKER_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_QWEN_ASR_WORKER_H

#include <memory>
#include <string>
#include <vector>

#include "aas/aas_callback.h"
#include "asr/model.h"
#include "models/worker/worker.h"

namespace qifeng {

    class QwenASRWorker : public ModelWorker {
    public:
        QwenASRWorker(const std::string& modelPath, int tpuId = 0, IOMode ioMode = IOMode::SYSIO);
        ~QwenASRWorker();

        QwenASRWorker(const QwenASRWorker&) = delete;
        QwenASRWorker& operator=(const QwenASRWorker&) = delete;
        QwenASRWorker(QwenASRWorker&&) = delete;
        QwenASRWorker& operator=(QwenASRWorker&&) = delete;

        bool Initialize(const std::string& configSection = "asr.model");
        bool Initialize(const std::string& tokenizerPath, const std::string& configPath);

        qifeng::aas::StreamInferContent Inference(const std::string& audioPath);
        qifeng::aas::StreamInferContent Inference(std::vector<float>& pcmData, bool isOnline);

        void StreamGenerate(const std::string& audioPath, qifeng::asr::StreamGenerateCallback callback);
        void StreamGenerate(const std::vector<float>& pcmData, qifeng::asr::StreamGenerateCallback callback);
        void ResetModelContext();
        void CreateContext();

    private:
        std::shared_ptr<qifeng::asr::Model> mModel;
        std::shared_ptr<qifeng::asr::ModelContext> mModelContext;
        bool mQwenInitialized = false;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_QWEN_ASR_WORKER_H
