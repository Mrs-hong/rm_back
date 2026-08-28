/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * vad_worker.cpp - Silero VAD 语音活动检测 Worker 实现 (TCIM 版本)
 */

#include "models_hm/worker/audio_feature.h"
#include "models_hm/worker/vad_worker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

#include "onnxruntime_c_api.h"
#include "onnxruntime_cxx_api.h"

#include "common/config_manager.h"
#include "common/logger.h"

namespace qifeng {

    VADWorker::VADWorker(const std::string& modelPath, int tpuId, IOMode ioMode)
        : ModelWorker(modelPath, tpuId, ioMode), mSrPerMs(kSampleRate / 1000), mContext(kContextSamples, 0.0f),
          mState(kStateSize, 0.0f), mThreshold(0.5f), mMinSilenceSamples(mSrPerMs * 100),
          mMinSilenceSamplesAtMaxSpeech(mSrPerMs * 98), mMinSpeechSamples(mSrPerMs * 250),
          mMaxSpeechSamples(std::numeric_limits<int64_t>::max()), mSpeechPadSamples(mSrPerMs * 30),
          mSpeechPadEndSamples(mSrPerMs * 0), mTriggered(false), mTempEnd(0), mCurrentSample(0), mPrevEnd(0),
          mNextStart(0), mEffectiveData(kEffectiveWindowSize, 0.0f), mOutputProb(1, 0.0f),
          mOutputStateN(kStateSize, 0.0f), mTensorsReady(false) {
        try {
            // 从配置读取 VAD 阈值等参数（可选）
            mThreshold =
                static_cast<float>(ConfigManager::GetInstance().GetDouble("models.vad", "silero_threshold", 0.5));
            int minSilenceMs = ConfigManager::GetInstance().GetInt("models.vad", "silero_min_silence_ms", 100);
            int minSpeechMs = ConfigManager::GetInstance().GetInt("models.vad", "silero_min_speech_ms", 250);
            int speechPadStartMs = ConfigManager::GetInstance().GetInt("models.vad", "silero_speech_pad_start_ms", 30);
            int speechPadEndMs = ConfigManager::GetInstance().GetInt("models.vad", "silero_speech_pad_end_ms", 0);
            mMinSilenceSamples = mSrPerMs * minSilenceMs;
            // mMinSilenceSamplesAtMaxSpeech keeps fixed 98ms (mSrPerMs * 98), matching reference implementation
            mMinSpeechSamples = mSrPerMs * minSpeechMs;
            mSpeechPadSamples = mSrPerMs * speechPadStartMs;
            mSpeechPadEndSamples = mSrPerMs * speechPadEndMs;

            // 根据模型文件后缀选择后端：.onnx -> CPU (onnxruntime)，否则 -> TCIM (TPU)
            const bool isOnnx = (modelPath.size() >= 5) && (modelPath.compare(modelPath.size() - 5, 5, ".onnx") == 0);
            if (isOnnx) {
                mBackend = Backend::ONNX_CPU;
                if (InitOnnxSession(modelPath)) {
                    mBackendReady = true;
                    SLOG_INFO << "VADWorker(Silero/ONNX): CPU 后端初始化成功, model=" << modelPath;
                } else {
                    SLOG_ERROR << "VADWorker(Silero/ONNX): CPU 后端初始化失败";
                }
            } else if (IsReady()) {
                mBackend = Backend::TCIM;
                // 先查询输入/输出节点名，InitPersistentTensors 依赖这些
                mInputNames = GetInputNames(mDefaultGraphName);
                mOutputNames = GetOutputNames(mDefaultGraphName);
                mTensorsReady = InitPersistentTensors();
                mBackendReady = mTensorsReady;

                // 打印模型输入/输出节点信息（调试用）
                std::string inStr, outStr;
                for (size_t i = 0; i < mInputNames.size(); ++i) {
                    inStr += (i ? "," : "") + mInputNames[i];
                }
                for (size_t i = 0; i < mOutputNames.size(); ++i) {
                    outStr += (i ? "," : "") + mOutputNames[i];
                }
                SLOG_INFO << "VADWorker(Silero/TCIM): 初始化成功, model=" << modelPath << ", inputs=[" << inStr
                          << "], outputs=[" << outStr << "], tensorsReady=" << mTensorsReady
                          << ", backendReady=" << mBackendReady;
            } else {
                SLOG_ERROR << "VADWorker(Silero/TCIM): 初始化失败 - ModelWorker::IsReady() "
                              "返回 false";
            }
        } catch (const std::exception& e) {
            SLOG_ERROR << "VADWorker: 初始化失败 - " << e.what();
        } catch (...) {
            SLOG_ERROR << "VADWorker: 初始化失败 - 未知异常";
        }
    }

