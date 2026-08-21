/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "models_hm/worker/sv_worker.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/config_manager.h"
#include "common/logger.h"

namespace qifeng {

    SVWorker::SVWorker(const std::string& modelPath, int tpuId, IOMode ioMode)
        : ModelWorker(modelPath, tpuId, ioMode), mInNameSpeech("feature"), mInNamesCache(),
          mOutNameEmbedding("embedding"), mOutNamesCache() {
        try {
            // 模型已在 ModelWorker 构造时加载，从模型文件读取静态 shape
            if (IsReady()) {
                tcim::TensorInfo inInfo = GetInputInfo(mInNameSpeech);
                const auto& inShape = inInfo.Shape();
                if (inShape.size() == 3) {
                    mStaticBatch = static_cast<int>(inShape[0]);
                    mMaxFrames = static_cast<int>(inShape[1]);
                    mFeatureDim = static_cast<int>(inShape[2]);
                }
                tcim::TensorInfo outInfo = GetOutputInfo(mOutNameEmbedding);
                const auto& outShape = outInfo.Shape();
                if (outShape.size() == 2) {
                    mEmbeddingDim = static_cast<int>(outShape[1]);
                }
            }
            SLOG_INFO << "SVWorker: Constructor called, static_batch=" << mStaticBatch << " max_frames=" << mMaxFrames
                      << " feature_dim=" << mFeatureDim << " embedding_dim=" << mEmbeddingDim;
        } catch (const std::exception& e) {
            SLOG_ERROR << "SVWorker: Exception in constructor: " << e.what();
        } catch (...) {
            SLOG_ERROR << "SVWorker: Unknown exception in constructor";
        }
    }

    SVWorker::~SVWorker() {
        try {
            SLOG_INFO << "SVWorker: Destructor called";
        } catch (const std::exception& e) {
            SLOG_ERROR << "SVWorker: Exception in destructor: " << e.what();
        } catch (...) {
            SLOG_ERROR << "SVWorker: Unknown exception in destructor";
        }
    }

    FloatMatrix SVWorker::InferenceBModel(const FloatMatrix& speechFeat) {
        if (speechFeat.empty() || speechFeat[0].empty()) {
            SLOG_WARN << "SVWorker: InferenceBModel got empty input";
            return FloatMatrix();
        }

        try {
            int frameCount = static_cast<int>(speechFeat.size());

            if (frameCount > mMaxFrames) {
                SLOG_WARN << "SVWorker: InferenceBModel input frames=" << frameCount << " exceeds limit=" << mMaxFrames
                          << ", truncating from start";
                frameCount = mMaxFrames;
            }

            SLOG_DEBUG << "SVWorker: InferenceBModel begin, speech_feat=" << frameCount << "x" << mFeatureDim;

            // 构造 [static_batch, max_frames, feature_dim] 的输入，batch 0 用重复帧填充到 max_frames 帧
            // （避免 0-padding 参与 attention pooling 导致 embedding 偏差）
            std::vector<float> flatInput(static_cast<size_t>(mStaticBatch) * mMaxFrames * mFeatureDim, 0.0F);
            float* batch0 = flatInput.data();
            for (int f = 0; f < mMaxFrames; ++f) {
                int srcFrame = f % frameCount;  // 重复填充
                const float* src = speechFeat[srcFrame].data();
                float* dst = batch0 + static_cast<size_t>(f) * mFeatureDim;
                std::memcpy(dst, src, mFeatureDim * sizeof(float));
            }

            ModelInput input;
            ModelOutput output;
            input.data["feature"] = const_cast<float*>(flatInput.data());
            input.shapes["feature"] = {mStaticBatch, mMaxFrames, mFeatureDim};

            size_t outputSize = static_cast<size_t>(mStaticBatch) * mEmbeddingDim;
            std::vector<float> outData(outputSize);
            std::vector<int> outShape = {mStaticBatch, mEmbeddingDim};
            output.data["embedding"] = outData.data();
            output.shapes["embedding"] = outShape;

            int ret = Process(input, output);
            if (ret != ERR_OK) {
                SLOG_ERROR << "SVWorker: Inference failed, ret=" << ret;
                return FloatMatrix();
            }

            // 只取 batch 0 的 embedding_dim 维 embedding
            FloatMatrix res;
            std::vector<float> emb(mEmbeddingDim);
            std::memcpy(emb.data(), outData.data(), mEmbeddingDim * sizeof(float));
            res.push_back(std::move(emb));

            SLOG_DEBUG << "SVWorker: InferenceBModel done, embedding size=" << res.size() << "x" << res[0].size();
            return res;
        } catch (const std::exception& e) {
            SLOG_ERROR << "SVWorker: Exception in InferenceBModel: " << e.what();
            return FloatMatrix();
        } catch (...) {
            SLOG_ERROR << "SVWorker: Unknown exception in InferenceBModel";
            return FloatMatrix();
        }
    }

