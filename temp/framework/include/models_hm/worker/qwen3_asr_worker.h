/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * qwen3_asr_worker.h - Qwen3-ASR 离线推理 Worker（TCIM 版本）
 *
 * 对齐 qwen3-asr/src/asr/asr_offline.py 的功能：
 *   1. 加载 encode / prefill / decode 三个 .hmm 模型（decode 声明 dummy KV cache 张量）
 *   2. prefill 与 decode 共享 KV cache
 *   3. 整段音频：Whisper 特征提取 -> encode -> prefill -> 自回归 decode
 *   4. 采样（重复惩罚 + argmax）、token 解码、<asr_text> 提取与字符过滤
 *
 * 架构参照 vad_worker.cpp：继承 ModelWorker 基类（TCIM）。
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_QWEN3_ASR_WORKER_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_QWEN3_ASR_WORKER_H

#include <string>
#include <vector>

#include "models_hm/worker/worker.h"
#include "tcim/tcim_runtime.h"

namespace qifeng {

    // Qwen3-ASR 离线推理 Worker（TCIM 版本），功能对齐 asr_offline.py
    class Qwen3AsrWorker : public ModelWorker {
    public:
        /**
         * @brief 构造并加载三个模型 + embedding + 分词器配置
         * @param encodePath     encode .hmm 模型路径
         * @param prefillPath    prefill .hmm 模型路径
         * @param decodePath     decode .hmm 模型路径
         * @param embeddingPath  quant_embedding.pt（内部解析 zip 取 raw float16 权重）
         * @param processorDir   分词器目录（vocab.json / tokenizer_config.json）
         * @param tpuId          芯片逻辑 ID
         * @param ioMode         IO 模式（TCIM 保留，默认 SYSIO）
         */
        Qwen3AsrWorker(const std::string& encodePath, const std::string& prefillPath, const std::string& decodePath,
                       const std::string& embeddingPath, const std::string& processorDir, int tpuId = 0,
                       IOMode ioMode = IOMode::SYSIO);
        ~Qwen3AsrWorker();

        /**
         * @brief 离线整段推理（对齐 asr_offline.py::_infer）
         * @param audioSample 16kHz 单声道 Float32 PCM 采样
         * @return 识别文本（已提取 <asr_text> 之后内容并过滤字符），失败返回空串
         */
        std::string Inference(const std::vector<float>& audioSample);

        /** 最近一次调用的错误信息 */
        const std::string& error() const {
            return error_;
        }

    private:
        // ---- 模型加载 ----
        // 加载 decode 模块（KV cache 输入声明为 dummy，由 prefill 的 device tensor 填充）
        bool LoadDecodeModule(const std::string& decodePath);
        // prefill 与 decode 共享 KV cache（对齐 python 的 set_dev_input 循环）
        bool ShareKvCache();
        // 从模型输入信息推导常量（nMel / maxFeatureOneLoop / maxPrefill / hidden / maxNewTokens / nBlocks）
        bool ResolveConstants();
        // 加载 embedding 权重：解析 .pt(zip) 中 "quant_embedding/data/0" 原始 float16 字节
        bool LoadEmbedding(const std::string& embeddingPath);
        // 加载分词器配置（vocab.json + tokenizer_config.json）
        bool LoadTokenizer(const std::string& processorDir);
        // 创建持久化 host 输入/输出张量（避免每次推理重复分配）
        bool InitPersistentTensors();

        // ---- 特征提取（WhisperFeatureExtractor 等价实现） ----
        // 输出 features_[128*nFrames]，nFrames = 采样数 / 160
        bool ExtractFeatures(const std::vector<float>& audio);
        // 构建 mel 滤波器组（torchaudio: mel_scale=slaney, norm=slaney）
        void BuildMelFilters();
        // 音频特征长度 -> 编码器输出长度（对齐 _feat_len）
        static int FeatLen(int inputLengths);

        // ---- 推理步骤（对齐 run_encode / run_prefill / run_decode） ----
        // 编码 features_ 中 [startFrame, startFrame+frames) 的特征块，audioEmbeds16_ 写入前 tOut 行
        bool RunEncode(int startFrame, int frames, int& tOut);
        // 拼接 text(左) + audio + text(右) embedding 并执行 prefill；logits 为 [vocab] float32
        bool RunPrefill(int tOut, std::vector<float>& logits, int& length);
        // 自回归解码；generated 为全部生成 token id（含 eos，对齐 python）
        bool RunDecode(int nextTokenId, int length, std::vector<int>& generated, int& nTokens);

        // ---- 采样（对齐 SamplingManager：重复惩罚 + argmax） ----
        int Sample(const float* logits, int vocab, const std::vector<int>& previousTokens);