    bool VADWorker::InitPersistentTensors() {
        // TCIM Module 内部托管输入输出张量的设备内存
        // 这里只需准备好 host 缓冲区，推理时通过 SetInput/GetOutput 交互
        // Silero bmodel 已将 sr 烘焙为常量，仅有 input/state 两个输入
        // output: [1] float32, stateN: [2,1,128] float32
        if (mInputNames.size() < 2 || mOutputNames.size() < 2) {
            SLOG_ERROR << "VADWorker(Silero/TCIM): InitPersistentTensors 失败 - 输入输出节点数不足"
                       << " (inputs=" << mInputNames.size() << ", outputs=" << mOutputNames.size() << ")";
            return false;
        }

        // 预分配 host 缓冲区
        mEffectiveData.assign(kEffectiveWindowSize, 0.0f);
        mState.assign(kStateSize, 0.0f);
        mOutputProb.assign(1, 0.0f);
        mOutputStateN.assign(kStateSize, 0.0f);
        return true;
    }

    VADWorker::~VADWorker() {
        try {
            CleanupOnnx();
            SLOG_INFO << "VADWorker: 析构函数调用";
        } catch (...) {
            SLOG_ERROR << "VADWorker: 析构函数异常";
        }
    }

    FloatMatrix VADWorker::VadFeature(const std::vector<float>& audioSample) {
        return AudioFeature::VadFeature(audioSample);
    }

    FloatMatrix VADWorker::AsrFeature(const std::vector<float>& audioSample) {
        return AudioFeature::AsrFeature(audioSample);
    }

    FloatMatrix VADWorker::VpFeature(const std::vector<float>& audioSample) {
        return AudioFeature::VpFeature(audioSample);
    }

    void VADWorker::ResetStates() {
        std::memset(mState.data(), 0, mState.size() * sizeof(float));
        mTriggered = false;
        mTempEnd = 0;
        mCurrentSample = 0;
        mPrevEnd = 0;
        mNextStart = 0;
        mSpeeches.clear();
        mCurrentSpeech.clear();
        std::fill(mContext.begin(), mContext.end(), 0.0f);
    }

