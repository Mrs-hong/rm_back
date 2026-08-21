/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * qwen3_forced_aligner_worker.cpp - Qwen3-ForcedAligner-0.6B 强制对齐 Worker（桩实现）
 *
 * 现状：对齐器 HMM 模型尚未产出，本文件为桩实现：
 *   - 路径为空 → 直接降级模式（正常路径，非错误）
 *   - 路径非空但加载失败 → 记录错误并降级
 *   - Align() 在降级模式下返回空列表，上层走降级策略
 *
 * HMM 模型就绪后的补齐点（见头文件注释的 python 对齐流程）：
 *   LoadProcessor / EncodeTimestampTemplate / RunAlignForward
 */

#include "models_hm/worker/qwen3_forced_aligner_worker.h"

#include "common/logger.h"

namespace qifeng {

    Qwen3ForcedAlignerWorker::Qwen3ForcedAlignerWorker(const std::string& alignerPath,
                                                       const std::string& processorDir, int tpuId, IOMode ioMode)
        : ModelWorker(alignerPath.empty() ? std::string("degraded") : alignerPath, tpuId, ioMode) {
        try {
            // 路径为空：正常进入降级模式（配置项默认即空，等待 HMM 模型就绪）
            if (alignerPath.empty()) {
                mDegraded = true;
                SLOG_INFO << "Qwen3ForcedAlignerWorker: 未配置对齐器模型路径，进入降级模式"
                          << "（时间戳将由上层按文本均匀分布生成）";
                return;
            }
            // 路径非空：尝试加载模型（当前为桩，模型就绪后补齐）
            if (!LoadAlignerModule(alignerPath)) {
                mDegraded = true;
                SLOG_WARN << "Qwen3ForcedAlignerWorker: 对齐器模型加载失败，进入降级模式: " << error_;
                return;
            }
            if (!LoadProcessor(processorDir)) {
                mDegraded = true;
                SLOG_WARN << "Qwen3ForcedAlignerWorker: 分词器加载失败，进入降级模式: " << error_;
                return;
            }
            mDegraded = false;
            SLOG_INFO << "Qwen3ForcedAlignerWorker: 初始化成功";
        } catch (const std::exception& e) {
            mDegraded = true;
            error_ = std::string("Qwen3ForcedAlignerWorker: 初始化异常 - ") + e.what();
            SLOG_ERROR << error_;
        }
    }

    Qwen3ForcedAlignerWorker::~Qwen3ForcedAlignerWorker() {
        if (!mDegraded) {
            SLOG_INFO << "Qwen3ForcedAlignerWorker: 析构函数调用";
        }
    }

    std::vector<Qwen3ForcedAlignerWorker::AlignItem>
    Qwen3ForcedAlignerWorker::Align(const std::vector<float>& audioSample, const std::string& text) {
        // 降级模式：返回空列表，由上层 ModelsManager::QwenForcedAlignProcess 走降级策略
        if (mDegraded) {
            return {};
        }
        // ---- 以下为 HMM 就绪后的推理流程（桩：直接返回空）----
        std::vector<int> tokenIds;
        if (!EncodeTimestampTemplate(text, tokenIds)) {
            SLOG_ERROR << "Qwen3ForcedAlignerWorker: 模板编码失败: " << error_;
            return {};
        }
        std::vector<int64_t> timestampMs;
        if (!RunAlignForward(audioSample, tokenIds, timestampMs)) {
            SLOG_ERROR << "Qwen3ForcedAlignerWorker: 对齐推理失败: " << error_;
            return {};
        }
        (void)timestampMs;  // TODO(HMM就绪): output_id × timestamp_segment_time → 分配到每个 token
        return {};
    }

    // ============================================================================
    // 模型加载（桩）
    // ============================================================================

    bool Qwen3ForcedAlignerWorker::LoadAlignerModule(const std::string& alignerPath) {
        // TODO(HMM就绪): 通过 LoadGraph 注册对齐器模块并解析输入输出 shape，
        //   推导 timestamp_segment_time 等常量（对齐 python aligner.timestamp_segment_time）
        error_ = "Qwen3ForcedAlignerWorker: 对齐器 HMM 模型尚未提供: " + alignerPath;
        return false;
    }

    bool Qwen3ForcedAlignerWorker::LoadProcessor(const std::string& processorDir) {
        // TODO(HMM就绪): 加载 vocab.json / tokenizer_config.json，
        //   实现切词（中文按字、英文按词）与 <ts> 模板 token 编码
        error_ = "Qwen3ForcedAlignerWorker: 分词器尚未提供: " + processorDir;
        return false;
    }

    // ============================================================================
    // 对齐推理（桩）
    // ============================================================================

    bool Qwen3ForcedAlignerWorker::EncodeTimestampTemplate(const std::string& text, std::vector<int>& tokenIds) {
        // TODO(HMM就绪): 构造 "<ts><ts>word1<ts><ts>word2..." 模板并编码
        (void)text;
        (void)tokenIds;
        error_ = "Qwen3ForcedAlignerWorker: EncodeTimestampTemplate 未实现（等待 HMM 模型）";
        return false;
    }

    bool Qwen3ForcedAlignerWorker::RunAlignForward(const std::vector<float>& audioSample,
                                                    const std::vector<int>& tokenIds,
                                                    std::vector<int64_t>& outTimestampMs) {
        // TODO(HMM就绪): mel 特征 -> audio_tower -> 模板前向 -> <|ts|> 位置 argmax
        (void)audioSample;
        (void)tokenIds;
        (void)outTimestampMs;
        error_ = "Qwen3ForcedAlignerWorker: RunAlignForward 未实现（等待 HMM 模型）";
        return false;
    }

}  // namespace qifeng
