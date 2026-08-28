/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "models/worker/sv_worker.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/config_manager.h"
#include "common/logger.h"

namespace qifeng {

    SVWorker::SVWorker(const std::string& modelPath, int tpuId, IOMode ioMode)
        : ModelWorker(modelPath, tpuId, ioMode), mInNameSpeech("feature"), mInNamesCache(),
          mOutNameEmbedding("embedding_BatchNormalization"), mOutNamesCache() {
        try {
            SLOG_INFO << "SVWorker: Constructor called";
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
            const int kMaxFrames = 2048;  // bmodel 静态输入维度的帧数上限
            int frameCount = static_cast<int>(speechFeat.size());
            const FloatMatrix* featPtr = &speechFeat;
            FloatMatrix truncated;

            if (frameCount > kMaxFrames) {
                SLOG_WARN << "SVWorker: InferenceBModel input frames=" << frameCount << " exceeds limit=" << kMaxFrames
                          << ", truncating from start";
                truncated.assign(speechFeat.begin(), speechFeat.begin() + kMaxFrames);
                featPtr = &truncated;
                frameCount = kMaxFrames;
            }

            SLOG_DEBUG << "SVWorker: InferenceBModel begin, speech_feat=" << frameCount << "x" << (*featPtr)[0].size();

            // 准备数据
            ModelInput input;
            ModelOutput output;

            std::vector<float> flatInput = Flattener<float>::Flatten(*featPtr);
            // 设置输入数据
            input.data["feature"] = const_cast<float*>(flatInput.data());
            input.shapes["feature"] = {1, frameCount, static_cast<int>((*featPtr)[0].size())};

            size_t outputSize = 1 * 512;

            // 分配输出缓冲区内存
            std::vector<float> outData(outputSize);
            std::vector<int> outShape = {1, 512};

            // 设置输出数据指针
            output.data["embedding_BatchNormalization"] = outData.data();
            // 设置输出形状
            output.shapes["embedding_BatchNormalization"] = outShape;

            // 调用模型推理
            int ret = Process(input, output);
            if (ret != ERR_OK) {
                SLOG_ERROR << "SVWorker: Inference failed, ret=" << ret;
                return FloatMatrix();
            }

            // 需要把logits取出对应的数据然后转换成对应的格式
            FloatMatrix res = Restorer<float>::Restore2D(outData, outShape);

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

        const int kMaxBatch = 16;
        const int kFeatureDim = 80;
        FloatMatrix allEmbeddings;
        allEmbeddings.reserve(speechFeats.size());

        try {
            size_t totalItems = speechFeats.size();
            size_t batchStart = 0;

            while (batchStart < totalItems) {
                size_t batchEnd = std::min(batchStart + kMaxBatch, totalItems);
                size_t batchSize = batchEnd - batchStart;

                // 找到当前 batch 内最大帧数
                int maxFrames = 0;
                for (size_t i = batchStart; i < batchEnd; ++i) {
                    int frames = static_cast<int>(speechFeats[i].size());
                    if (frames > maxFrames) {
                        maxFrames = frames;
                    }
                }
                if (maxFrames <= 0) {
                    SLOG_WARN << "SVWorker: InferenceBModelBatch empty feature at index " << batchStart;
                    batchStart = batchEnd;
                    continue;
                }

                // bmodel 静态输入维度帧数上限，超过则截断
                const int kMaxFrames = 2048;
                if (maxFrames > kMaxFrames) {
                    SLOG_WARN << "SVWorker: InferenceBModelBatch maxFrames=" << maxFrames
                              << " exceeds limit=" << kMaxFrames << ", truncating from start";
                    maxFrames = kMaxFrames;
                }

                // 构造补零后的 3D 输入 [batch, maxFrames, 80]
                std::vector<float> flatInput(batchSize * static_cast<size_t>(maxFrames) * kFeatureDim, 0.0F);
                for (size_t i = 0; i < batchSize; ++i) {
                    const auto& feat = speechFeats[batchStart + i];
                    int copyFrames = std::min(static_cast<int>(feat.size()), maxFrames);
                    for (int f = 0; f < copyFrames; ++f) {
                        const float* src = feat[f].data();
                        float* dst = flatInput.data() + (i * static_cast<size_t>(maxFrames) + f) * kFeatureDim;
                        std::memcpy(dst, src, feat[f].size() * sizeof(float));
                    }
                }

                ModelInput input;
                ModelOutput output;
                input.data["feature"] = const_cast<float*>(flatInput.data());
                input.shapes["feature"] = {static_cast<int>(batchSize), maxFrames, kFeatureDim};

                size_t outputSize = batchSize * 512;
                std::vector<float> outData(outputSize);
                std::vector<int> outShape = {static_cast<int>(batchSize), 512};
                output.data["embedding_BatchNormalization"] = outData.data();
                output.shapes["embedding_BatchNormalization"] = outShape;

                int ret = Process(input, output);
                if (ret != ERR_OK) {
                    SLOG_ERROR << "SVWorker: InferenceBModelBatch failed at batchStart=" << batchStart
                               << ", ret=" << ret;
                    return FloatMatrix();
                }

                // 拆分输出到各 embedding
                for (size_t i = 0; i < batchSize; ++i) {
                    std::vector<float> emb(512);
                    std::memcpy(emb.data(), outData.data() + i * 512, 512 * sizeof(float));
                    allEmbeddings.push_back(std::move(emb));
                }

                SLOG_DEBUG << "SVWorker: InferenceBModelBatch batch [" << batchStart << "," << batchEnd
                           << ") done, maxFrames=" << maxFrames;
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