    void VADWorker::Predict(const std::vector<float>& dataChunk) {
        if (!mBackendReady) {
            SLOG_ERROR << "VADWorker: Predict 失败 - 后端未初始化";
            return;
        }

        // 构造有效窗口：上下文 + 当前数据块
        std::copy(mContext.begin(), mContext.end(), mEffectiveData.begin());
        std::copy(dataChunk.begin(), dataChunk.end(), mEffectiveData.begin() + kContextSamples);

        try {
            // ── 按后端执行推理，结果写入 mOutputProb / mOutputStateN ──
            int ret = (mBackend == Backend::ONNX_CPU) ? PredictOnnx() : PredictTCIM();
            THROW_IF(ret != ERR_OK, ERR_RUNTIME);

            // 读取输出：语音概率和新状态
            float speechProb = mOutputProb[0];
            std::copy(mOutputStateN.begin(), mOutputStateN.end(), mState.begin());

            // 更新当前样本位置
            mCurrentSample += static_cast<unsigned int>(kWindowSizeSamples);

            // === Silero VAD 状态机逻辑 ===

            // 语音概率超过阈值 → 检测到语音
            if (speechProb >= mThreshold) {
                if (mTempEnd != 0) {
                    mTempEnd = 0;
                    if (mNextStart < mPrevEnd) {
                        mNextStart = static_cast<int>(mCurrentSample) - kWindowSizeSamples;
                    }
                }
                if (!mTriggered) {
                    mTriggered = true;
                    mCurrentSpeech = {static_cast<int>(mCurrentSample) - kWindowSizeSamples, -1};
                }
                std::copy(mEffectiveData.end() - kContextSamples, mEffectiveData.end(), mContext.begin());
                return;
            }

            // 已触发但超过最大语音长度
            if (mTriggered && !mCurrentSpeech.empty()) {
                int64_t currentSpeechLen = static_cast<int64_t>(mCurrentSample) - mCurrentSpeech[0];
                if (currentSpeechLen > mMaxSpeechSamples) {
                    if (mPrevEnd > 0) {
                        mCurrentSpeech[1] = mPrevEnd;
                        mSpeeches.push_back(mCurrentSpeech);
                        mCurrentSpeech.clear();
                        if (mNextStart < mPrevEnd) {
                            mTriggered = false;
                        } else {
                            mCurrentSpeech = {mNextStart, -1};
                        }
                        mPrevEnd = 0;
                        mNextStart = 0;
                        mTempEnd = 0;
                    } else {
                        mCurrentSpeech[1] = static_cast<int>(mCurrentSample);
                        mSpeeches.push_back(mCurrentSpeech);
                        mCurrentSpeech.clear();
                        mPrevEnd = 0;
                        mNextStart = 0;
                        mTempEnd = 0;
                        mTriggered = false;
                    }
                    std::copy(mEffectiveData.end() - kContextSamples, mEffectiveData.end(), mContext.begin());
                    return;
                }
            }

            // 边界区域（threshold - kBoundaryOffset ~ threshold），不做任何判断
            if ((speechProb >= (mThreshold - kBoundaryOffset)) && (speechProb < mThreshold)) {
                std::copy(mEffectiveData.end() - kContextSamples, mEffectiveData.end(), mContext.begin());
                return;
            }

            // 语音概率低于 threshold - kBoundaryOffset → 可能是静音或语音结束
            if (speechProb < (mThreshold - kBoundaryOffset)) {
                if (mTriggered) {
                    if (mTempEnd == 0) {
                        mTempEnd = mCurrentSample;
                    }
                    if (mCurrentSample - mTempEnd > static_cast<unsigned int>(mMinSilenceSamplesAtMaxSpeech)) {
                        mPrevEnd = static_cast<int>(mTempEnd);
                    }
                    if ((mCurrentSample - mTempEnd) >= static_cast<unsigned int>(mMinSilenceSamples)) {
                        mCurrentSpeech[1] = static_cast<int>(mTempEnd);
                        if (mCurrentSpeech[1] - mCurrentSpeech[0] > mMinSpeechSamples) {
                            mSpeeches.push_back(mCurrentSpeech);
                        }
                        mCurrentSpeech.clear();
                        mPrevEnd = 0;
                        mNextStart = 0;
                        mTempEnd = 0;
                        mTriggered = false;
                    }
                }
                std::copy(mEffectiveData.end() - kContextSamples, mEffectiveData.end(), mContext.begin());
                return;
            }
        } catch (const CommonException& e) {
            SLOG_ERROR << "VADWorker(Silero/TCIM): Predict 推理失败 - CommonException errCode=" << e.errorCode
                       << " line=" << e.line << " func=" << e.func;
        } catch (const std::exception& e) {
            SLOG_ERROR << "VADWorker(Silero/TCIM): Predict 推理失败 - " << e.what();
        } catch (...) {
            SLOG_ERROR << "VADWorker(Silero/TCIM): Predict 推理失败 - 未知异常";
        }
    }

    int VADWorker::PredictTCIM() {
        // Silero VAD 的两个输入：input [1, 576], state [2, 1, 128]
        // 两个输出：output [1], stateN [2, 1, 128]
        ModelInput input;
        ModelOutput output;

        // 设置输入数据（使用查询到的节点名）
        input.data[mInputNames[0]] = mEffectiveData.data();
        input.shapes[mInputNames[0]] = {1, kEffectiveWindowSize};

        input.data[mInputNames[1]] = mState.data();
        input.shapes[mInputNames[1]] = {2, 1, 128};

        // 设置输出缓冲区
        output.data[mOutputNames[0]] = mOutputProb.data();
        output.shapes[mOutputNames[0]] = {1};

        output.data[mOutputNames[1]] = mOutputStateN.data();
        output.shapes[mOutputNames[1]] = {2, 1, 128};

        int ret = Process(input, output);
        return ret;
    }

