/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "models/worker/qwen_asr_worker.h"

#include <cstring>

#include "common/config_manager.h"
#include "common/logger.h"

namespace qifeng {

    QwenASRWorker::QwenASRWorker(const std::string& modelPath, int tpuId, IOMode ioMode)
        : ModelWorker(modelPath, tpuId, ioMode) {
        try {
            SLOG_INFO << "QwenASRWorker: Constructor called";
        } catch (const std::exception& e) {
            SLOG_ERROR << "QwenASRWorker: Exception in constructor: " << e.what();
        } catch (...) {
            SLOG_ERROR << "QwenASRWorker: Unknown exception in constructor";
        }
    }

    QwenASRWorker::~QwenASRWorker() {
        try {
            SLOG_INFO << "QwenASRWorker: Destructor called";
            mModel.reset();
        } catch (const std::exception& e) {
            SLOG_ERROR << "QwenASRWorker: Exception in destructor: " << e.what();
        } catch (...) {
            SLOG_ERROR << "QwenASRWorker: Unknown exception in destructor";
        }
    }

    bool QwenASRWorker::Initialize(const std::string& configSection) {
        if (mQwenInitialized) {
            SLOG_WARN << "QwenASRWorker: already initialized";
            return true;
        }

        auto& cfg = ConfigManager::GetInstance();
        std::string tokenizerPath = cfg.GetString(configSection, "tokenizer_path", "");
        std::string configPath = cfg.GetString(configSection, "config_path", "");

        if (tokenizerPath.empty() || configPath.empty()) {
            SLOG_ERROR << "QwenASRWorker: missing config items under section [" << configSection
                       << "]: tokenizer_path/config_path";
            return false;
        }

        return Initialize(tokenizerPath, configPath);
    }

    void QwenASRWorker::ResetModelContext() {
        if (mModelContext) {
            mModelContext->Reset();
        }
    }

    void QwenASRWorker::CreateContext() {
        if (!mModelContext) {
            mModelContext = mModel->CreateContext();
        }
    }

    bool QwenASRWorker::Initialize(const std::string& tokenizerPath, const std::string& configPath) {
        if (mQwenInitialized) {
            return true;
        }

        try {
            // 使用基类 ModelWorker 已创建的 mpBmrt 和 mHandle 构造 RuntimeWrapper
            mModel = qifeng::asr::CreateQwen3ASRBmruntimeModel(mHandle, mpBmrt, tokenizerPath, configPath, mModelPath);
            mModelContext = mModel->CreateContext();
            // mModel 内部持有 RuntimeWrapper 的 shared_ptr<Impl> 拷贝，
            // mModel 析构时 Impl 析构会调用 bmrt_destroy 释放 mpBmrt
            // 基类 ~ModelWorker 中也会检查 mpBmrt，因此将其置空避免二次释放
            mpBmrt = nullptr;
            mQwenInitialized = true;
            SLOG_DEBUG << "QwenASRWorker: initialized successfully, model=" << mModel->ModelName()
                       << " engine=" << mModel->EngineName();
            return true;
        } catch (const std::exception& e) {
            SLOG_ERROR << "QwenASRWorker: initialization failed, reason=" << e.what();
            mQwenInitialized = false;
            mModel.reset();
            return false;
        }
    }

    // ==================== 推理接口实现 ====================

    qifeng::aas::StreamInferContent QwenASRWorker::Inference(const std::string& audioPath) {
        if (!mQwenInitialized) {
            throw std::runtime_error("QwenASRWorker: model not initialized");
        }
        return mModelContext->Generate(qifeng::aas::AudioFile {audioPath}, false);
    }

    qifeng::aas::StreamInferContent QwenASRWorker::Inference(std::vector<float>& pcmData, bool isOnline) {
        if (!mQwenInitialized) {
            throw std::runtime_error("QwenASRWorker: model not initialized");
        }
        qifeng::aas::PcmData pcmRequest;
        pcmRequest.size = static_cast<int32_t>(pcmData.size() * sizeof(float));
        std::vector<uint8_t> pcmRequestBytes(static_cast<size_t>(pcmRequest.size));
        std::memcpy(pcmRequestBytes.data(), pcmData.data(), static_cast<size_t>(pcmRequest.size));
        pcmRequest.raw = pcmRequestBytes.data();

        // 获取采样率、声道数、位深
        int sampleRate = ConfigManager::GetInstance().GetInt("aas", "sample_rate");
        int channels = ConfigManager::GetInstance().GetInt("aas", "channels");
        int bitDepth = ConfigManager::GetInstance().GetInt("aas", "bit_depth");

        pcmRequest.sampleRate = static_cast<uint32_t>(sampleRate);
        pcmRequest.channels = static_cast<uint16_t>(channels);
        pcmRequest.bitDepth = static_cast<uint16_t>(bitDepth);
        return mModelContext->Generate(qifeng::aas::Request {std::move(pcmRequest)}, isOnline);
    }

    void QwenASRWorker::StreamGenerate(const std::string& audioPath, qifeng::asr::StreamGenerateCallback callback) {
        if (!mQwenInitialized) {
            callback(qifeng::asr::StreamError {"QwenASRWorker: model not initialized"});
            return;
        }
        mModelContext->StreamGenerate(qifeng::aas::AudioFile {audioPath}, std::move(callback));
    }

    void QwenASRWorker::StreamGenerate(const std::vector<float>& pcmData,
                                       qifeng::asr::StreamGenerateCallback callback) {
        if (!mQwenInitialized) {
            callback(qifeng::asr::StreamError {"QwenASRWorker: model not initialized"});
            return;
        }
        qifeng::aas::PcmData pcmRequest;
        pcmRequest.size = static_cast<int32_t>(pcmData.size() * sizeof(float));
        std::memcpy(pcmRequest.raw, pcmData.data(), static_cast<size_t>(pcmRequest.size));
        // 获取采样率、声道数、位深
        int sampleRate = ConfigManager::GetInstance().GetInt("aas", "sample_rate");
        int channels = ConfigManager::GetInstance().GetInt("aas", "channels");
        int bitDepth = ConfigManager::GetInstance().GetInt("aas", "bit_depth");

        pcmRequest.sampleRate = static_cast<uint32_t>(sampleRate);
        pcmRequest.channels = static_cast<uint16_t>(channels);
        pcmRequest.bitDepth = static_cast<uint16_t>(bitDepth);

        mModelContext->StreamGenerate(qifeng::aas::Request {std::move(pcmRequest)}, std::move(callback));
    }

}  // namespace qifeng
