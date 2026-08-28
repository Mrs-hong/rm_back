/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * qwen3_asr_worker.cpp - Qwen3-ASR 离线推理 Worker 实现（TCIM 版本）
 *
 * 功能对齐 qwen3-asr/src/asr/asr_offline.py：
 *   1. Load：加载 encode / prefill / decode 三个 .hmm 模型
 *      - decode 的 KV cache 输入声明为 dummy 张量（对应 python 的 set_dummy_tensors）
 *      - prefill 与 decode 共享同一份 KV cache（对应 python 的 set_dev_input 循环）
 *   2. 特征提取：WhisperFeatureExtractor 等价实现（n_fft=400, hop=160, 128 路 mel）
 *   3. 推理：encode -> prefill -> 自回归 decode（重复惩罚 + argmax 采样）
 *   4. 文本：token 解码（byte-level BPE 逆映射）-> 提取 <asr_text> 之后 -> 字符过滤
 *
 * 架构参照 vad_worker.cpp / asr_worker.cpp：继承 ModelWorker 基类（TCIM）。
 */

#include "models_hm/worker/qwen3_asr_worker.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/logger.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace qifeng {

    namespace {
        constexpr double kPi = 3.14159265358979323846;

        // 对齐 python 的 // 整除（向负无穷取整）
        int FloorDiv(int a, int b) {
            int q = a / b;
            if ((a % b) != 0 && ((a < 0) != (b < 0))) {
                --q;
            }
            return q;
        }

        // 将 Unicode 码点追加为 UTF-8 字节
        void AppendUtf8(std::string& out, uint32_t cp) {
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
    }  // namespace

    // ============================================================================
    // 构造 / 析构
    // ============================================================================

    Qwen3AsrWorker::Qwen3AsrWorker(const std::string& encodePath, const std::string& prefillPath,
                                   const std::string& decodePath, const std::string& embeddingPath,
                                   const std::string& processorDir, int tpuId, IOMode ioMode)
        : ModelWorker(encodePath, tpuId, ioMode) {
        try {
            // 基类已将 encode 加载为默认图（图名 = 文件名），注册 "encode" 别名
            if (!IsReady()) {
                error_ = "Qwen3AsrWorker: encode 模型加载失败: " + encodePath;
                SLOG_ERROR << error_;
                return;
            }
            mModules[kGraphEncode] = mModule;
            mGraphInputNames[kGraphEncode] = mGraphInputNames[mDefaultGraphName];
            mGraphOutputNames[kGraphEncode] = mGraphOutputNames[mDefaultGraphName];
            mGraphNames.push_back(kGraphEncode);

            // prefill（普通加载，KV cache 输入由模块自行分配）
            if (!LoadGraph(kGraphPrefill, prefillPath)) {
                error_ = "Qwen3AsrWorker: prefill 模型加载失败: " + prefillPath;
                SLOG_ERROR << error_;
                return;
            }
            // 推导常量（nBlocks 依赖 prefill 输入名）
            if (!ResolveConstants()) {
                error_ = "Qwen3AsrWorker: 模型常量推导失败";
                SLOG_ERROR << error_;
                return;
            }
            // decode（KV cache 输入声明为 dummy）
            if (!LoadDecodeModule(decodePath)) {
                error_ = "Qwen3AsrWorker: decode 模型加载失败: " + decodePath;
                SLOG_ERROR << error_;
                return;
            }
            // prefill 与 decode 共享 KV cache
            if (!ShareKvCache()) {
                error_ = "Qwen3AsrWorker: KV cache 共享失败";
                SLOG_ERROR << error_;
                return;
            }
            // embedding 权重 + 分词器配置
            if (!LoadEmbedding(embeddingPath)) {
                error_ = "Qwen3AsrWorker: embedding 加载失败: " + embeddingPath;
                SLOG_ERROR << error_;
                return;
            }
            if (!LoadTokenizer(processorDir)) {
                error_ = "Qwen3AsrWorker: 分词器配置加载失败: " + processorDir;
                SLOG_ERROR << error_;
                return;
            }
            // 持久 host tensor + 推理缓冲
            if (!InitPersistentTensors()) {
                error_ = "Qwen3AsrWorker: 持久张量初始化失败";
                SLOG_ERROR << error_;
                return;
            }

            SLOG_INFO << "Qwen3AsrWorker: 初始化成功, maxPrefill=" << maxPrefill_ << ", hidden=" << hiddenSize_
                      << ", maxFeatureOneLoop=" << maxFeatureOneLoop_ << ", maxNewTokens=" << maxNewTokens_
                      << ", nBlocks=" << nBlocks_ << ", vocab=" << vocabSize_ << ", eos=" << eosTokenId_;
        } catch (const std::exception& e) {
            error_ = std::string("Qwen3AsrWorker: 初始化异常 - ") + e.what();
            SLOG_ERROR << error_;
        } catch (...) {
            error_ = "Qwen3AsrWorker: 初始化异常 - 未知异常";
            SLOG_ERROR << error_;
        }
    }

    Qwen3AsrWorker::~Qwen3AsrWorker() {
        try {
            SLOG_INFO << "Qwen3AsrWorker: 析构函数调用";
        } catch (...) {
        }
    }

    // ============================================================================
    // 模型加载
    // ============================================================================

    bool Qwen3AsrWorker::ResolveConstants() {
        auto prefill = mModules[kGraphPrefill];
        if (!prefill) {
            return false;
        }
        const auto& pnames = mGraphInputNames[kGraphPrefill];
        if (pnames.size() < 3) {
            return false;
        }
        // nBlocks：统计 prefill 输入中 KV cache 输入数量（对齐 python 的 _get_nblocks）
        nBlocks_ = 0;
        for (const auto& n : pnames) {
            if (n.find("model_layers_") == 0 && n.find("_self_attn_kcache_input") != std::string::npos) {
                ++nBlocks_;
            }
        }
        // input_embeds [1, max_prefill, hidden]
        auto s0 = prefill->GetInputInfo(pnames[0]).Shape();
        if (s0.size() >= 3) {
            maxPrefill_ = static_cast<int>(s0[1]);
            hiddenSize_ = static_cast<int>(s0[2]);
        }
        // 第一个 kcache 输入 [1, 8, max_new_tokens, 128]（对齐 python：input_name(3) 的 shape[2]）
        if (pnames.size() > 3) {
            auto s3 = prefill->GetInputInfo(pnames[3]).Shape();
            if (s3.size() >= 3) {
                maxNewTokens_ = static_cast<int>(s3[2]);
            }
        }
        auto enc = mModules[kGraphEncode];
        if (!enc) {
            return false;
        }
        const auto& enames = mGraphInputNames[kGraphEncode];
        if (enames.empty()) {
            return false;
        }
        auto e0 = enc->GetInputInfo(enames[0]).Shape();  // [1, n_mels, max_feature_one_loop]
        if (e0.size() >= 3) {
            nMel_ = static_cast<int>(e0[1]);
            maxFeatureOneLoop_ = static_cast<int>(e0[2]);
        }
        return maxPrefill_ > 0 && hiddenSize_ > 0 && maxFeatureOneLoop_ > 0 && maxNewTokens_ > 0 && nBlocks_ > 0;
    }

    bool Qwen3AsrWorker::LoadDecodeModule(const std::string& decodePath) {
        if (nBlocks_ <= 0) {
            return false;
        }
        // 对齐 python：dummy = [model_layers_i_self_attn_kcache_input,
        //                       model_layers_i_self_attn_vcache_input] for i in range(nblocks)
        std::vector<std::string> dummy;
        dummy.reserve(static_cast<size_t>(nBlocks_) * 2);
        for (int i = 0; i < nBlocks_; ++i) {
            dummy.push_back("model_layers_" + std::to_string(i) + "_self_attn_kcache_input");
            dummy.push_back("model_layers_" + std::to_string(i) + "_self_attn_vcache_input");
        }
        tcim::Module::Option opt(mTpuId);
        opt.SetDummyTensors(dummy);

        auto module = std::make_shared<tcim::Module>(tcim::Module::LoadFromFile(decodePath, opt));
        if (module->GetInitStatus() != tcim::Status::OK) {
            return false;
        }
        if (module->SetStream(mStream) != tcim::Status::OK) {
            return false;
        }
        std::vector<std::string> inNames, outNames;
        for (size_t i = 0; i < module->GetInputNum(); ++i) {
            inNames.push_back(module->GetInputName(static_cast<int>(i)));
        }
        for (size_t i = 0; i < module->GetOutputNum(); ++i) {
            outNames.push_back(module->GetOutputName(static_cast<int>(i)));
        }
        mModules[kGraphDecode] = module;
        mGraphInputNames[kGraphDecode] = std::move(inNames);
        mGraphOutputNames[kGraphDecode] = std::move(outNames);
        mGraphNames.push_back(kGraphDecode);
        SLOG_INFO << "Qwen3AsrWorker: decode 模型加载完成, inputs=" << mGraphInputNames[kGraphDecode].size()
                  << ", dummyKv=" << dummy.size();
        return true;
    }

    bool Qwen3AsrWorker::ShareKvCache() {
        auto prefill = mModules[kGraphPrefill];
        auto decode = mModules[kGraphDecode];
        if (!prefill || !decode) {
            return false;
        }
        // 对齐 python：for i in range(3, 2*nblocks+3): decode.set_dev_input(name, prefill.get_dev_input(name))
        const auto& names = mGraphInputNames[kGraphPrefill];
        for (size_t i = 3; i < names.size(); ++i) {
            tcim::Tensor cache = prefill->GetDevInput(names[i]);
            if (decode->SetDevInput(names[i], cache) != tcim::Status::OK) {
                SLOG_ERROR << "Qwen3AsrWorker: KV cache 共享失败 at " << names[i];
                return false;
            }
        }
        return true;
    }

    bool Qwen3AsrWorker::LoadEmbedding(const std::string& embeddingPath) {
        // quant_embedding.pt 为 zip 容器，权重存放于 "quant_embedding/data/0"（STORED, float16 行主序）
        std::vector<uint8_t> raw;
        if (!ExtractZipEntry(embeddingPath, "quant_embedding/data/0", raw)) {
            return false;
        }
        if (raw.size() % 2 != 0 || hiddenSize_ <= 0 || raw.size() % (static_cast<size_t>(hiddenSize_) * 2) != 0) {
            return false;
        }
        const size_t elems = raw.size() / 2;
        vocabSize_ = static_cast<int>(elems / hiddenSize_);
        embedding_.resize(elems);
        std::memcpy(embedding_.data(), raw.data(), raw.size());
        SLOG_INFO << "Qwen3AsrWorker: embedding 加载完成, vocab=" << vocabSize_ << ", hidden=" << hiddenSize_;
        return true;
    }

    bool Qwen3AsrWorker::LoadTokenizer(const std::string& processorDir) {
        // ---- 1. vocab.json：token -> id，反查为 id -> token ----
        fs::path vp = fs::path(processorDir) / "vocab.json";
        if (!fs::is_regular_file(vp)) {
            return false;
        }
        {
            std::ifstream ifs(vp);
            if (!ifs) {
                return false;
            }
            json j;
            ifs >> j;
            if (!j.is_object()) {
                return false;
            }
            vocab_.assign(j.size(), std::string());
            for (auto it = j.begin(); it != j.end(); ++it) {
                int id = it.value().get<int>();
                if (id >= 0 && id < static_cast<int>(vocab_.size())) {
                    vocab_[id] = it.key();
                }
            }
        }

        // ---- 2. tokenizer_config.json：added_tokens_decoder（特殊标记）+ eos_token_id ----
        fs::path tp = fs::path(processorDir) / "tokenizer_config.json";
        if (fs::is_regular_file(tp)) {
            std::ifstream ifs(tp);
            if (ifs) {
                json j;
                ifs >> j;
                if (j.contains("added_tokens_decoder") && j["added_tokens_decoder"].is_object()) {
                    const json& atd = j["added_tokens_decoder"];
                    int maxId = 0;
                    for (auto it = atd.begin(); it != atd.end(); ++it) {
                        maxId = std::max(maxId, std::stoi(it.key()));
                    }
                    added_.assign(static_cast<size_t>(maxId) + 1, {std::string(), false});
                    for (auto it = atd.begin(); it != atd.end(); ++it) {
                        int id = std::stoi(it.key());
                        std::string content = it.value().value("content", std::string());
                        bool special = it.value().value("special", false);
                        if (id >= 0 && id < static_cast<int>(added_.size())) {
                            added_[id] = {content, special};
                        }
                    }
                }
                if (j.contains("eos_token_id")) {
                    if (j["eos_token_id"].is_array() && !j["eos_token_id"].empty()) {
                        eosTokenId_ = j["eos_token_id"][0].get<int>();
                    } else if (j["eos_token_id"].is_number_integer()) {
                        eosTokenId_ = j["eos_token_id"].get<int>();
                    }
                }
            }
        }

        // ---- 3. 固定 prompt 的 token id ----
        // 离线模板固定为：
        //   <|im_start|>system\n<|im_end|>\n<|im_start|>user\n<|audio_start|> <N×audio_pad>
        //   <|audio_end|><|im_end|>\n<|im_start|>assistant\n
        // 经 Qwen2 tokenizer 分词后（与 asr_offline.py 完全一致）：
        //   前缀 9 个 id：[im_start, system, Ċ, im_end, Ċ, im_start, user, Ċ, audio_start]
        //   后缀 6 个 id：[audio_end, im_end, Ċ, im_start, assistant, Ċ]
        prefixIds_ = {151644, 8948, 198, 151645, 198, 151644, 872, 198, 151669};
        suffixIds_ = {151670, 151645, 198, 151644, 77091, 198};

        // ---- 4. byte-level BPE 逆映射（GPT-2 bytes_to_unicode 的逆） ----
        byteMap_.clear();
        std::vector<uint32_t> b2u(256);
        int n = 0;
        for (int b = 0; b < 256; ++b) {
            bool inFirst = (b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255);
            b2u[static_cast<size_t>(b)] = inFirst ? static_cast<uint32_t>(b) : (256u + static_cast<uint32_t>(n++));
        }
        for (int b = 0; b < 256; ++b) {
            byteMap_.push_back({b2u[static_cast<size_t>(b)], static_cast<uint8_t>(b)});
        }
        return true;
    }

    bool Qwen3AsrWorker::InitPersistentTensors() {
        auto mkIn = [](const std::shared_ptr<tcim::Module>& m, const std::string& name) {
            return tcim::Tensor::CreateHostTensor(m->GetInputInfo(name).AsContiguous());
        };
        const auto& enames = mGraphInputNames[kGraphEncode];
        const auto& pnames = mGraphInputNames[kGraphPrefill];
        const auto& dnames = mGraphInputNames[kGraphDecode];
        if (enames.size() < 2 || pnames.size() < 3 || dnames.size() < 3) {
            return false;
        }
        encIn0_ = mkIn(mModules[kGraphEncode], enames[0]);       // input_features  f16 [1,128,3000]
        encIn1_ = mkIn(mModules[kGraphEncode], enames[1]);       // feature_lens    int32 [1]
        prefillIn0_ = mkIn(mModules[kGraphPrefill], pnames[0]);  // input_embeds  f16 [1,411,2048]
        prefillIn1_ = mkIn(mModules[kGraphPrefill], pnames[1]);  // valid_length  int32 [1]
        prefillIn2_ = mkIn(mModules[kGraphPrefill], pnames[2]);  // current_length int32 [1]
        decIn0_ = mkIn(mModules[kGraphDecode], dnames[0]);       // input_embeds  f16 [1,1,2048]
        decIn1_ = mkIn(mModules[kGraphDecode], dnames[1]);       // valid_length  int32 [1]
        decIn2_ = mkIn(mModules[kGraphDecode], dnames[2]);       // current_length int32 [1]

        // 输出 logits 张量（模型输出 f16 -> 转为 f32 便于采样）
        const auto& pouts = mGraphOutputNames[kGraphPrefill];
        tcim::TensorInfo oinfo =
            mModules[kGraphPrefill]->GetOutputInfo(pouts[0]).AsType(tcim::DataType::FLOAT32).AsContiguous();
        logitsTensor_ = tcim::Tensor::CreateHostTensor(oinfo);

        // 推理缓冲
        featBufF32_.assign(static_cast<size_t>(nMel_) * maxFeatureOneLoop_, 0.0f);
        encFeatF16_.assign(static_cast<size_t>(nMel_) * maxFeatureOneLoop_, 0);
        fusedF16_.assign(static_cast<size_t>(maxPrefill_) * hiddenSize_, 0);

        return encIn0_.GetInitStatus() == tcim::Status::OK && prefillIn0_.GetInitStatus() == tcim::Status::OK &&
               decIn0_.GetInitStatus() == tcim::Status::OK && logitsTensor_.GetInitStatus() == tcim::Status::OK;
    }

    // ============================================================================
    // 特征提取（WhisperFeatureExtractor 等价实现）
    // ============================================================================

    // torchaudio melscale_fbanks(mel_scale="slaney", norm="slaney")
    // slaney 刻度为分段线性 + 对数（librosa 风格）：<1000Hz 线性，>=1000Hz 对数
    void Qwen3AsrWorker::BuildMelFilters() {
        const int nFreqs = kNumFreqBins;  // 201
        const int nMels = nMel_;          // 128
        const double fMax = kSampleRate / 2.0;
        const double fSp = 200.0 / 3.0;
        const double minLogHz = 1000.0;
        const double minLogMel = minLogHz / fSp;  // 15.0
        const double logStep = std::log(6.4) / 27.0;
        auto hz2mel = [&](double f) {
            if (f < minLogHz) {
                return f / fSp;
            }
            return minLogMel + std::log(f / minLogHz) / logStep;
        };
        auto mel2hz = [&](double m) {
            if (m < minLogMel) {
                return fSp * m;
            }
            return minLogHz * std::exp(logStep * (m - minLogMel));
        };

        std::vector<double> allFreqs(nFreqs);
        for (int i = 0; i < nFreqs; ++i) {
            allFreqs[static_cast<size_t>(i)] = fMax * i / (nFreqs - 1);  // linspace(0, 8000, 201)
        }
        std::vector<double> mPts(nMels + 2), fPts(nMels + 2);
        const double mMin = hz2mel(0.0), mMax = hz2mel(fMax);
        for (int i = 0; i < nMels + 2; ++i) {
            mPts[static_cast<size_t>(i)] = mMin + (mMax - mMin) * i / (nMels + 1);
        }
        for (int i = 0; i < nMels + 2; ++i) {
            fPts[static_cast<size_t>(i)] = mel2hz(mPts[static_cast<size_t>(i)]);
        }
        melFilters_.assign(static_cast<size_t>(nFreqs) * nMels, 0.0f);
        for (int i = 0; i < nMels; ++i) {
            const double left = fPts[static_cast<size_t>(i)];
            const double center = fPts[static_cast<size_t>(i + 1)];
            const double right = fPts[static_cast<size_t>(i + 2)];
            for (int k = 0; k < nFreqs; ++k) {
                double up = (allFreqs[static_cast<size_t>(k)] - left) / (center - left);
                double down = (right - allFreqs[static_cast<size_t>(k)]) / (right - center);
                double v = std::min(up, down);
                melFilters_[static_cast<size_t>(k) * nMels + i] = static_cast<float>(v > 0.0 ? v : 0.0);
            }
            // slaney 面积归一化：2 / (f_pts[i+2] - f_pts[i])
            const double enorm = 2.0 / (fPts[static_cast<size_t>(i + 2)] - fPts[static_cast<size_t>(i)]);
            for (int k = 0; k < nFreqs; ++k) {
                melFilters_[static_cast<size_t>(k) * nMels + i] *= static_cast<float>(enorm);
            }
        }
    }

    int Qwen3AsrWorker::FeatLen(int inputLengths) {
        // 对齐 asr_offline.py::_feat_len（注意 python // 为向下取整）
        int leave = inputLengths % 100;
        int feat = FloorDiv(leave - 1, 2) + 1;
        int out = FloorDiv(FloorDiv(feat - 1, 2) + 1 - 1, 2) + 1 + (inputLengths / 100) * 13;
        return out;
    }

    bool Qwen3AsrWorker::ExtractFeatures(const std::vector<float>& audio) {
        const int N = static_cast<int>(audio.size());
        if (N == 0) {
            return false;
        }
        if (melFilters_.empty()) {
            BuildMelFilters();
        }
        if (hannWindow_.empty()) {
            // 周期 Hann 窗，等价 np.hanning(401)[:-1]
            for (int i = 0; i < kNfft; ++i) {
                hannWindow_.push_back(static_cast<float>(0.5 * (1.0 - std::cos(2.0 * kPi * i / kNfft))));
            }
        }
        nFrames_ = N / kHopLength;  // 输出帧数 = floor(N/160)
        if (nFrames_ <= 0) {
            return false;
        }
        const int totalFrames = nFrames_ + 1;  // 中心补零 STFT 帧数，最后一帧随后丢弃

        // 预计算 twiddle 表（2π*m/400）
        std::vector<double> cosT(kNfft), sinT(kNfft);
        for (int a = 0; a < kNfft; ++a) {
            double ph = 2.0 * kPi * a / kNfft;
            cosT[static_cast<size_t>(a)] = std::cos(ph);
            sinT[static_cast<size_t>(a)] = std::sin(ph);
        }

        // 每帧 400 点中心反射填充（对齐 torch.stft center=True, pad_mode='reflect'）+ Hann 加窗
        std::vector<double> power(static_cast<size_t>(kNumFreqBins) * totalFrames);  // [k][t]
        std::vector<double> frame(kNfft, 0.0);
        for (int t = 0; t < totalFrames; ++t) {
            const int base = t * kHopLength - kNfft / 2;
            for (int n = 0; n < kNfft; ++n) {
                const int src = base + n;  // 反射填充后的索引
                double v;
                if (src < 0) {
                    v = audio[static_cast<size_t>(-src)];  // 左侧反射
                } else if (src >= N) {
                    v = audio[static_cast<size_t>(2 * N - 2 - src)];  // 右侧反射
                } else {
                    v = audio[static_cast<size_t>(src)];
                }
                frame[static_cast<size_t>(n)] = v * hannWindow_[static_cast<size_t>(n)];
            }
            for (int k = 0; k < kNumFreqBins; ++k) {
                double re = 0.0, im = 0.0;
                for (int n = 0; n < kNfft; ++n) {
                    int idx = (k * n) % kNfft;
                    re += frame[static_cast<size_t>(n)] * cosT[static_cast<size_t>(idx)];
                    im -= frame[static_cast<size_t>(n)] * sinT[static_cast<size_t>(idx)];
                }
                power[static_cast<size_t>(k) * totalFrames + t] = re * re + im * im;
            }
        }

        // mel：melFilters_[201*128]^T @ power -> melTmp_[128][t]
        std::vector<double> melTmp(static_cast<size_t>(nMel_) * totalFrames);
        for (int t = 0; t < totalFrames; ++t) {
            for (int m = 0; m < nMel_; ++m) {
                double s = 0.0;
                for (int k = 0; k < kNumFreqBins; ++k) {
                    s += melFilters_[static_cast<size_t>(k) * nMel_ + m] *
                         power[static_cast<size_t>(k) * totalFrames + t];
                }
                melTmp[static_cast<size_t>(m) * totalFrames + t] = s;
            }
        }
        // log10，下限 1e-10
        for (size_t i = 0; i < melTmp.size(); ++i) {
            melTmp[i] = std::log10(std::max(melTmp[i], 1e-10));
        }
        // 丢弃最后一帧；全局 max - 8 截断；(x+4)/4
        double maxv = -std::numeric_limits<double>::max();
        for (int m = 0; m < nMel_; ++m) {
            for (int f = 0; f < nFrames_; ++f) {
                maxv = std::max(maxv, melTmp[static_cast<size_t>(m) * totalFrames + f]);
            }
        }
        const double clip = maxv - 8.0;
        features_.assign(static_cast<size_t>(nMel_) * nFrames_, 0.0f);
        for (int m = 0; m < nMel_; ++m) {
            for (int f = 0; f < nFrames_; ++f) {
                double v = std::max(melTmp[static_cast<size_t>(m) * totalFrames + f], clip);
                features_[static_cast<size_t>(m) * nFrames_ + f] = static_cast<float>((v + 4.0) / 4.0);
            }
        }
        return true;
    }

    // ============================================================================
    // 推理步骤（对齐 run_encode / run_prefill / run_decode）
    // ============================================================================

    bool Qwen3AsrWorker::RunEncode(int startFrame, int frames, int& tOut) {
        if (frames <= 0 || frames > maxFeatureOneLoop_ || !mModules[kGraphEncode]) {
            return false;
        }
        auto mod = mModules[kGraphEncode];
        const auto& names = mGraphInputNames[kGraphEncode];
        const std::string& outName = mGraphOutputNames[kGraphEncode][0];

        // 零填充到 [128, 3000] 并取子块（对齐 np.pad）
        std::fill(featBufF32_.begin(), featBufF32_.end(), 0.0f);
        for (int m = 0; m < nMel_; ++m) {
            const float* src = features_.data() + static_cast<size_t>(m) * nFrames_ + startFrame;
            std::copy(src, src + frames, featBufF32_.begin() + static_cast<size_t>(m) * maxFeatureOneLoop_);
        }
        // f32 -> f16
        for (size_t i = 0; i < encFeatF16_.size(); ++i) {
            encFeatF16_[i] = Fp32ToFp16(featBufF32_[i]);
        }
        if (encIn0_.Buffer().CopyFromHost(encFeatF16_.data(), encFeatF16_.size() * 2) != tcim::Status::OK) {
            return false;
        }
        if (mod->SetInput(names[0], encIn0_) != tcim::Status::OK) {
            return false;
        }
        int32_t len = frames;
        if (encIn1_.Buffer().CopyFromHost(&len, sizeof(len)) != tcim::Status::OK) {
            return false;
        }
        if (mod->SetInput(names[1], encIn1_) != tcim::Status::OK) {
            return false;
        }
        if (mod->Run(true) != tcim::Status::OK) {
            return false;
        }
        // 输出 [1, 390, 2048] f16，仅保留前 tOut 行（对齐 audio_embeds[:, :t_out, :]）
        tcim::Tensor out = mod->GetOutput(outName);
        const uint16_t* p = reinterpret_cast<const uint16_t*>(out.Data());
        if (!p) {
            return false;
        }
        tOut = FeatLen(frames);
        audioEmbeds16_.assign(p, p + static_cast<size_t>(tOut) * hiddenSize_);
        return true;
    }

    bool Qwen3AsrWorker::RunPrefill(int tOut, std::vector<float>& logits, int& length) {
        auto mod = mModules[kGraphPrefill];
        if (!mod || tOut <= 0) {
            return false;
        }
        const auto& names = mGraphInputNames[kGraphPrefill];
        const std::string& outName = mGraphOutputNames[kGraphPrefill][0];

        // 拼接 text(左) + audio + text(右)，在 audio_pad 处插入音频 embedding（对齐 run_prefill）
        std::fill(fusedF16_.begin(), fusedF16_.end(), 0);
        int start = -1, end = -1;
        for (size_t i = 0; i < textIds_.size(); ++i) {
            if (textIds_[i] == audioPadId_) {
                if (start < 0) {
                    start = static_cast<int>(i);
                }
                end = static_cast<int>(i);
            }
        }
        auto copyRow = [this](int pos, int tokenId) {
            if (tokenId >= 0 && tokenId < vocabSize_) {
                std::memcpy(fusedF16_.data() + static_cast<size_t>(pos) * hiddenSize_,
                            embedding_.data() + static_cast<size_t>(tokenId) * hiddenSize_,
                            static_cast<size_t>(hiddenSize_) * 2);
            }
        };

        int cursor = 0;
        if (start >= 0) {
            int left = std::min(start, maxPrefill_);
            for (int i = 0; i < left; ++i) {
                copyRow(cursor++, textIds_[static_cast<size_t>(i)]);
            }
            int remain = maxPrefill_ - cursor;
            int audioLen = std::min(tOut, remain);
            if (audioLen > 0) {
                std::memcpy(fusedF16_.data() + static_cast<size_t>(cursor) * hiddenSize_, audioEmbeds16_.data(),
                            static_cast<size_t>(audioLen) * hiddenSize_ * 2);
                cursor += audioLen;
            }
            remain = maxPrefill_ - cursor;
            int tailAvail = std::max(static_cast<int>(textIds_.size()) - end - 1, 0);
            int tailLen = std::min(tailAvail, remain);
            for (int i = 0; i < tailLen; ++i) {
                copyRow(cursor++, textIds_[static_cast<size_t>(end + 1 + i)]);
            }
            length = cursor;
        } else {
            length = std::min(static_cast<int>(textIds_.size()), maxPrefill_);
            for (int i = 0; i < length; ++i) {
                copyRow(i, textIds_[static_cast<size_t>(i)]);
            }
        }
        if (length <= 0) {
            return false;
        }

        // 输入
        if (prefillIn0_.Buffer().CopyFromHost(fusedF16_.data(), fusedF16_.size() * 2) != tcim::Status::OK) {
            return false;
        }
        if (mod->SetInput(names[0], prefillIn0_) != tcim::Status::OK) {
            return false;
        }
        int32_t v = 0;
        if (prefillIn1_.Buffer().CopyFromHost(&v, sizeof(v)) != tcim::Status::OK) {
            return false;
        }
        if (mod->SetInput(names[1], prefillIn1_) != tcim::Status::OK) {
            return false;
        }
        int32_t c = length;
        if (prefillIn2_.Buffer().CopyFromHost(&c, sizeof(c)) != tcim::Status::OK) {
            return false;
        }
        if (mod->SetInput(names[2], prefillIn2_) != tcim::Status::OK) {
            return false;
        }
        if (mod->Run(true) != tcim::Status::OK) {
            return false;
        }
        tcim::Tensor out = mod->GetOutput(outName);
        if (out.CastTo(logitsTensor_) != tcim::Status::OK) {
            return false;
        }
        const float* lp = reinterpret_cast<const float*>(logitsTensor_.Data());
        size_t n = logitsTensor_.MemSize() / sizeof(float);
        logits.assign(lp, lp + n);
        return true;
    }

    bool Qwen3AsrWorker::RunDecode(int nextTokenId, int length, std::vector<int>& generated, int& nTokens) {
        auto mod = mModules[kGraphDecode];
        if (!mod) {
            return false;
        }
        const auto& names = mGraphInputNames[kGraphDecode];
        const std::string& outName = mGraphOutputNames[kGraphDecode][0];

        generated.clear();
        generated.push_back(nextTokenId);
        int32_t validLen = length;
        int step = 0;
        for (; step < maxNewTokens_; ++step) {
            const int tokenId = generated.back();
            if (tokenId < 0 || tokenId >= vocabSize_) {
                break;
            }
            // decode 输入：当前 token 的 embedding（f16 行）
            const uint16_t* emb = embedding_.data() + static_cast<size_t>(tokenId) * hiddenSize_;
            if (decIn0_.Buffer().CopyFromHost(emb, static_cast<size_t>(hiddenSize_) * 2) != tcim::Status::OK) {
                return false;
            }
            if (mod->SetInput(names[0], decIn0_) != tcim::Status::OK) {
                return false;
            }
            if (decIn1_.Buffer().CopyFromHost(&validLen, sizeof(validLen)) != tcim::Status::OK) {
                return false;
            }
            if (mod->SetInput(names[1], decIn1_) != tcim::Status::OK) {
                return false;
            }
            int32_t cur = 1;  // current_length 恒为 1（对齐 python）
            if (decIn2_.Buffer().CopyFromHost(&cur, sizeof(cur)) != tcim::Status::OK) {
                return false;
            }
            if (mod->SetInput(names[2], decIn2_) != tcim::Status::OK) {
                return false;
            }
            if (mod->Run(true) != tcim::Status::OK) {
                return false;
            }
            tcim::Tensor out = mod->GetOutput(outName);
            if (out.CastTo(logitsTensor_) != tcim::Status::OK) {
                return false;
            }
            const float* lp = reinterpret_cast<const float*>(logitsTensor_.Data());
            int next = Sample(lp, vocabSize_, generated);
            generated.push_back(next);
            ++validLen;
            if (next == eosTokenId_) {
                break;
            }
        }
        nTokens = static_cast<int>(generated.size()) - 1;  // 对齐 python: len(generated)-1
        return true;
    }

    // ============================================================================
    // 采样（对齐 SamplingManager：重复惩罚 + argmax；top_k/top_p/温度保持默认不生效）
    // ============================================================================

    int Qwen3AsrWorker::Sample(const float* logits, int vocab, const std::vector<int>& previousTokens) {
        if (!logits || vocab <= 0) {
            return 0;
        }
        std::vector<float> processed(logits, logits + vocab);
        if (repetitionPenalty_ != 1.0f && !previousTokens.empty()) {
            // 重复惩罚：对已生成 token 去重后惩罚（对齐 python 的 set(previous_tokens)）
            std::vector<char> flag(static_cast<size_t>(vocab), 0);
            for (int t : previousTokens) {
                if (t >= 0 && t < vocab) {
                    flag[static_cast<size_t>(t)] = 1;
                }
            }
            for (int i = 0; i < vocab; ++i) {
                if (!flag[static_cast<size_t>(i)]) {
                    continue;
                }
                if (processed[static_cast<size_t>(i)] < 0.0f) {
                    processed[static_cast<size_t>(i)] *= repetitionPenalty_;
                } else {
                    processed[static_cast<size_t>(i)] /= repetitionPenalty_;
                }
            }
        }
        int best = 0;
        float m = processed[0];
        for (int i = 1; i < vocab; ++i) {
            if (processed[static_cast<size_t>(i)] > m) {
                m = processed[static_cast<size_t>(i)];
                best = i;
            }
        }
        return best;
    }

    // ============================================================================
    // 文本处理
    // ============================================================================

    std::string Qwen3AsrWorker::DecodeTokens(const std::vector<int>& ids) {
        std::string raw;
        for (int id : ids) {
            std::string tok;
            if (id >= 0 && id < static_cast<int>(added_.size()) && !added_[static_cast<size_t>(id)].first.empty()) {
                if (added_[static_cast<size_t>(id)].second) {
                    continue;  // 跳过特殊 token（对齐 skip_special_tokens=True）
                }
                tok = added_[static_cast<size_t>(id)].first;
            } else if (id >= 0 && id < static_cast<int>(vocab_.size())) {
                tok = vocab_[static_cast<size_t>(id)];
            } else {
                continue;
            }
            raw += tok;
        }
        // byte-level 逆映射：unicode 码点 -> 字节
        std::string bytes;
        for (size_t i = 0; i < raw.size();) {
            uint32_t cp = Utf8Decode(raw, i);
            uint8_t b = 0;
            bool found = false;
            for (const auto& kv : byteMap_) {
                if (kv.first == cp) {
                    b = kv.second;
                    found = true;
                    break;
                }
            }
            if (found) {
                bytes.push_back(static_cast<char>(b));
            } else {
                // 理论不会出现（byte-level token 全部在映射内）；原样保留该码点
                AppendUtf8(bytes, cp);
            }
        }
        return bytes;  // 已是合法 UTF-8
    }

    std::string Qwen3AsrWorker::FilterValidChars(const std::string& text) {
        // 与 asr_offline.py 的 PUNCTUATION_CHARS 完全一致（32 个码点）
        static const uint32_t kPunct[] = {0x0020, 0x0021, 0x0022, 0x0027, 0x0028, 0x0029, 0x002C, 0x002D,
                                          0x002E, 0x003A, 0x003B, 0x003C, 0x003E, 0x003F, 0x005B, 0x005D,
                                          0x00B7, 0x2014, 0x2026, 0x3001, 0x3002, 0x300A, 0x300B, 0x3010,
                                          0x3011, 0xFF01, 0xFF08, 0xFF09, 0xFF0C, 0xFF1A, 0xFF1B, 0xFF1F};
        const size_t kPunctN = sizeof(kPunct) / sizeof(kPunct[0]);

        auto isValid = [&](uint32_t cp) -> bool {
            if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x20000 && cp <= 0x2A6DF) ||
                (cp >= 0x2A700 && cp <= 0x2B73F) || (cp >= 0x2B740 && cp <= 0x2B81F) ||
                (cp >= 0x2B820 && cp <= 0x2CEAF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
                (cp >= 0x2F800 && cp <= 0x2FA1F)) {
                return true;
            }
            if ((cp >= 0x41 && cp <= 0x5A) || (cp >= 0x61 && cp <= 0x7A)) {
                return true;
            }
            if (cp >= 0x30 && cp <= 0x39) {
                return true;
            }
            return std::binary_search(kPunct, kPunct + kPunctN, cp);
        };

        std::string out;
        for (size_t i = 0; i < text.size();) {
            uint32_t cp = Utf8Decode(text, i);
            if (isValid(cp)) {
                AppendUtf8(out, cp);
            }
        }
        return out;
    }

    // ============================================================================
    // 主流程（对齐 asr_offline.py::run / _infer）
    // ============================================================================

    std::string Qwen3AsrWorker::Inference(const std::vector<float>& audioSample) {
        if (audioSample.empty()) {
            error_ = "Qwen3AsrWorker: 输入音频为空";
            return "";
        }
        if (!IsReady()) {
            error_ = "Qwen3AsrWorker: 模型未初始化";
            return "";
        }
        // 特征提取：整段音频 -> [128, nFrames]
        if (!ExtractFeatures(audioSample)) {
            error_ = "Qwen3AsrWorker: 特征提取失败";
            return "";
        }
        // prompt：prefix + N*audio_pad + suffix，N = FeatLen(总帧数)
        const int nPads = FeatLen(nFrames_);
        textIds_.clear();
        textIds_.insert(textIds_.end(), prefixIds_.begin(), prefixIds_.end());
        textIds_.insert(textIds_.end(), nPads, audioPadId_);
        textIds_.insert(textIds_.end(), suffixIds_.begin(), suffixIds_.end());

        // 按 maxFeatureOneLoop 切段循环（对齐 loop_count = feat_len // max_loop + 1）
        const int loopCount = nFrames_ / maxFeatureOneLoop_ + 1;
        std::string fullText;
        for (int loop = 0; loop < loopCount; ++loop) {
            const int start = loop * maxFeatureOneLoop_;
            const int end = std::min(start + maxFeatureOneLoop_, nFrames_);
            const int frames = end - start;
            if (frames <= 0) {
                break;
            }
            int tOut = 0;
            if (!RunEncode(start, frames, tOut)) {
                error_ = "Qwen3AsrWorker: encode 推理失败";
                return "";
            }
            std::vector<float> logits;
            int length = 0;
            if (!RunPrefill(tOut, logits, length)) {
                error_ = "Qwen3AsrWorker: prefill 推理失败";
                return "";
            }
            if (logits.empty()) {
                return "";
            }
            // prefill 输出取首 token（无重复惩罚，对齐 sample(last_hidden)）
            const int nextToken = Sample(logits.data(), static_cast<int>(logits.size()), {});
            std::vector<int> generated;
            int nTokens = 0;
            if (!RunDecode(nextToken, length, generated, nTokens)) {
                error_ = "Qwen3AsrWorker: decode 推理失败";
                return "";
            }
            (void)nTokens;  // 对齐 python 的 total_tokens（仅统计用途）
            // 文本：decode -> 提取 <asr_text> 之后 -> 过滤字符
            std::string decoded = DecodeTokens(generated);
            std::string text;
            const size_t pos = decoded.find("<asr_text>");
            text = (pos != std::string::npos) ? decoded.substr(pos + 10) : decoded;
            fullText += FilterValidChars(text);
        }
        return fullText;
    }

    // ============================================================================
    // 工具函数
    // ============================================================================

    uint16_t Qwen3AsrWorker::Fp32ToFp16(float f) {
        uint32_t x;
        std::memcpy(&x, &f, sizeof(x));
        const uint32_t sign = (x >> 16) & 0x8000u;
        const int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 127 + 15;
        const uint32_t mant = x & 0x7FFFFFu;
        if (((x >> 23) & 0xFFu) == 0xFFu) {  // Inf / NaN
            return static_cast<uint16_t>(sign | (mant ? 0x7E00u : 0x7C00u));
        }
        if (exp >= 31) {
            return static_cast<uint16_t>(sign | 0x7C00u);  // 溢出 -> Inf
        }
        if (exp <= 0) {  // 次正规 / 下溢
            if (exp < -10) {
                return static_cast<uint16_t>(sign);
            }
            uint32_t m = mant | 0x800000u;  // 隐含前导 1
            const uint32_t shift = static_cast<uint32_t>(14 - exp);
            uint32_t half = m >> shift;
            const uint32_t rem = m & ((1u << shift) - 1u);
            if (rem > (1u << (shift - 1u))) {
                ++half;  // 四舍五入
            } else if (rem == (1u << (shift - 1u)) && (half & 1u)) {
                ++half;  // 平局取偶
            }
            return static_cast<uint16_t>(sign | half);
        }
        // 正规数：尾数截断 + 就近舍入
        uint32_t hm = mant >> 13;
        const uint32_t rem = mant & 0x1FFFu;
        if (rem > 0x1000u) {
            ++hm;
        } else if (rem == 0x1000u && (hm & 1u)) {
            ++hm;
        }
        if (hm >= 0x400u) {  // 进位到指数
            if (exp + 1 >= 31) {
                return static_cast<uint16_t>(sign | 0x7C00u);
            }
            return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp + 1) << 10));
        }
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | hm);
    }

    float Qwen3AsrWorker::Fp16ToFp32(uint16_t h) {
        const uint32_t sign = (static_cast<uint32_t>(h) & 0x8000u) << 16;
        const uint32_t e = (h >> 10) & 0x1Fu;
        const uint32_t m = h & 0x3FFu;
        uint32_t x;
        if (e == 0) {
            if (m == 0) {
                x = sign;
            } else {  // 次正规
                int32_t e2 = -14;
                uint32_t mm = m;
                while (!(mm & 0x400u)) {
                    mm <<= 1;
                    --e2;
                }
                mm &= 0x3FFu;
                x = sign | (static_cast<uint32_t>(e2 + 127) << 23) | (mm << 13);
            }
        } else if (e == 31) {
            x = sign | 0x7F800000u | (m << 13);
        } else {
            x = sign | (static_cast<uint32_t>(e - 15 + 127) << 23) | (m << 13);
        }
        float f;
        std::memcpy(&f, &x, sizeof(f));
        return f;
    }

    bool Qwen3AsrWorker::ExtractZipEntry(const std::string& zipPath, const std::string& entryName,
                                         std::vector<uint8_t>& out) {
        std::ifstream f(zipPath, std::ios::binary);
        if (!f) {
            return false;
        }
        // 读取文件尾部的 EOCD 搜索窗口（EOCD 前可能有最长 65535 字节注释）
        f.seekg(0, std::ios::end);
        const std::streamoff fileSize = f.tellg();
        if (fileSize <= 0) {
            return false;
        }
        const size_t win = static_cast<size_t>(std::min<std::streamoff>(fileSize, 65557));
        std::vector<uint8_t> tail(win);
        f.seekg(fileSize - static_cast<std::streamoff>(win), std::ios::beg);
        f.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(win));

        // 查找 EOCD 签名 0x06054b50
        ssize_t eocd = -1;
        for (ssize_t i = static_cast<ssize_t>(win) - 22; i >= 0; --i) {
            if (tail[static_cast<size_t>(i)] == 0x50 && tail[static_cast<size_t>(i + 1)] == 0x4b &&
                tail[static_cast<size_t>(i + 2)] == 0x05 && tail[static_cast<size_t>(i + 3)] == 0x06) {
                eocd = i;
                break;
            }
        }
        if (eocd < 0) {
            return false;
        }
        auto rd32 = [&](size_t off, int len) -> uint32_t {
            uint32_t v = 0;
            for (int k = 0; k < len; ++k) {
                v |= static_cast<uint32_t>(tail[off + static_cast<size_t>(k)]) << (8 * k);
            }
            return v;
        };
        const uint32_t nEntries = rd32(static_cast<size_t>(eocd) + 10, 2);
        const uint32_t cdSize = rd32(static_cast<size_t>(eocd) + 12, 4);
        const uint32_t cdOff = rd32(static_cast<size_t>(eocd) + 16, 4);

        // 读取中心目录
        std::vector<uint8_t> cd(cdSize);
        f.seekg(cdOff, std::ios::beg);
        f.read(reinterpret_cast<char*>(cd.data()), static_cast<std::streamsize>(cdSize));
        if (f.gcount() != static_cast<std::streamsize>(cdSize)) {
            return false;
        }
        size_t p = 0;
        for (uint32_t e = 0; e < nEntries && p + 46 <= cd.size(); ++e) {
            if (!(cd[p] == 0x50 && cd[p + 1] == 0x4b && cd[p + 2] == 0x01 && cd[p + 3] == 0x02)) {
                break;
            }
            const uint32_t method = cd[p + 10] | (static_cast<uint32_t>(cd[p + 11]) << 8);
            const uint32_t usize = cd[p + 24] | (static_cast<uint32_t>(cd[p + 25]) << 8) |
                                   (static_cast<uint32_t>(cd[p + 26]) << 16) |
                                   (static_cast<uint32_t>(cd[p + 27]) << 24);
            const uint16_t nlen = static_cast<uint16_t>(cd[p + 28] | (static_cast<uint16_t>(cd[p + 29]) << 8));
            const uint16_t xlen = static_cast<uint16_t>(cd[p + 30] | (static_cast<uint16_t>(cd[p + 31]) << 8));
            const uint16_t clen = static_cast<uint16_t>(cd[p + 32] | (static_cast<uint16_t>(cd[p + 33]) << 8));
            const uint32_t lho = cd[p + 42] | (static_cast<uint32_t>(cd[p + 43]) << 8) |
                                 (static_cast<uint32_t>(cd[p + 44]) << 16) | (static_cast<uint32_t>(cd[p + 45]) << 24);
            const std::string name(reinterpret_cast<const char*>(cd.data()) + p + 46, nlen);
            if (name == entryName) {
                if (method != 0) {
                    return false;  // 仅支持 STORED（不压缩）
                }
                // 读取 local header，获得数据区偏移
                std::vector<uint8_t> lh(30);
                f.seekg(lho, std::ios::beg);
                f.read(reinterpret_cast<char*>(lh.data()), 30);
                if (f.gcount() != 30 || !(lh[0] == 0x50 && lh[1] == 0x4b && lh[2] == 0x03 && lh[3] == 0x04)) {
                    return false;
                }
                const uint16_t lnlen = static_cast<uint16_t>(lh[26] | (static_cast<uint16_t>(lh[27]) << 8));
                const uint16_t lxlen = static_cast<uint16_t>(lh[28] | (static_cast<uint16_t>(lh[29]) << 8));
                const std::streamoff dataOff = lho + 30 + lnlen + lxlen;
                out.resize(usize);
                f.seekg(dataOff, std::ios::beg);
                f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(usize));
                return f.gcount() == static_cast<std::streamsize>(usize);
            }
            p += 46 + nlen + xlen + clen;
        }
        return false;
    }

    uint32_t Qwen3AsrWorker::Utf8Decode(const std::string& s, size_t& i) {
        const uint8_t c = static_cast<uint8_t>(s[i]);
        if (c < 0x80) {
            ++i;
            return c;
        }
        int len = 0;
        uint32_t cp = 0;
        uint32_t min = 0;
        if ((c & 0xE0) == 0xC0) {
            len = 1;
            cp = c & 0x1Fu;
            min = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            len = 2;
            cp = c & 0x0Fu;
            min = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            len = 3;
            cp = c & 0x07u;
            min = 0x10000;
        } else {
            ++i;
            return 0xFFFD;
        }
        if (i + static_cast<size_t>(len) >= s.size() + 1) {
            i = s.size();
            return 0xFFFD;
        }
        for (int k = 1; k <= len; ++k) {
            const uint8_t cc = static_cast<uint8_t>(s[i + static_cast<size_t>(k)]);
            if ((cc & 0xC0) != 0x80) {
                ++i;
                return 0xFFFD;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        i += static_cast<size_t>(len) + 1;
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return 0xFFFD;
        }
        return cp;
    }

}  // namespace qifeng