    int VADWorker::PredictOnnx() {
        if (!mOnnxSession) {
            SLOG_ERROR << "VADWorker(Silero/ONNX): PredictOnnx 失败 - 会话未初始化";
            return ERR_RUNTIME;
        }

        std::lock_guard<std::mutex> lock(mOnnxMutex);

        try {
            // 输入张量：input [1, 576], state [2, 1, 128]
            std::vector<int64_t> inputShape = {1, kEffectiveWindowSize};
            std::vector<int64_t> stateShape = {2, 1, 128};
            std::vector<int64_t> outputShape = {1, 1};
            std::vector<int64_t> stateOutShape = {2, 1, 128};

            std::vector<const char*> inputNames = {mOnnxInputName.c_str(), mOnnxStateName.c_str()};
            std::vector<const char*> outputNames = {mOnnxOutputName.c_str(), mOnnxStateOutName.c_str()};

            std::vector<Ort::Value> inputTensors;
            inputTensors.emplace_back(Ort::Value::CreateTensor<float>(
                *mOnnxMemoryInfo, mEffectiveData.data(), mEffectiveData.size(), inputShape.data(), inputShape.size()));
            inputTensors.emplace_back(Ort::Value::CreateTensor<float>(*mOnnxMemoryInfo, mState.data(), mState.size(),
                                                                      stateShape.data(), stateShape.size()));

            auto outputTensors = mOnnxSession->Run(Ort::RunOptions {nullptr}, inputNames.data(), inputTensors.data(),
                                                   inputTensors.size(), outputNames.data(), outputNames.size());
            if (outputTensors.size() < 2) {
                SLOG_ERROR << "VADWorker(Silero/ONNX): PredictOnnx 输出数量异常 " << outputTensors.size();
                return ERR_RUNTIME;
            }

            // 输出：output [1,1] 的语音概率，stateN [2,1,128] 的新状态
            const float* probPtr = outputTensors[0].GetTensorData<float>();
            const float* stateOutPtr = outputTensors[1].GetTensorData<float>();
            if (!probPtr || !stateOutPtr) {
                SLOG_ERROR << "VADWorker(Silero/ONNX): PredictOnnx 输出指针为空";
                return ERR_RUNTIME;
            }
            mOutputProb[0] = probPtr[0];
            std::copy(stateOutPtr, stateOutPtr + kStateSize, mOutputStateN.begin());
            return ERR_OK;
        } catch (const Ort::Exception& e) {
            SLOG_ERROR << "VADWorker(Silero/ONNX): PredictOnnx 推理失败 - " << e.what();
            return ERR_RUNTIME;
        }
    }

