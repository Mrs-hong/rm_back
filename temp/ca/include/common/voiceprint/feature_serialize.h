/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once
#include <string>
#include <vector>

namespace qifeng_ca {
    /**
     * @brief 序列化声纹嵌入向量为二进制数据(用于存储到features字段)
     *
     * @param embedding 声纹嵌入向量(二维数组, 每个元素为float)
     * @return std::string 序列化后的二进制字符串
     */
    std::string SerializeEmbedding(const std::vector<std::vector<float>> &embedding);

    /**
     * @brief 反序列化声纹嵌入向量(从features二进制字段还原)
     *
     * @param data 序列化后的二进制字符串
     * @param dim 嵌入的行数(如1, 表示1×512), 用于还原二维结构
     * @param featureDim 每个向量的维度(如512)
     * @return std::vector<std::vector<float>> 声纹嵌入向量(二维数组, 每个元素为float)
     */
    std::vector<std::vector<float>> DeserializeEmbedding(const std::string &data, int32_t dim);
}  // namespace qifeng_ca