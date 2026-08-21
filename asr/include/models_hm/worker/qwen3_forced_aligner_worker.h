/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * qwen3_forced_aligner_worker.h - Qwen3-ForcedAligner-0.6B 强制对齐 Worker（TCIM 版本，桩实现）
 *
 * 对齐 python qwen_asr.Qwen3ForcedAligner.align() 的功能：
 *   1. 输入：16kHz 音频 + 已知识别文本
 *   2. 切词 + 构造 "<ts><ts>word1<ts><ts>word2..." 模板
 *   3. 音频转 mel 特征，过 audio_tower，替换 <|audio_pad|> 占位
 *   4. 一次前向，logits.argmax 在 <|ts|> 位置取 output_ids
 *   5. output_id × timestamp_segment_time → 毫秒时间戳
 *
 * 现状：ForcedAligner 的 HMM 模型尚未产出（仅 PyTorch 版），
 * 因此本 Worker 当前为桩实现：
 *   - alignerHmmPath 为空或加载失败时进入降级模式（IsReady() == false）
 *   - Align() 返回空列表，由上层 ModelsManager::QwenForcedAlignProcess
 *     走降级策略（文本字符均匀分布）生成粗粒度时间戳
 *   - HMM 模型就绪后，在 LoadAlignerModule/Align 内补齐推理逻辑即可，
 *     对外接口保持不变
 *
 * 架构参照 qwen3_asr_worker.cpp：继承 ModelWorker 基类（TCIM）。
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_QWEN3_FORCED_ALIGNER_WORKER_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_QWEN3_FORCED_ALIGNER_WORKER_H

#include <cstdint>
#include <string>
#include <vector>

#include "models_hm/worker/worker.h"

namespace qifeng {

    // Qwen3-ForcedAligner 强制对齐 Worker（TCIM 版本，当前为桩实现）
    class Qwen3ForcedAlignerWorker : public ModelWorker {
    public:
        // 对齐结果项：一个 token（中文按字/英文按词）的时间区间
        struct AlignItem {
            std::string text;     // token 文本
            int64_t startMs = 0;  // 相对音频起点的开始时间（毫秒）
            int64_t endMs = 0;    // 相对音频起点的结束时间（毫秒）
        };

        /**
         * @brief 构造并尝试加载对齐模型
         * @param alignerPath   对齐器 .hmm 模型路径（空串=直接进入降级模式，不加载）
         * @param processorDir  分词器目录（vocab.json / tokenizer_config.json）
         * @param tpuId         芯片逻辑 ID
         * @param ioMode        IO 模式（TCIM 保留，默认 SYSIO）
         */
        Qwen3ForcedAlignerWorker(const std::string& alignerPath, const std::string& processorDir, int tpuId = 0,
                                 IOMode ioMode = IOMode::SYSIO);
        ~Qwen3ForcedAlignerWorker() override;

        /**
         * @brief 强制对齐推理（对齐 python Qwen3ForcedAligner.align）
         * @param audioSample 16kHz 单声道 Float32 PCM 采样
         * @param text        已知识别文本（ASR 输出）
         * @return 词级时间戳列表（相对音频起点，毫秒）；降级模式下返回空列表
         */
        std::vector<AlignItem> Align(const std::vector<float>& audioSample, const std::string& text);

        /** 是否处于降级模式（模型未就绪） */
        bool IsDegraded() const {
            return mDegraded;
        }

        /** 最近一次调用的错误信息 */
        const std::string& error() const {
            return error_;
        }

    private:
        // ---- 模型加载（HMM 就绪后实现）----
        // 加载对齐器分词器配置（切词 + <ts> 模板构造所需）
        bool LoadProcessor(const std::string& processorDir);
        // 加载对齐器 HMM 模块；路径为空返回 false（进入降级模式，不算错误）
        bool LoadAlignerModule(const std::string& alignerPath);

        // ---- 对齐推理（HMM 就绪后实现）----
        // 构造 "<ts><ts>word1<ts><ts>word2..." 对齐模板并编码为 token 序列
        bool EncodeTimestampTemplate(const std::string& text, std::vector<int>& tokenIds);
        // 一次前向：音频特征 + 模板 -> 在 <|ts|> 位置提取时间戳 output_ids
        bool RunAlignForward(const std::vector<float>& audioSample, const std::vector<int>& tokenIds,
                             std::vector<int64_t>& outTimestampMs);

        bool mDegraded = true;  // 降级模式标志（模型未加载/加载失败）
        std::string error_;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_QWEN3_FORCED_ALIGNER_WORKER_H