    bool VADWorker::InitOnnxSession(const std::string& modelPath) {
        try {
            std::lock_guard<std::mutex> lock(mOnnxMutex);

            // 仅使用 CPU EP（Silero VAD 小模型，CPU 足够）
            mOnnxEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "vad_inference");
            mOnnxSessionOptions = std::make_unique<Ort::SessionOptions>();
            mOnnxSessionOptions->SetIntraOpNumThreads(1);
            mOnnxSessionOptions->SetInterOpNumThreads(1);
            mOnnxMemoryInfo =
                std::make_unique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

            mOnnxSession = std::make_unique<Ort::Session>(*mOnnxEnv, modelPath.c_str(), *mOnnxSessionOptions);

            // 获取输入/输出名
            Ort::AllocatorWithDefaultOptions allocator;
            size_t numInputs = mOnnxSession->GetInputCount();
            for (size_t i = 0; i < numInputs; ++i) {
                auto name = mOnnxSession->GetInputNameAllocated(i, allocator);
                std::string s(name.get());
                if (s == "input")
                    mOnnxInputName = s;
                else if (s == "state")
                    mOnnxStateName = s;
            }
            size_t numOutputs = mOnnxSession->GetOutputCount();
            for (size_t i = 0; i < numOutputs; ++i) {
                auto name = mOnnxSession->GetOutputNameAllocated(i, allocator);
                std::string s(name.get());
                if (s == "output")
                    mOnnxOutputName = s;
                else if (s == "stateN")
                    mOnnxStateOutName = s;
            }
            if (mOnnxInputName.empty() || mOnnxStateName.empty() || mOnnxOutputName.empty() ||
                mOnnxStateOutName.empty()) {
                SLOG_ERROR << "VADWorker(Silero/ONNX): 输入/输出节点名不完整 inputs=" << numInputs
                           << " outputs=" << numOutputs;
                CleanupOnnx();
                return false;
            }

            // 预分配推理缓冲
            mEffectiveData.assign(kEffectiveWindowSize, 0.0f);
            mState.assign(kStateSize, 0.0f);
            mOutputProb.assign(1, 0.0f);
            mOutputStateN.assign(kStateSize, 0.0f);

            SLOG_INFO << "VADWorker(Silero/ONNX): onnxruntime 会话初始化成功";
            return true;
        } catch (const Ort::Exception& e) {
            SLOG_ERROR << "VADWorker(Silero/ONNX): onnxruntime 初始化失败 - " << e.what();
            CleanupOnnx();
            return false;
        } catch (const std::exception& e) {
            SLOG_ERROR << "VADWorker(Silero/ONNX): onnxruntime 初始化失败 - " << e.what();
            CleanupOnnx();
            return false;
        }
    }

    void VADWorker::CleanupOnnx() {
        std::lock_guard<std::mutex> lock(mOnnxMutex);
        mOnnxSession.reset();
        mOnnxMemoryInfo.reset();
        mOnnxSessionOptions.reset();
        mOnnxEnv.reset();
    }

    std::vector<std::vector<std::vector<int>>> VADWorker::InferenceBModel(const FloatMatrix& /*speechFeat*/,
                                                                          const std::vector<float>& audioSample) {
        std::lock_guard<std::mutex> lock(mInferenceMutex);

        if (audioSample.empty()) {
            SLOG_WARN << "VADWorker: InferenceBModel 输入音频为空";
            return {};
        }

        if (!mBackendReady) {
            SLOG_ERROR << "VADWorker: 后端未初始化";
            return {};
        }

        SLOG_INFO << "VADWorker: InferenceBModel 开始, audio_sample=" << audioSample.size() << "样本";
        if (mBackend == Backend::TCIM) {
            SLOG_INFO << "VADWorker: 后端=TCIM(TPU)";
        } else {
            SLOG_INFO << "VADWorker: 后端=ONNX_CPU";
        }

        ResetStates();

        const int audioLength = static_cast<int>(audioSample.size());

        // 预分配帧缓冲区（避免循环内每帧堆分配）
        std::vector<float> chunk(kWindowSizeSamples);

        // 逐帧处理音频
        for (int j = 0; j + kWindowSizeSamples <= audioLength; j += kWindowSizeSamples) {
            std::copy(audioSample.begin() + j, audioSample.begin() + j + kWindowSizeSamples, chunk.begin());
            Predict(chunk);
        }

        // 处理最后一个未完成段
        if (!mCurrentSpeech.empty() && mCurrentSpeech[0] >= 0 && mCurrentSpeech[1] < 0) {
            mCurrentSpeech[1] = audioLength;
            mSpeeches.push_back(mCurrentSpeech);
            mCurrentSpeech.clear();
            mPrevEnd = 0;
            mNextStart = 0;
            mTempEnd = 0;
            mTriggered = false;
        }

        // 将采样级别的时间戳转换为毫秒（应用起点回溯/终点前向填充，并限制在音频范围内）
        std::vector<std::vector<int>> resultBatch;
        for (auto& seg : mSpeeches) {
            if (seg.size() >= 2) {
                int startSample = std::max(0, seg[0] - mSpeechPadSamples);
                int endSample = std::min(audioLength, seg[1] + mSpeechPadEndSamples);
                int startMs = static_cast<int>(static_cast<int64_t>(startSample) * 1000 / kSampleRate);
                int endMs = static_cast<int>(static_cast<int64_t>(endSample) * 1000 / kSampleRate);
                resultBatch.push_back({startMs, endMs});
            }
        }

        std::vector<std::vector<std::vector<int>>> segments;
        if (!resultBatch.empty()) {
            segments.push_back(std::move(resultBatch));
        }

        SLOG_INFO << "VADWorker(Silero/TCIM): InferenceBModel 完成, segments=" << segments.size();
        return segments;
    }

    std::vector<std::vector<int>> VADWorker::Inference(const FloatMatrix& speechFeat,
                                                       const std::vector<float>& audioSample) {
        std::vector<std::vector<int>> result;
        const std::vector<std::vector<std::vector<int>>> batchSegments = InferenceBModel(speechFeat, audioSample);
        for (const auto& batch : batchSegments) {
            for (const auto& seg : batch) {
                result.push_back(seg);
            }
        }
        return result;
    }

}  // namespace qifeng