    FloatMatrix SVWorker::InferenceBModelBatch(const std::vector<FloatMatrix>& speechFeats) {
        if (speechFeats.empty()) {
            return FloatMatrix();
        }
        if (speechFeats.size() == 1) {
            return InferenceBModel(speechFeats[0]);
        }

        const int kStaticBatch = mStaticBatch;  // .hmm 静态 batch size
        const int kMaxFrames = mMaxFrames;      // .hmm 静态帧数
        const int kFeatureDim = mFeatureDim;
        FloatMatrix allEmbeddings;
        allEmbeddings.reserve(speechFeats.size());

        try {
            size_t totalItems = speechFeats.size();
            size_t batchStart = 0;

            while (batchStart < totalItems) {
                size_t batchEnd = std::min(batchStart + static_cast<size_t>(kStaticBatch), totalItems);
                size_t batchSize = batchEnd - batchStart;

                // 构造 [static_batch, max_frames, feature_dim] 的输入，每个 batch 用重复帧填充到 max_frames 帧
                std::vector<float> flatInput(static_cast<size_t>(kStaticBatch) * kMaxFrames * kFeatureDim, 0.0F);
                for (size_t i = 0; i < batchSize; ++i) {
                    const auto& feat = speechFeats[batchStart + i];
                    int frameCount = std::min(static_cast<int>(feat.size()), kMaxFrames);
                    if (frameCount <= 0) {
                        SLOG_WARN << "SVWorker: InferenceBModelBatch empty feature at index " << batchStart + i;
                        continue;
                    }
                    float* dstBase = flatInput.data() + (i * static_cast<size_t>(kMaxFrames)) * kFeatureDim;
                    for (int f = 0; f < kMaxFrames; ++f) {
                        int srcFrame = f % frameCount;  // 重复填充
                        const float* src = feat[srcFrame].data();
                        float* dst = dstBase + static_cast<size_t>(f) * kFeatureDim;
                        std::memcpy(dst, src, kFeatureDim * sizeof(float));
                    }
                }

                ModelInput input;
                ModelOutput output;
                input.data["feature"] = const_cast<float*>(flatInput.data());
                input.shapes["feature"] = {kStaticBatch, kMaxFrames, kFeatureDim};

                size_t outputSize = static_cast<size_t>(kStaticBatch) * mEmbeddingDim;
                std::vector<float> outData(outputSize);
                std::vector<int> outShape = {kStaticBatch, mEmbeddingDim};
                output.data["embedding"] = outData.data();
                output.shapes["embedding"] = outShape;

                int ret = Process(input, output);
                if (ret != ERR_OK) {
                    SLOG_ERROR << "SVWorker: InferenceBModelBatch failed at batchStart=" << batchStart
                               << ", ret=" << ret;
                    return FloatMatrix();
                }

                // 拆分输出到各 embedding
                for (size_t i = 0; i < batchSize; ++i) {
                    std::vector<float> emb(mEmbeddingDim);
                    std::memcpy(emb.data(), outData.data() + i * mEmbeddingDim, mEmbeddingDim * sizeof(float));
                    allEmbeddings.push_back(std::move(emb));
                }

                SLOG_DEBUG << "SVWorker: InferenceBModelBatch batch [" << batchStart << "," << batchEnd << ") done";
                batchStart = batchEnd;
            }

            SLOG_DEBUG << "SVWorker: InferenceBModelBatch total=" << speechFeats.size()
                       << ", embeddings=" << allEmbeddings.size();
            return allEmbeddings;
        } catch (const std::exception& e) {
            SLOG_ERROR << "SVWorker: Exception in InferenceBModelBatch: " << e.what();
            return FloatMatrix();
        } catch (...) {
            SLOG_ERROR << "SVWorker: Unknown exception in InferenceBModelBatch";
            return FloatMatrix();
        }
    }

}  // namespace qifeng