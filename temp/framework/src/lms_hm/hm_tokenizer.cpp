/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * File: hm_tokenizer.cpp
 * Description:
 *   HmTokenizer 实现，基于 tokenizers_c 的 C 绑定。
 */

#include <cstring>
#include <stdexcept>

#include <tokenizers_c.h>

#include "lms_hm/hm_tokenizer.h"
namespace qifeng {

    namespace lmshm {

        namespace {
            // 将 C 绑定返回的错误信息包装为异常消息。
            std::string TokenizerError(const char* msg) {
                return msg != nullptr ? std::string(msg) : std::string("unknown error");
            }
        }  // namespace

        HmTokenizer::HmTokenizer(const std::string& tokenizerJsonPath, const std::string& embeddingWeightPath,
                                 int embeddingLen, int prefillLen)
            : mPrefillLength(prefillLen), mEmbeddingLength(embeddingLen) {
            // 1. 加载 tokenizer 配置
            std::string blob = LoadBytesFromFile(tokenizerJsonPath);
            mTokenizerHandle = new_tokenizer_from_json(blob.c_str());
            if (mTokenizerHandle == nullptr) {
                throw std::runtime_error("Failed to create tokenizer from " + tokenizerJsonPath + ": " +
                                         TokenizerError(tokenizer_get_last_error()));
            }

            // 2. 加载 embedding 权重（末尾预留 prefillLen * embeddingLen 元素，
            //    便于单 token 直接返回权重内部指针）
            mEmbedW = ReadEmbeddingWeight<tensor_type>(embeddingWeightPath,
                                                       static_cast<size_t>(mPrefillLength) * mEmbeddingLength);
            if (!mEmbedW) {
                throw std::runtime_error("Failed to load embedding weights: " + embeddingWeightPath);
            }

            // 3. 预分配多 token embedding 缓冲
            mBufferSize = static_cast<size_t>(mPrefillLength) * mEmbeddingLength;
            mBuffer = new tensor_type[mBufferSize];
        }

        HmTokenizer::~HmTokenizer() {
            if (mTokenizerHandle != nullptr) {
                free_tokenizer(mTokenizerHandle);
                mTokenizerHandle = nullptr;
            }
            delete[] mBuffer;
            mBuffer = nullptr;
        }

        std::string HmTokenizer::ApplyChatTemplate(const std::vector<Message>& msgs, bool addGenerationPrompt,
                                                   bool enableThinking) {
            std::string out;
            out.reserve(1024);

            for (const auto& m : msgs) {
                out.append("<|im_start|>");
                out.append(m.role);
                out.push_back('\n');
                out.append(m.content);
                out.append("<|im_end|>\n");
            }

            if (addGenerationPrompt) {
                out.append("<|im_start|>assistant\n");
            }

            // 关闭思考模式时，追加空的 think 块，与官方 chat_template 保持一致。
            if (!enableThinking) {
                out.append(" thinking\n");
                out.append("\n");
                out.append(" response\n");
                out.append("\n");
            }

            return out;
        }

        std::vector<int32_t> HmTokenizer::Encode(const std::string& text) {
            std::vector<int32_t> ids;
            auto* result = tokenizer_encode(mTokenizerHandle, text.c_str(), /*add_special_tokens=*/false);
            if (result == nullptr) {
                throw std::runtime_error("tokenizer_encode failed: " + TokenizerError(tokenizer_get_last_error()));
            }
            ids.assign(result->token_ids, result->token_ids + result->len);
            free_tokenizer_encode_result(result);
            return ids;
        }

        std::string HmTokenizer::Decode(const std::vector<int32_t>& ids) {
            if (ids.empty()) {
                return std::string();
            }
            auto* result = tokenizer_decode(mTokenizerHandle, reinterpret_cast<const uint32_t*>(ids.data()), ids.size(),
                                            /*skip_special_tokens=*/true);
            if (result == nullptr) {
                throw std::runtime_error("tokenizer_decode failed: " + TokenizerError(tokenizer_get_last_error()));
            }
            std::string text = (result->text != nullptr) ? std::string(result->text) : std::string();
            free_tokenizer_decode_result(result);
            return text;
        }

        tensor_type* HmTokenizer::EmbeddingTokens(const std::vector<int32_t>& ids) {
            const std::size_t numTokens = ids.size();
            if (numTokens == 0) {
                return nullptr;
            }

            if (numTokens == 1) {
                // 单 token 直接返回权重内部指针，避免拷贝。
                const std::size_t offset = static_cast<std::size_t>(ids[0]) * mEmbeddingLength;
                return reinterpret_cast<tensor_type*>(&mEmbedW[offset]);
            }

            // 多 token 场景：先清空缓冲（等效补零到 prefillLen），再逐行拷贝。
            std::fill(mBuffer, mBuffer + mBufferSize, tensor_type(0.0f));
            for (std::size_t i = 0; i < numTokens; ++i) {
                const std::size_t src = static_cast<std::size_t>(ids[i]) * mEmbeddingLength;
                std::memcpy(&mBuffer[i * mEmbeddingLength], &mEmbedW[src], mEmbeddingLength * sizeof(tensor_type));
            }
            return mBuffer;
        }

        tensor_type* HmTokenizer::EmbeddingTokens(const std::vector<Message>& msgs, bool addGenerationPrompt,
                                                  bool enableThinking) {
            if (msgs.empty()) {
                return nullptr;
            }
            std::string rendered = ApplyChatTemplate(msgs, addGenerationPrompt, enableThinking);
            std::vector<int32_t> ids = Encode(rendered);
            return EmbeddingTokens(ids);
        }

    }  // namespace lmshm

}  // namespace qifeng