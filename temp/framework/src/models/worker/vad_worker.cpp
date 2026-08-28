/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * vad_worker.cpp - Silero VAD 语音活动检测 Worker 实现
 */

#include "models/worker/vad_worker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

#include "common/config_manager.h"
#include "common/logger.h"
#include "models/worker/audio_feature.h"

namespace qifeng {

    namespace {
        // 构造 bm_shape_t 辅助函数
        bm_shape_t makeShape(const std::vector<int>& dims) {
            bm_shape_t shape {};
            shape.num_dims = static_cast<int>(dims.size());
            for (size_t i = 0; i < dims.size() && i < BM_MAX_DIMS_NUM; ++i) {
                shape.dims[i] = dims[i];
            }
            return shape;
        }
    }  // namespace

    VADWorker::VADWorker(const std::string& modelPath, int tpuId, IOMode ioMode)
        : ModelWorker(modelPath, tpuId, ioMode), mSrPerMs(kSampleRate / 1000), mContext(kContextSamples, 0.0f),
          mState(kStateSize, 0.0f), mThreshold(0.5f), mMinSilenceSamples(mSrPerMs * 100),
          mMinSilenceSamplesAtMaxSpeech(mSrPerMs * 98), mMinSpeechSamples(mSrPerMs * 250),
          mMaxSpeechSamples(std::numeric_limits<int64_t>::max()), mSpeechPadSamples(mSrPerMs * 30),
          mSpeechPadEndSamples(mSrPerMs * 0), mTriggered(false), mTempEnd(0), mCurrentSample(0), mPrevEnd(0),
          mNextStart(0), mEffectiveData(kEffectiveWindowSize, 0.0f), mTensorsReady(false) {
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

            if (IsReady()) {
                mTensorsReady = InitPersistentTensors();

                // 打印模型输入/输出节点信息（调试用）
                auto inputNames = GetInputNames(mDefaultGraphName);
                auto outputNames = GetOutputNames(mDefaultGraphName);
                std::string inStr, outStr;
                for (size_t i = 0; i < inputNames.size(); ++i) {
                    inStr += (i ? "," : "") + inputNames[i];
                }
                for (size_t i = 0; i < outputNames.size(); ++i) {
                    outStr += (i ? "," : "") + outputNames[i];
                }
                SLOG_INFO << "VADWorker(Silero): 初始化成功, bmodel=" << modelPath << ", inputs=[" << inStr
                          << "], outputs=[" << outStr << "], tensorsReady=" << mTensorsReady;
            } else {
                SLOG_ERROR << "VADWorker(Silero): 初始化失败 - ModelWorker::IsReady() "
                              "返回 false";
            }
        } catch (const std::exception& e) {
            SLOG_ERROR << "VADWorker(Silero): 初始化失败 - " << e.what();
        } catch (...) {
            SLOG_ERROR << "VADWorker(Silero): 初始化失败 - 未知异常";
        }
    }

    bool VADWorker::InitPersistentTensors() {
        // Silero bmodel 已将 sr 烘焙为常量，仅有 input/state 两个输入
        // output_Reshape: [1] float32, stateN_Concat: [2, 1, 128] float32
        // 注意：BmTensor 构造函数在 bmrt_tensor 失败时不抛异常，仅置 mOwnsMem=false，
        //       必须通过 ownsMem() 判断设备内存是否真正分配成功
        mInputTensor = std::make_unique<BmTensor>(mHandle, mpBmrt, BM_FLOAT32, makeShape({1, kEffectiveWindowSize}));
        mStateTensor = std::make_unique<BmTensor>(mHandle, mpBmrt, BM_FLOAT32, makeShape({2, 1, 128}));
        mOutputTensor = std::make_unique<BmTensor>(mHandle, mpBmrt, BM_FLOAT32, makeShape({1}));
        mStateNTensor = std::make_unique<BmTensor>(mHandle, mpBmrt, BM_FLOAT32, makeShape({2, 1, 128}));
        return mInputTensor->ownsMem() && mStateTensor->ownsMem() && mOutputTensor->ownsMem() &&
               mStateNTensor->ownsMem();
    }

    VADWorker::~VADWorker() {
        try {
            SLOG_INFO << "VADWorker(Silero): 析构函数调用";
        } catch (...) {
            SLOG_ERROR << "VADWorker(Silero): 析构函数异常";
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
        if (!mTensorsReady) {
            SLOG_ERROR << "VADWorker(Silero): Predict 失败 - 张量未初始化";
            return;
        }

        // 构造有效窗口：上下文 + 当前数据块
        std::copy(mContext.begin(), mContext.end(), mEffectiveData.begin());
        std::copy(dataChunk.begin(), dataChunk.end(), mEffectiveData.begin() + kContextSamples);

        try {
            // 填充输入张量：系统内存 → 设备内存
            bm_status_t st = bm_memcpy_s2d_partial(mHandle, mInputTensor->tensor.device_mem, mEffectiveData.data(),
                                                   static_cast<unsigned int>(kEffectiveWindowSize * sizeof(float)));
            THROW_IF(st != BM_SUCCESS, ERR_RUNTIME);

            st = bm_memcpy_s2d_partial(mHandle, mStateTensor->tensor.device_mem, mState.data(),
                                       static_cast<unsigned int>(kStateSize * sizeof(float)));
            THROW_IF(st != BM_SUCCESS, ERR_RUNTIME);

            // 构建张量数组（顺序与 bmodel 的 input/output 节点顺序一致）
            bm_tensor_t inputs[2] = {mInputTensor->tensor, mStateTensor->tensor};
            bm_tensor_t outputs[2] = {mOutputTensor->tensor, mStateNTensor->tensor};

            // 执行推理：bmrt_launch_tensor_ex + bm_thread_sync
            bool launchOk =
                bmrt_launch_tensor_ex(mpBmrt, mDefaultGraphName.c_str(), inputs, 2, outputs, 2, true, false);
            THROW_IF(!launchOk, ERR_RUNTIME);

            st = bm_thread_sync(mHandle);
            THROW_IF(st != BM_SUCCESS, ERR_RUNTIME);

            // 读取输出：设备内存 → 系统内存
            float speechProb = 0.0f;
            st = bm_memcpy_d2s_partial(mHandle, &speechProb, mOutputTensor->tensor.device_mem, sizeof(float));
            THROW_IF(st != BM_SUCCESS, ERR_RUNTIME);

            st = bm_memcpy_d2s_partial(mHandle, mState.data(), mStateNTensor->tensor.device_mem,
                                       static_cast<unsigned int>(kStateSize * sizeof(float)));
            THROW_IF(st != BM_SUCCESS, ERR_RUNTIME);

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
            // 注意：必须先检查 mTriggered 和 mCurrentSpeech 非空，才能访问 mCurrentSpeech[0]
            // 否则当 speechProb < mThreshold 时会跳过上面的初始化，导致 mCurrentSpeech 为空，
            // 这里访问 mCurrentSpeech[0] 会段错误
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
            SLOG_ERROR << "VADWorker(Silero): Predict 推理失败 - CommonException errCode=" << e.errorCode
                       << " line=" << e.line << " func=" << e.func;
        } catch (const std::exception& e) {
            SLOG_ERROR << "VADWorker(Silero): Predict 推理失败 - " << e.what();
        } catch (...) {
            SLOG_ERROR << "VADWorker(Silero): Predict 推理失败 - 未知异常";
        }
    }

    std::vector<std::vector<std::vector<int>>> VADWorker::InferenceBModel(const FloatMatrix& /*speechFeat*/,
                                                                          const std::vector<float>& audioSample) {
        std::lock_guard<std::mutex> lock(mInferenceMutex);

        if (audioSample.empty()) {
            SLOG_WARN << "VADWorker(Silero): InferenceBModel 输入音频为空";
            return {};
        }

        if (!IsReady()) {
            SLOG_ERROR << "VADWorker(Silero): 模型未初始化";
            return {};
        }

        SLOG_INFO << "VADWorker(Silero): InferenceBModel 开始, audio_sample=" << audioSample.size() << "样本";

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

        SLOG_INFO << "VADWorker(Silero): InferenceBModel 完成, segments=" << segments.size();
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
