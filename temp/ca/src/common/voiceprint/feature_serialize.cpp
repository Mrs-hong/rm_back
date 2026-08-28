/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/voiceprint/feature_serialize.h"
#include "qifeng_framework/common/logger.h"

#include <cstddef>
#include <cstring>
namespace qifeng_ca {
    // 序列化声纹嵌入向量为二进制数据(用于存储到features字段)
    // 格式: 直接将float数组的原始内存按序写入, 每个float占4字节(IEEE 754)
    // 512维嵌入向量 → 512 × 4 = 2048 字节
    std::string SerializeEmbedding(const std::vector<std::vector<float>> &embedding) {
        if (embedding.empty()) {
            SLOG_ERROR << "SerializeEmbedding: embedding is empty";
            return "";
        }
        // 计算总float个数
        size_t totalFloats = 0;
        size_t prevSize = embedding[0].size();
        for (const auto &row : embedding) {
            totalFloats += row.size();
            if (row.size() != prevSize) {
                SLOG_ERROR << "SerializeEmbedding: row size not equal to prevSize, row size=" << row.size()
                           << ", prevSize=" << prevSize;
                return "";
            }
        }
        if (totalFloats == 0) {
            SLOG_ERROR << "SerializeEmbedding: totalFloats is 0";
            return "";
        }
        // 直接将float内存拷贝为二进制字符串
        std::string result(totalFloats * sizeof(float), '\0');
        float* dst = reinterpret_cast<float*>(result.data());  // NOLINT
        for (const auto &row : embedding) {
            std::memcpy(dst, row.data(), row.size() * sizeof(float));
            dst += row.size();
        }
        return result;
    }

    // 反序列化声纹嵌入向量(从features二进制字段还原)
    // dim: 嵌入的行数(如1, 表示1×512), 用于还原二维结构
    std::vector<std::vector<float>> DeserializeEmbedding(const std::string &data, int32_t dim) {
        if (data.empty() || dim <= 0) {
            return {};
        }
        size_t totalFloats = data.size() / sizeof(float);
        if (totalFloats == 0) {
            return {};
        }
        size_t dimSize = totalFloats / static_cast<size_t>(dim);
        if (dimSize == 0) {
            return {};
        }
        if (dimSize * static_cast<size_t>(dim) != totalFloats) {
            SLOG_ERROR << "DeserializeEmbedding: totalFloats not divisible by dim, totalFloats=" << totalFloats
                       << ", dim=" << dim;
            return {};
        }
        const float* begin = reinterpret_cast<const float*>(data.data());  // NOLINT
        std::vector<std::vector<float>> result;
        result.reserve(static_cast<size_t>(dim));
        for (int32_t i = 0; i < dim; ++i) {
            const float* rowBegin = begin + static_cast<size_t>(i) * dimSize;
            result.emplace_back(rowBegin, rowBegin + dimSize);
        }
        return result;
    }
}  // namespace qifeng_ca