        // ---- 文本处理 ----
        // token id 序列 -> 文本（byte-level BPE 逆映射 + UTF-8）
        std::string DecodeTokens(const std::vector<int>& ids);
        // 过滤无效字符（保留中文/英文/数字/常见标点，对齐 filter_valid_chars）
        static std::string FilterValidChars(const std::string& text);

        // ---- 工具 ----
        static uint16_t Fp32ToFp16(float f);
        static float Fp16ToFp32(uint16_t h);
        static bool ExtractZipEntry(const std::string& zipPath, const std::string& entryName,
                                    std::vector<uint8_t>& out);
        // UTF-8 解码一个码点，i 前进到下一位置；非法字节按 0xFFFD 处理
        static uint32_t Utf8Decode(const std::string& s, size_t& i);

        // ---- 常量（对齐 preprocessor_config.json / 模型 shape） ----
        static constexpr int kSampleRate = 16000;
        static constexpr int kNfft = 400;
        static constexpr int kHopLength = 160;
        static constexpr int kNumMels = 128;
        static constexpr int kNumFreqBins = kNfft / 2 + 1;  // 201

        static constexpr const char* kGraphEncode = "encode";
        static constexpr const char* kGraphPrefill = "prefill";
        static constexpr const char* kGraphDecode = "decode";

        // 固定 prompt 的 token id（Qwen2 tokenizer + chat_template 分词结果，见 cpp 注释）
        // 前缀：<|im_start|>system\n<|im_end|>\n<|im_start|>user\n<|audio_start|>
        // 后缀：<|audio_end|><|im_end|>\n<|im_start|>assistant\n
        std::vector<int> prefixIds_;
        std::vector<int> suffixIds_;

        // ---- 模型超参（由 .hmm 输入 shape 推导） ----
        int nMel_ = kNumMels;
        int maxFeatureOneLoop_ = 0;  // encode 输入帧上限（3000）
        int maxPrefill_ = 0;         // prefill 输入序列上限（411）
        int hiddenSize_ = 0;         // 隐藏维度（2048）
        int maxNewTokens_ = 0;       // 最大生成 token 数（2048）
        int nBlocks_ = 0;            // decoder 层数（KV cache 对数）
        int vocabSize_ = 0;          // embedding 行数（151936）
        int audioPadId_ = 151676;    // <|audio_pad|>
        int eosTokenId_ = 151645;    // tokenizer eos_token_id

        // ---- 采样参数（对齐 SamplingManager 默认值） ----
        float repetitionPenalty_ = 1.15f;

        // ---- 特征提取成员 ----
        std::vector<float> melFilters_;  // [201 * 128] f32
        std::vector<float> hannWindow_;  // [400] f32
        std::vector<float> features_;    // [128 * nFrames_] 全段特征（f32）
        int nFrames_ = 0;

        // ---- embedding 表（float16 位模式，行主序 [vocab, hidden]） ----
        std::vector<uint16_t> embedding_;

        // ---- 分词器（解码用） ----
        std::vector<std::string> vocab_;                     // id -> token（vocab.json 反查）
        std::vector<std::pair<std::string, bool>> added_;    // id -> (content, special)（added_tokens_decoder）
        std::vector<std::pair<uint32_t, uint8_t>> byteMap_;  // unicode 码点 -> 字节（byte-level BPE 逆映射）

        // ---- 推理缓冲 ----
        std::vector<float> featBufF32_;        // [128 * 3000] 编码输入（零填充）f32
        std::vector<uint16_t> encFeatF16_;     // [128 * 3000] 编码输入 f16
        std::vector<uint16_t> fusedF16_;       // [maxPrefill * hidden] prefill 拼接 embedding f16
        std::vector<uint16_t> audioEmbeds16_;  // encode 输出（f16，仅保留前 tOut 行）
        std::vector<int> textIds_;             // prefix + N*audio_pad + suffix

        // ---- 持久化 host tensor（与模型 dtype 一致，复用避免重复分配） ----
        tcim::Tensor encIn0_;        // input_features   [1,128,3000] f16
        tcim::Tensor encIn1_;        // feature_lens     [1] int32
        tcim::Tensor prefillIn0_;    // input_embeds  [1,411,2048] f16
        tcim::Tensor prefillIn1_;    // valid_length  [1] int32
        tcim::Tensor prefillIn2_;    // current_length[1] int32
        tcim::Tensor decIn0_;        // input_embeds     [1,1,2048] f16
        tcim::Tensor decIn1_;        // valid_length     [1] int32
        tcim::Tensor decIn2_;        // current_length   [1] int32
        tcim::Tensor logitsTensor_;  // 输出 logits [1,vocab] f32（prefill/decode 复用）

        std::string error_;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_QWEN3_ASR_WORKER_H
