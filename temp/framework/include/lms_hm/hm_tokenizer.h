/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * File: hm_tokenizer.h
 * Description:
 *   Tokenizer 与 Embedding 封装。基于 tokenizers_c 的 C 绑定实现，
 *   提供 chat template 渲染、编码、解码以及 token embedding 查找。
 */

#ifndef QIFENG_FRAMEWORK_LMS_HM_HM_TOKENIZER_H
#define QIFENG_FRAMEWORK_LMS_HM_HM_TOKENIZER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lms_hm/hm_utils.h"

namespace qifeng {

    namespace lmshm {

        /**
         * @brief HmTokenizer - 负责文本 tokenize / detokenize 与 embedding 查找。
         *
         * 底层使用 tokenizers_c 的 C 绑定（tokenizers_c.h），避免引入额外的 C++ 封装依赖。
         */
        class HmTokenizer {
        public:
            /**
             * @param tokenizerJsonPath   tokenizer.json 路径
             * @param embeddingWeightPath embedding 权重二进制文件（fp16，.bin）路径
             * @param embeddingLen        每个 token 的 embedding 维度
             * @param prefillLen          单次 prefill 的最大 token 数
             */
            HmTokenizer(const std::string& tokenizerJsonPath, const std::string& embeddingWeightPath, int embeddingLen,
                        int prefillLen);

            HmTokenizer(const HmTokenizer&) = delete;
            HmTokenizer& operator=(const HmTokenizer&) = delete;
            HmTokenizer(HmTokenizer&&) noexcept = default;
            HmTokenizer& operator=(HmTokenizer&&) noexcept = default;

            ~HmTokenizer();

            /**
             * @brief 将消息序列渲染为模型可接受的文本（chat template）。
             */
            std::string ApplyChatTemplate(const std::vector<Message>& msgs, bool addGenerationPrompt = true,
                                          bool enableThinking = false);

            /**
             * @brief 将文本编码为 token id 序列。
             */
            std::vector<int32_t> Encode(const std::string& text);

            /**
             * @brief 将 token id 序列解码为文本（跳过特殊 token）。
             */
            std::string Decode(const std::vector<int32_t>& ids);

            /**
             * @brief 根据 token id 序列获取 embedding（fp16）。
             *
             * - 单个 token：直接返回权重内部指针（无拷贝）
             * - 多个 token：拷贝到内部预分配缓冲（已按 prefillLen 补零对齐）
             */
            tensor_type* EmbeddingTokens(const std::vector<int32_t>& ids);

            /**
             * @brief 一步完成 chat template 渲染 + 编码 + embedding。
             */
            tensor_type* EmbeddingTokens(const std::vector<Message>& msgs, bool addGenerationPrompt = true,
                                         bool enableThinking = false);

        private:
            void* mTokenizerHandle = nullptr;        // tokenizers_c 的 TokenizerHandle（void*）
            std::unique_ptr<tensor_type[]> mEmbedW;  // embedding 权重矩阵
            tensor_type* mBuffer = nullptr;          // 多 token embedding 预分配缓冲
            std::size_t mBufferSize = 0;             // 缓冲元素个数

            int mPrefillLength = 0;    // 单次 prefill 最大 token 数
            int mEmbeddingLength = 0;  // embedding 维度
        };

    }  // namespace lmshm

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_LMS_HM_HM_TOKENIZER_H