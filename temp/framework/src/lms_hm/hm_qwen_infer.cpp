/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * File: hm_qwen_infer.cpp
 * Description:
 *   Qwen3.6 推理引擎实现，适配 HM runtime（tcim）。
 */
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <openssl/evp.h>

#include "common/logger.h"
#include "common/utils/batch_executor.h"
#include "lms_hm/hm_qwen_infer.h"
#include "lms_hm/prompt_data.h"

namespace qifeng {

    namespace lmshm {

        namespace {
            // 检查 tcim 调用返回值，失败时抛出异常。
            void ThrowIfError(tcim::Status status, const std::string& what) {
                if (status != tcim::Status::OK) {
                    throw std::runtime_error(what + " failed, status = " + std::to_string(static_cast<int>(status)));
                }
            }

            // 对 logits 施加 repetition_penalty 与 presence_penalty 后取 argmax，
            // 与 Python demo 的 SamplingManager.process_logits 逻辑一致：
            // 已生成过的 token 先做 repetition penalty（logit < 0 乘 penalty，否则除 penalty），
            // 再减去 presence_penalty。
            template <typename T>
            int ArgMaxWithPenalties(const T* ptr, std::size_t n, float repetitionPenalty, float presencePenalty,
                                    const std::vector<int32_t>& generatedIds) {
                if (ptr == nullptr || n == 0) {
                    return -1;
                }
                const std::unordered_set<int32_t> seen(generatedIds.begin(), generatedIds.end());
                int best = 0;
                float bestValue = -std::numeric_limits<float>::infinity();
                for (std::size_t i = 0; i < n; ++i) {
                    float value = static_cast<float>(ptr[i]);
                    if (seen.count(static_cast<int32_t>(i)) != 0) {
                        if (value < 0.0f) {
                            value *= repetitionPenalty;
                        } else {
                            value /= repetitionPenalty;
                        }
                        value -= presencePenalty;
                    }
                    if (value > bestValue) {
                        bestValue = value;
                        best = static_cast<int>(i);
                    }
                }
                return best;
            }

            // ── 时间预估（基于 HM 实测数据标定）──────────────────────────────
            // 输入侧综合耗时（prefill + embedding）：约 3.3 ms/token。
            constexpr double kInputMsPerToken = 3.3;
            // 输出侧 decode：实测约 6.37 tok/s，折合约 157 ms/token。
            constexpr double kDecodeMsPerToken = 157.0;
            // 纪要主输出 token 数预估（分段拟合实测数据）：
            // 短文本输出随输入近似线性增长（拟合斜率 0.45），长文本趋于平缓。
            constexpr std::size_t kShortTextThreshold = 2500;  // 短/长文本分界（字符）
            constexpr double kOverviewShortBase = 400.0;       // 短文本：输出 ≈ 400 + 0.45 × 输入字符
            constexpr double kOverviewShortPerInputChar = 0.45;
            constexpr double kOverviewLongBase = 500.0;  // 长文本：输出 = 500 + 0.15 × 输入字符
            constexpr double kOverviewLongPerInputChar = 0.15;
            constexpr std::size_t kOverviewTokenCap = 2200;  // 输出上限

            constexpr std::size_t BaseTime = 180000;  // 基础时间（ms）

            // 单次 LLM 调用耗时预估（单位：ms）。
            inline std::chrono::milliseconds EstimateCallCost(std::size_t inputTokens, std::size_t outputTokens) {
                const double ms = static_cast<double>(inputTokens) * kInputMsPerToken +
                                  static_cast<double>(outputTokens) * kDecodeMsPerToken;
                return std::chrono::milliseconds {static_cast<long long>(ms)};
            }

            // 纪要主输出 token 数预估。
            inline std::size_t EstimateOverviewTokens(std::size_t inputChars) {
                if (inputChars <= kShortTextThreshold) {
                    return static_cast<std::size_t>(kOverviewShortBase +
                                                    static_cast<double>(inputChars) * kOverviewShortPerInputChar);
                }
                const double v = kOverviewLongBase + static_cast<double>(inputChars) * kOverviewLongPerInputChar;
                return static_cast<std::size_t>(v >= kOverviewTokenCap ? kOverviewTokenCap : v);
            }

            // 后处理：将 overview 中某个会议基本信息字段所在整行替换为用户注入的值。
            // 仅当 value 非空（即用户确实注入了该字段）时才替换。
            static void ReplaceMettingInfoLine(std::string& overview, const std::string& label,
                                               const std::string& value) {
                if (value.empty()) {
                    return;
                }
                const std::string marker = "**" + label + "**";
                const auto markerPos = overview.find(marker);
                if (markerPos == std::string::npos) {
                    return;
                }
                const auto lineStart = overview.rfind('\n', markerPos);
                const auto begin = (lineStart == std::string::npos) ? 0 : lineStart + 1;
                const auto lineEnd = overview.find('\n', markerPos);
                const auto end = (lineEnd == std::string::npos) ? overview.size() : lineEnd;
                overview.replace(begin, end - begin, "- **" + label + "**：" + value);
            }

            // 后处理：依次把会议时间/地点/主持人/参会人员注入到 overview 对应行。
            static void InjectMettingInfoFields(std::string& overview, const MettingInfo& info) {
                ReplaceMettingInfoLine(overview, "会议时间", info.date);
                ReplaceMettingInfoLine(overview, "会议地点", info.location);
                ReplaceMettingInfoLine(overview, "主持人", info.host);
                ReplaceMettingInfoLine(overview, "参会人员", info.attendees);
            }

            // ── 模型文件 MD5 校验辅助函数 ────────────────────────────────
            // 将二进制 MD5 摘要转换为 32 位小写十六进制字符串。
            std::string ToLowerHex(const unsigned char* digest, std::size_t len) {
                std::ostringstream oss;
                oss << std::hex << std::setfill('0');
                for (std::size_t i = 0; i < len; ++i) {
                    oss << std::setw(2) << static_cast<int>(digest[i]);
                }
                return oss.str();
            }

            // ASCII 字符串转小写。
            std::string ToLower(std::string s) {
                for (char& c : s) {
                    if (c >= 'A' && c <= 'Z') {
                        c = static_cast<char>(c - 'A' + 'a');
                    }
                }
                return s;
            }

            // 计算文件 MD5，返回 32 位小写十六进制字符串；文件无法读取时返回空串。
            std::string HashFileMd5(const std::string& filePath) {
                std::ifstream ifs(filePath, std::ios::binary);
                if (!ifs) {
                    return "";
                }
                EVP_MD_CTX* ctx = EVP_MD_CTX_new();
                if (ctx == nullptr || EVP_DigestInit_ex(ctx, EVP_md5(), nullptr) != 1) {
                    EVP_MD_CTX_free(ctx);
                    return "";
                }
                std::array<char, 1024 * 1024> buf {};
                while (ifs.read(buf.data(), static_cast<std::streamsize>(buf.size())) || ifs.gcount() > 0) {
                    EVP_DigestUpdate(ctx, buf.data(), static_cast<std::size_t>(ifs.gcount()));
                }
                std::array<unsigned char, EVP_MAX_MD_SIZE> digest {};
                unsigned int digestLen = 0;
                EVP_DigestFinal_ex(ctx, digest.data(), &digestLen);
                EVP_MD_CTX_free(ctx);
                return ToLowerHex(digest.data(), digestLen);
            }

            // 读取 .md5 文件中的期望 MD5（取首个空白分隔字段并转小写）。
            // 兼容 "hash" 与 "hash  filename" 两种格式；文件无法读取时返回空串。
            std::string ReadExpectedMd5(const std::string& checksumPath) {
                std::ifstream ifs(checksumPath);
                if (!ifs) {
                    return "";
                }
                std::string line;
                std::getline(ifs, line);
                const auto end = line.find_first_of(" \t\r\n");
                const std::string hash = (end == std::string::npos) ? line : line.substr(0, end);
                return ToLower(hash);
            }

            // 校验单个模型文件：读取 .md5 期望 MD5，与实际 MD5 比对，返回该校验结果。
            ModelVerifyStatus VerifySingleFile(const std::string& filePath) {
                ModelVerifyStatus status;
                const std::string expected = ReadExpectedMd5(filePath + ".md5");
                const std::string actual = HashFileMd5(filePath);

                if (expected.empty()) {
                    status.corrupted = true;
                    status.info += "缺少校验文件: " + filePath + ".md5\n";
                } else if (actual.empty()) {
                    status.corrupted = true;
                    status.info += "无法读取模型文件: " + filePath + "\n";
                } else if (actual != expected) {
                    status.corrupted = true;
                    status.info += "MD5 不匹配: " + filePath + " (期望 " + expected + ", 实际 " + actual + ")\n";
                }
                return status;
            }
        }  // namespace

        // ModelVerifyStatus 因 corrupted 为原子变量而不可拷贝，显式提供移动语义。
        ModelVerifyStatus::ModelVerifyStatus(ModelVerifyStatus&& other) noexcept
            : corrupted(other.corrupted.load(std::memory_order_relaxed)), info(std::move(other.info)) {
        }

        ModelVerifyStatus& ModelVerifyStatus::operator=(ModelVerifyStatus&& other) noexcept {
            if (this != &other) {
                info = std::move(other.info);
                // 先写 info，再以 release 顺序发布 corrupted，保证读取方 acquire 后能看到完整 info。
                corrupted.store(other.corrupted.load(std::memory_order_relaxed), std::memory_order_release);
            }
            return *this;
        }

        MettingInfo::MettingInfo(const std::string& dateInput, const std::string& locationInput,
                                 const std::string& hostInput, const std::string& attendeesInput,
                                 const std::string& textInput)
            : date {dateInput}, location {locationInput}, host {hostInput}, attendees {attendeesInput}, text {
                                                                                                            textInput} {
            text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        }

        std::optional<lms::SummaryError> MettingInfo::Validate() const {
            if (!utf8::is_valid(date)) {
                return lms::SummaryError {lms::SummaryError::StateCode::INVALID_DATA, "date is invalid"};
            }
            if (!utf8::is_valid(location)) {
                return lms::SummaryError {lms::SummaryError::StateCode::INVALID_DATA, "location is invalid"};
            }
            if (!utf8::is_valid(host)) {
                return lms::SummaryError {lms::SummaryError::StateCode::INVALID_DATA, "host is invalid"};
            }
            if (!utf8::is_valid(attendees)) {
                return lms::SummaryError {lms::SummaryError::StateCode::INVALID_DATA, "attendees is invalid"};
            }
            if (!utf8::is_valid(text)) {
                return lms::SummaryError {lms::SummaryError::StateCode::INVALID_DATA, "text is invalid"};
            }
            std::size_t count = Utf8Length(text);
            SLOG_INFO << "Input text length: " << count;
            if (count < 500) {
                return lms::SummaryError {lms::SummaryError::StateCode::INPUT_TOO_SHORT, "text is too short"};
            } else if (count > 60000) {
                return lms::SummaryError {lms::SummaryError::StateCode::INPUT_TOO_LONG, "text is too long"};
            }
            return std::nullopt;
        }

        HmQwenInfer::HmQwenInfer(const ModelPathConfig& config) {
            mVerifyThread = std::thread(qifeng::lmshm::HmQwenInfer::VerifyModelFiles, config);

            mPrefillModelPath = config.prefillModel;
            mDecodeModelPath = config.decodeModel;

            // 1. 创建权重管理器，prefill / decode 共享同一份权重内存。
            //    必须显式指定 Xh2HalBackend 后端，否则 runtime 会回退到默认的
            tcim::DevManager devManager = tcim::DevManager::Create(std::vector<int> {config.deviceId}, "Xh2HalBackend");
            mWeightManager = tcim::Module::WeightManager::CreateWeightManager(devManager);
            tcim::Module::Option optionPrefill(mWeightManager);
            tcim::Module::Option optionDecode(mWeightManager);

            // 2. 加载 prefill 模型。
            mPrefillModule = std::make_shared<tcim::Module>();
            ThrowIfError(mPrefillModule->LoadModel(mPrefillModelPath, optionPrefill),
                         "Load prefill model: " + mPrefillModelPath);
            SLOG_INFO << "Prefill model loaded: " << mPrefillModelPath.c_str() << " done!";

            // 3. 收集 full-attention KV cache 输入名，作为 decode 的 dummy tensor，
            //    加载 decode 时不为这些张量分配独立内存（运行时与 prefill 共享）。
            std::vector<std::string> dummyNames;
            for (std::size_t i = 0; i < mPrefillModule->GetInputNum(); ++i) {
                const std::string name = mPrefillModule->GetInputName(i);
                if (name.find("model_layers") != std::string::npos) {
                    dummyNames.push_back(name);
                }
            }
            optionDecode.SetDummyTensors(dummyNames);

            mDecodeModule = std::make_shared<tcim::Module>();
            ThrowIfError(mDecodeModule->LoadModel(mDecodeModelPath, optionDecode),
                         "Load decode model: " + mDecodeModelPath);
            SLOG_INFO << "Decode model loaded: " << mDecodeModelPath.c_str() << " done!";

            // 4. 提取关键维度。
            //    prefill 输入 0: [batch, prefillLength, embeddingLen]
            const tcim::TensorInfo prefill_input_info = mPrefillModule->GetInputInfo(mPrefillModule->GetInputName(0));
            if (prefill_input_info.Shape().size() < 3) {
                throw std::runtime_error("Unexpected prefill input shape (expect 3 dims)");
            }
            mPrefillLength = static_cast<int>(prefill_input_info.Shape()[1]);
            mEmbeddingLength = static_cast<int>(prefill_input_info.Shape()[2]);

            //    decode 输入 7 为第一个 cache 输入，其 shape[2] 为最大上下文长度。
            const tcim::TensorInfo decode_cache_info = mDecodeModule->GetInputInfo(mDecodeModule->GetInputName(7));
            if (decode_cache_info.Shape().size() < 3) {
                throw std::runtime_error("Unexpected decode cache shape (expect >= 3 dims)");
            }
            mContextMaxLength = static_cast<int>(decode_cache_info.Shape()[2]);

            mBatch = static_cast<int>(mDecodeModule->GetInputInfo(mDecodeModule->GetInputName(0)).Shape()[0]);

            const tcim::TensorInfo decode_output_info = mDecodeModule->GetOutputInfo(mDecodeModule->GetOutputName(0));
            mArgmaxDimLen = static_cast<int>(decode_output_info.Shape()[2]);

            // 5. 关联 cache 张量（full-attention KV 以及 linear-attention 循环状态）。
            WireCaches();

            // 6. 清空 linear-attention 的 conv_cache / recurrent_state。
            ClearCache();

            // 7. 设置 decode 的 current_length 输入为 1（每个 decode step 处理 1 个 token）。
            SetDecodeCurrentLength();

            // 8. 初始化 tokenizer 与 embedding。
            mTokenizer = std::make_shared<HmTokenizer>(config.tokenizerJson, config.embeddingBin, mEmbeddingLength,
                                                       mPrefillLength);

            // 9. 结束符 token id。
            std::vector<int32_t> eosIds = mTokenizer->Encode("<|im_end|>");
            if (eosIds.empty()) {
                throw std::runtime_error("Cannot resolve <|im_end|> token id");
            }
            mEosTokenId = eosIds[0];

            // 10. 预分配每步复用的 host 缓冲。
            mPrefillPosBuffer.resize(mPrefillLength, 0);
            mDecodePosBuffer.resize(1, 0);
            mPrefillAttnMaskBuffer.resize(mPrefillLength, tensor_type(0.0f));
            mDecodeAttnMaskBuffer.resize(1, tensor_type(0.0f));
        }

        HmQwenInfer::~HmQwenInfer() {
            // 等待后台校验线程结束，保证正常退出路径下线程被回收。
            if (mVerifyThread.joinable()) {
                mVerifyThread.join();
            }
            Unload();
        }

        void HmQwenInfer::Unload() {
            mPrefillModule.reset();
            mDecodeModule.reset();
            mTokenizer.reset();
            // 权重内存由 WeightManager 统一持有，须显式释放才能真正归还 DDR。
            mWeightManager = tcim::Module::WeightManager {};
        }

        ModelVerifyStatus HmQwenInfer::sVerifyStatus;

        const ModelVerifyStatus& HmQwenInfer::GetVerifyStatus() const {
            return sVerifyStatus;
        }

        void HmQwenInfer::VerifyModelFiles(const ModelPathConfig& config) {
            // 仅校验实际加载的三个文件：prefill 模型、decode 模型、embedding 权重。
            const std::vector<std::string> modelFiles = {
                config.prefillModel,
                config.decodeModel,
                config.embeddingBin,
            };

            // 通过 BatchExecutor 线程池并行校验各文件，收集 future 用于等待与汇总。
            BatchExecutor& executor = BatchExecutor::GetInstance();
            std::vector<std::future<ModelVerifyStatus>> futures;
            for (const std::string& filePath : modelFiles) {
                if (filePath.empty()) {
                    continue;
                }
                futures.push_back(executor.AddTask(VerifySingleFile, filePath));
            }

            // 汇总各文件的校验结果。
            ModelVerifyStatus status;
            for (auto& future : futures) {
                const ModelVerifyStatus fileStatus = future.get();
                if (fileStatus.corrupted) {
                    status.corrupted = true;
                    status.info += fileStatus.info;
                }
            }

            // 结果写入静态成员，供加载完成后查询。
            sVerifyStatus = std::move(status);
            SLOG_DEBUG << "[VerifyModelFiles] Verify model files done! corrupted: "
                       << sVerifyStatus.corrupted.load(std::memory_order_acquire);
        }

        std::shared_ptr<tcim::Module> HmQwenInfer::GetModule(int modelType) {
            if (modelType == 0) {
                return mPrefillModule;
            }
            if (modelType == 1) {
                return mDecodeModule;
            }
            return nullptr;
        }

        std::shared_ptr<HmTokenizer> HmQwenInfer::GetTokenizer() {
            return mTokenizer;
        }

        void HmQwenInfer::WireCaches() {
            for (std::size_t i = 0; i < mPrefillModule->GetInputNum(); ++i) {
                const std::string inputName = mPrefillModule->GetInputName(i);

                if (inputName.find("model_layers") != std::string::npos) {
                    // full-attention KV cache：prefill 与 decode 共享同一块设备内存。
                    tcim::Tensor cache = mPrefillModule->GetDevInput(inputName);
                    ThrowIfError(mDecodeModule->SetDevInput(inputName, cache), "SetDevInput(" + inputName + ")");
                }

                if (inputName.find("conv_cache") != std::string::npos) {
                    // linear-attention 卷积缓存：输入与输出为同一块内存（循环状态原地更新）。
                    const std::string outputName = ReplaceAll(inputName, "past_conv_cache_", "conv_cache_out_");
                    tcim::Tensor cache = mPrefillModule->GetDevInput(inputName);
                    ThrowIfError(mPrefillModule->SetDevOutput(outputName, cache),
                                 "SetDevOutput(" + outputName + ") on prefill");
                    ThrowIfError(mDecodeModule->SetDevInput(inputName, cache),
                                 "SetDevInput(" + inputName + ") on decode");
                    ThrowIfError(mDecodeModule->SetDevOutput(outputName, cache),
                                 "SetDevOutput(" + outputName + ") on decode");
                }

                if (inputName.find("recurrent_state") != std::string::npos) {
                    // linear-attention 循环状态：同样为原地更新。
                    const std::string outputName =
                        ReplaceAll(inputName, "past_recurrent_state_", "recurrent_state_out_");
                    tcim::Tensor cache = mPrefillModule->GetDevInput(inputName);
                    ThrowIfError(mPrefillModule->SetDevOutput(outputName, cache),
                                 "SetDevOutput(" + outputName + ") on prefill");
                    ThrowIfError(mDecodeModule->SetDevInput(inputName, cache),
                                 "SetDevInput(" + inputName + ") on decode");
                    ThrowIfError(mDecodeModule->SetDevOutput(outputName, cache),
                                 "SetDevOutput(" + outputName + ") on decode");
                }
            }
        }

        void HmQwenInfer::ClearCache() {
            // conv_cache / recurrent_state 在 prefill 与 decode 之间共享同一设备内存，
            // 因此只需对 prefill 侧清零一次即可。
            for (std::size_t i = 0; i < mPrefillModule->GetInputNum(); ++i) {
                const std::string name = mPrefillModule->GetInputName(i);
                if (name.find("conv_cache") != std::string::npos || name.find("recurrent_state") != std::string::npos) {
                    const tcim::TensorInfo info = mPrefillModule->GetDevInput(name).Info().AsContiguous();
                    tcim::Tensor hostZero = tcim::Tensor::CreateHostTensor(info);
                    std::memset(hostZero.Data(), 0, hostZero.MemSize());
                    ThrowIfError(mPrefillModule->SetInput(name, hostZero), "ClearCache SetInput(" + name + ")");
                }
            }
            ThrowIfError(mPrefillModule->Sync(), "ClearCache prefill Sync");
        }

        void HmQwenInfer::SetDecodeCurrentLength() {
            const std::string name = mDecodeModule->GetInputName(5);
            const tcim::TensorInfo info = mDecodeModule->GetInputInfo(name).AsContiguous();
            tcim::Tensor tensor = tcim::Tensor::CreateHostTensor(info, info.MemSize(), &mDecodeCurrentLength);
            ThrowIfError(mDecodeModule->SetInput(name, tensor), "SetInput decode current_length");
        }

        int HmQwenInfer::OutputArgMax(tcim::Module* module) {
            const std::string name = module->GetOutputName(0);
            tcim::Tensor output = module->GetOutput(name);  // 连续 host 张量

            switch (output.Info().DataType()) {
                case tcim::DataType::FLOAT32:
                    return ArgMax<float>(static_cast<float*>(output.Data()), mArgmaxDimLen);
                case tcim::DataType::FLOAT16:
                    return ArgMax<tensor_type>(static_cast<tensor_type*>(output.Data()), mArgmaxDimLen);
                case tcim::DataType::INT32:
                    return ArgMax<int32_t>(static_cast<int32_t*>(output.Data()), mArgmaxDimLen);
                // 其余整型（INT8/UINT8/INT16/UINT16/UINT32）与本模块无关，统一走 fp32 兜底。
                case tcim::DataType::INT8:
                case tcim::DataType::UINT8:
                case tcim::DataType::INT16:
                case tcim::DataType::UINT16:
                case tcim::DataType::UINT32:
                default: {
                    // 兜底：转换为 fp32 后再取 argmax。
                    tcim::TensorInfo info32 = output.Info().AsType(tcim::DataType::FLOAT32).AsContiguous();
                    tcim::Tensor host32 = tcim::Tensor::CreateHostTensor(info32);
                    ThrowIfError(output.CastTo(host32), "OutputArgMax CastTo");
                    return ArgMax<float>(static_cast<float*>(host32.Data()), mArgmaxDimLen);
                }
            }
        }

        int HmQwenInfer::OutputArgMaxWithPenalties(tcim::Module* module, const std::vector<int32_t>& generatedIds,
                                                   float repetitionPenalty, float presencePenalty) {
            const std::string name = module->GetOutputName(0);
            tcim::Tensor output = module->GetOutput(name);

            switch (output.Info().DataType()) {
                case tcim::DataType::FLOAT32:
                    return ArgMaxWithPenalties<float>(static_cast<float*>(output.Data()), mArgmaxDimLen,
                                                      repetitionPenalty, presencePenalty, generatedIds);
                case tcim::DataType::FLOAT16:
                    return ArgMaxWithPenalties<tensor_type>(static_cast<tensor_type*>(output.Data()), mArgmaxDimLen,
                                                            repetitionPenalty, presencePenalty, generatedIds);
                case tcim::DataType::INT32:
                    return ArgMax<int32_t>(static_cast<int32_t*>(output.Data()), mArgmaxDimLen);
                // 其余整型（INT8/UINT8/INT16/UINT16/UINT32）与本模块无关，统一走 fp32 兜底。
                case tcim::DataType::INT8:
                case tcim::DataType::UINT8:
                case tcim::DataType::INT16:
                case tcim::DataType::UINT16:
                case tcim::DataType::UINT32:
                default: {
                    tcim::TensorInfo info32 = output.Info().AsType(tcim::DataType::FLOAT32).AsContiguous();
                    tcim::Tensor host32 = tcim::Tensor::CreateHostTensor(info32);
                    ThrowIfError(output.CastTo(host32), "OutputArgMaxWithPenalties CastTo");
                    return ArgMaxWithPenalties<float>(static_cast<float*>(host32.Data()), mArgmaxDimLen,
                                                      repetitionPenalty, presencePenalty, generatedIds);
                }
            }
        }

        tcim::Tensor HmQwenInfer::MakeHostInput(tcim::Module* module, const std::string& name, void* ptr) {
            const tcim::TensorInfo info = module->GetInputInfo(name).AsContiguous();
            return tcim::Tensor::CreateHostTensor(info, info.MemSize(), ptr);
        }

        void HmQwenInfer::PrefillSetInputDatas(void* data, int32_t validLength, int32_t currentLength) {
            mPrefillValidLength = validLength;
            mPrefillCurrentLength = currentLength;

            // 位置编码：time / height / width 三者相同，均为 [validLength, ...)。
            for (int i = 0; i < mPrefillLength; ++i) {
                mPrefillPosBuffer[i] = validLength + i;
            }
            // linear attention mask：前 currentLength 个位置为 1，其余为 0。
            std::fill(mPrefillAttnMaskBuffer.begin(), mPrefillAttnMaskBuffer.end(), tensor_type(0.0f));
            for (int i = 0; i < currentLength; ++i) {
                mPrefillAttnMaskBuffer[i] = tensor_type(1.0f);
            }

            const std::string inputName = mPrefillModule->GetInputName(0);
            const std::string timeName = mPrefillModule->GetInputName(1);
            const std::string heightName = mPrefillModule->GetInputName(2);
            const std::string widthName = mPrefillModule->GetInputName(3);
            const std::string validName = mPrefillModule->GetInputName(4);
            const std::string currentName = mPrefillModule->GetInputName(5);
            const std::string maskName = mPrefillModule->GetInputName(6);

            ThrowIfError(mPrefillModule->SetInput(inputName, MakeHostInput(mPrefillModule.get(), inputName, data)),
                         "prefill SetInput embedding");
            ThrowIfError(mPrefillModule->SetInput(
                             timeName, MakeHostInput(mPrefillModule.get(), timeName, mPrefillPosBuffer.data())),
                         "prefill SetInput time_pos");
            ThrowIfError(mPrefillModule->SetInput(
                             heightName, MakeHostInput(mPrefillModule.get(), heightName, mPrefillPosBuffer.data())),
                         "prefill SetInput height_pos");
            ThrowIfError(mPrefillModule->SetInput(
                             widthName, MakeHostInput(mPrefillModule.get(), widthName, mPrefillPosBuffer.data())),
                         "prefill SetInput width_pos");
            ThrowIfError(mPrefillModule->SetInput(validName,
                                                  MakeHostInput(mPrefillModule.get(), validName, &mPrefillValidLength)),
                         "prefill SetInput valid_length");
            ThrowIfError(mPrefillModule->SetInput(
                             currentName, MakeHostInput(mPrefillModule.get(), currentName, &mPrefillCurrentLength)),
                         "prefill SetInput current_length");
            ThrowIfError(mPrefillModule->SetInput(
                             maskName, MakeHostInput(mPrefillModule.get(), maskName, mPrefillAttnMaskBuffer.data())),
                         "prefill SetInput linear_attn_mask");
        }

        void HmQwenInfer::PrefillInfer() {
            ThrowIfError(mPrefillModule->Run(), "prefill Run");
            ThrowIfError(mPrefillModule->Sync(), "prefill Sync");
        }

        void HmQwenInfer::PrefillGetOutputDatas(std::vector<int32_t>& ids) {
            ids.push_back(OutputArgMax(mPrefillModule.get()));
        }

        void HmQwenInfer::DecodeSetInputDatas(void* data, int32_t contextLength) {
            mDecodeValidLength = contextLength;
            mDecodePosBuffer[0] = contextLength;
            mDecodeAttnMaskBuffer[0] = tensor_type(1.0f);

            const std::string inputName = mDecodeModule->GetInputName(0);
            const std::string timeName = mDecodeModule->GetInputName(1);
            const std::string heightName = mDecodeModule->GetInputName(2);
            const std::string widthName = mDecodeModule->GetInputName(3);
            const std::string validName = mDecodeModule->GetInputName(4);
            const std::string maskName = mDecodeModule->GetInputName(6);
            // current_length（输入 5）已在构造阶段设为 1。

            ThrowIfError(mDecodeModule->SetInput(inputName, MakeHostInput(mDecodeModule.get(), inputName, data)),
                         "decode SetInput embedding");
            ThrowIfError(mDecodeModule->SetInput(timeName,
                                                 MakeHostInput(mDecodeModule.get(), timeName, mDecodePosBuffer.data())),
                         "decode SetInput time_pos");
            ThrowIfError(mDecodeModule->SetInput(
                             heightName, MakeHostInput(mDecodeModule.get(), heightName, mDecodePosBuffer.data())),
                         "decode SetInput height_pos");
            ThrowIfError(mDecodeModule->SetInput(
                             widthName, MakeHostInput(mDecodeModule.get(), widthName, mDecodePosBuffer.data())),
                         "decode SetInput width_pos");
            ThrowIfError(
                mDecodeModule->SetInput(validName, MakeHostInput(mDecodeModule.get(), validName, &mDecodeValidLength)),
                "decode SetInput valid_length");
            ThrowIfError(mDecodeModule->SetInput(
                             maskName, MakeHostInput(mDecodeModule.get(), maskName, mDecodeAttnMaskBuffer.data())),
                         "decode SetInput linear_attn_mask");
        }

        void HmQwenInfer::DecodeInfer() {
            ThrowIfError(mDecodeModule->Run(), "decode Run");
            ThrowIfError(mDecodeModule->Sync(), "decode Sync");
        }

        void HmQwenInfer::DecodeGetOutputDatas(std::vector<int32_t>& ids, const std::vector<int32_t>& generatedIds) {
            ids.push_back(
                OutputArgMaxWithPenalties(mDecodeModule.get(), generatedIds, mRepetitionPenalty, mPresencePenalty));
        }

        void HmQwenInfer::CheckCancellation() const {
            if (mCancelRequested.load(std::memory_order_relaxed)) {
                throw OperationCancelled();
            }
        }

        void HmQwenInfer::ResetContextAfterCancel() {
            // 与 Python 参考实现取消逻辑一致：复位上下文并清空循环状态，
            // 模型保持常驻，以便后续任务可正常启动。
            mContextLength = 0;
            ClearCache();
        }

        void HmQwenInfer::Cancel() {
            // 先置取消标志（原子），再尝试推进状态机 Running -> Cancelling。
            mCancelRequested.store(true, std::memory_order_relaxed);
            TaskState expected = TaskState::Running;
            mTaskState.compare_exchange_strong(expected, TaskState::Cancelling);
        }

        TaskState HmQwenInfer::GetTaskState() const {
            return mTaskState.load(std::memory_order_relaxed);
        }

        void HmQwenInfer::ResetContext() {
            std::lock_guard<std::mutex> lock(mInferMutex);
            mContextLength = 0;
            ClearCache();
        }

        std::string HmQwenInfer::GenerateInternal(const std::string& msg, const std::string_view& systemPrompt,
                                                  bool silent, PerfInfos& perf, const HmStreamCallback& callback) {
            const auto tStart = std::chrono::high_resolution_clock::now();

            // 1. 渲染 chat template 并编码。
            std::vector<Message> msgs = {{"system", std::string(systemPrompt)}, {"user", msg}};
            std::string rendered = mTokenizer->ApplyChatTemplate(msgs, true, false);
            std::vector<int32_t> allInputIds = mTokenizer->Encode(rendered);
            const int32_t inputEchoLen = static_cast<int32_t>(allInputIds.size());
            if (inputEchoLen > mContextMaxLength) {
                throw std::runtime_error("input longer than " + std::to_string(mContextMaxLength) +
                                         ", please shorten it!");
            }

            // 2. 单轮对话：重置上下文与循环状态。
            mContextLength = 0;
            ClearCache();

            // 3. prefill 阶段（按 mPrefillLength 分块）。
            const int prefillLoopRound = static_cast<int>(std::ceil(static_cast<float>(inputEchoLen) / mPrefillLength));
            tensor_type* inputDatas = nullptr;

            for (int round = 0; round < prefillLoopRound; ++round) {
                CheckCancellation();
                const int32_t validLength = round * mPrefillLength + mContextLength;
                int32_t currentLength = 0;
                std::vector<int32_t> inputIds;

                if (round == prefillLoopRound - 1) {
                    currentLength = inputEchoLen - round * mPrefillLength;
                    inputIds.assign(allInputIds.end() - currentLength, allInputIds.end());
                } else {
                    currentLength = mPrefillLength;
                    inputIds.assign(allInputIds.begin() + round * mPrefillLength,
                                    allInputIds.begin() + (round + 1) * mPrefillLength);
                }

                auto tEmbed = std::chrono::high_resolution_clock::now();
                inputDatas = mTokenizer->EmbeddingTokens(inputIds);
                perf.embeddingTime +=
                    std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - tEmbed)
                        .count();

                PrefillSetInputDatas(inputDatas, validLength, currentLength);

                auto tPrefill = std::chrono::high_resolution_clock::now();
                PrefillInfer();
                perf.prefillTime +=
                    std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - tPrefill)
                        .count();
            }

            // 4. 取出首 token。
            std::vector<int32_t> ids;
            PrefillGetOutputDatas(ids);
            const std::string first = mTokenizer->Decode(ids);
            perf.ttftTime =
                std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - tStart).count();

            std::vector<int32_t> chatHistoryIds = allInputIds;
            chatHistoryIds.push_back(ids[0]);
            std::vector<int32_t> generatedIds;  // 已生成 token id，供 repetition_penalty 使用
            generatedIds.push_back(ids[0]);

            // 5. 首 token embedding 供 decode 使用。
            auto tEmbed = std::chrono::high_resolution_clock::now();
            inputDatas = mTokenizer->EmbeddingTokens(ids);
            perf.embeddingTime +=
                std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - tEmbed).count();

            std::string allResponse = first;
            mContextLength = inputEchoLen;

            if (!silent) {
                std::cout << "Response : " << first;
                std::cout.flush();
            }
            if (callback && !first.empty()) {
                callback(first);
            }

            uint32_t decodeCount = 0;
            uint32_t skipTokens = 0;
            const uint32_t slideLen = 10;

            std::string lastResponse = mTokenizer->Decode(LastN(chatHistoryIds, slideLen));
            std::string decodeResponse;

            // 6. decode 阶段（自回归生成）。
            while (true) {
                CheckCancellation();
                if (mContextLength > mContextMaxLength) {
                    break;
                }

                DecodeSetInputDatas(static_cast<void*>(inputDatas), static_cast<int32_t>(mContextLength));

                auto tDecode = std::chrono::high_resolution_clock::now();
                DecodeInfer();
                perf.decodeTime +=
                    std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - tDecode)
                        .count();

                ids.clear();
                DecodeGetOutputDatas(ids, generatedIds);
                ++decodeCount;

                if (ids[0] == mEosTokenId) {
                    if (!silent) {
                        std::cout << decodeResponse << std::endl;
                    }
                    allResponse += decodeResponse;
                    if (callback && !decodeResponse.empty()) {
                        callback(decodeResponse);
                    }
                    break;
                }

                chatHistoryIds.push_back(ids[0]);
                generatedIds.push_back(ids[0]);

                // 增量解码（滑动窗口），仅输出完整、有效的字符。
                const int substart = static_cast<int>(Utf8Len(lastResponse));
                const std::vector<int32_t> window = LastN(chatHistoryIds, slideLen + skipTokens + 1);
                const std::u32string udecode = Utf8ToU32(mTokenizer->Decode(window)).substr(substart);
                decodeResponse = U32ToUtf8(udecode);

                if (!decodeResponse.empty() && IsValidChar(udecode.back())) {
                    if (!silent) {
                        std::cout << decodeResponse << std::flush;
                    }
                    allResponse += decodeResponse;
                    if (callback && !callback(decodeResponse)) {
                        break;  // 调用方请求提前终止
                    }
                    decodeResponse.clear();  // 已追加输出，避免 EOS 分支重复追加导致末尾重复
                    lastResponse = mTokenizer->Decode(LastN(chatHistoryIds, slideLen));
                    skipTokens = 0;
                } else {
                    ++skipTokens;
                }

                auto tEmbed2 = std::chrono::high_resolution_clock::now();
                inputDatas = mTokenizer->EmbeddingTokens(ids);
                perf.embeddingTime +=
                    std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - tEmbed2)
                        .count();

                mContextLength = mContextLength + 1;
            }

            perf.inputTokens = inputEchoLen;
            perf.outputTokens = static_cast<int>(decodeCount) + 1;
            return allResponse;
        }

        lms::SummaryResult HmQwenInfer::Summarize(const MettingInfo& meetingInfo, const MettingHints& hints,
                                                  const ProgressObserver& progressObserver) {
            if (auto err = meetingInfo.Validate(); err) {
                SLOG_ERROR << "MettingInfo is invalid, code = " << static_cast<int>(err->code)
                           << ", reason = " << err->reason;
                return err.value();
            }
            std::lock_guard<std::mutex> lock(mInferMutex);
            if (mTaskState.load() == TaskState::Running) {
                throw std::runtime_error("another task is already running");
            }
            mCancelRequested.store(false);
            mTaskState.store(TaskState::Running);

            try {
                const auto tSummarizeStart = std::chrono::high_resolution_clock::now();

                // 开始时预估总耗时，并上报一次（spentTime = 0）。
                if (progressObserver) {
                    const std::size_t inputChars = Utf8Length(meetingInfo.text);
                    const std::size_t inputTokens = mTokenizer->Encode(meetingInfo.text).size();
                    const std::size_t overviewTokens = EstimateOverviewTokens(inputChars);

                    std::chrono::milliseconds total {0};
                    total += EstimateCallCost(inputTokens, 10);  // 会议类型
                    if (hints.topic.empty()) {
                        total += EstimateCallCost(inputTokens, 100);  // 议题
                    }
                    if (!hints.exampleSummary.empty()) {
                        total += EstimateCallCost(inputTokens, 500);  // 范文风格
                    }
                    total += EstimateCallCost(inputTokens, overviewTokens);  // 纪要
                    total += EstimateCallCost(overviewTokens, 20);           // 关键词

                    total += std::chrono::milliseconds(BaseTime);
                    progressObserver(lms::ProgressStat {std::chrono::milliseconds {0}, total});
                }

                // 提取会议类型。
                PerfInfos p1;
                const std::string meetingType =
                    Trim(GenerateInternal(meetingInfo.text, prompt::ExtractMeetingTypePrompt, true, p1, {}));
                // if (meetingType.find("invalid") != std::string::npos) {
                //     SLOG_ERROR << "[Summarize] 会议类型无效 " << meetingType;
                //     mTaskState.store(TaskState::Completed);
                //     return lms::SummaryError {lms::SummaryError::StateCode::INVALID_DATA, meetingType};
                // }
                SLOG_INFO << "[Summarize] 会议类型: " << meetingType;
                // 提取会议议题。
                std::string topics = "";
                if (!hints.topic.empty()) {
                    topics = hints.topic;
                    SLOG_INFO << "[Summarize] 用户注入议题:\n" << topics;
                } else {
                    PerfInfos p2;
                    topics = Trim(GenerateInternal(meetingInfo.text, prompt::ExtractTopicPrompt, true, p2, {}));
                    SLOG_INFO << "[Summarize] 提取会议议题:\n" << topics;
                }

                // 提取会议示例风格。
                std::string example = "";
                if (!hints.exampleSummary.empty()) {
                    PerfInfos p3;
                    example = Trim(GenerateInternal(meetingInfo.text, prompt::ExtractExampleStylePrompt, true, p3, {}));
                    SLOG_INFO << "[Summarize] 提取范文风格:\n" << example;
                }

                // 生成会议纪要（填充类型与议题）。
                std::string overviewPrompt = ReplacePlaceholders(
                    prompt::OverviewOncePrompt, {
                                                    {"meeting_type", meetingType},
                                                    {"meeting_topic", topics.empty() ? "无" : topics},
                                                    {"example_style", example.empty() ? "无" : example},
                                                });
                PerfInfos p3;
                std::string overview = GenerateInternal(meetingInfo.text, overviewPrompt, true, p3, {});
                SLOG_INFO << "[Summarize] 会议纪要: " << overview.size() << " 字";

                // 基于纪要提取关键词。
                PerfInfos p4;
                std::vector<std::string> keywords =
                    ParseKeywords(GenerateInternal(overview, prompt::ExtractKeywordPrompt, true, p4, {}));
                SLOG_INFO << "[Summarize] 关键词个数: " << keywords.size();

                SpliceKeywords(overview, keywords);

                // 后处理：确保注入的会议基本信息正确。
                InjectMettingInfoFields(overview, meetingInfo);

                // 打印四次大模型调用的性能数据，并统计 Summarize 总耗时。
                auto logPerf = [](const char* label, const PerfInfos& p) {
                    const int decodeCount = p.outputTokens - 1;
                    SLOG_INFO << "[Summarize] " << label << ": 输入 " << p.inputTokens << " tok, 输出 "
                              << p.outputTokens << " tok, Prefill " << std::fixed << std::setprecision(2)
                              << p.prefillTime << " ms, Decode " << p.decodeTime << " ms; Decode Speed: "
                              << (p.decodeTime > 0 ? decodeCount / (p.decodeTime * 0.001f) : 0.0f) << " tokens/s\n";
                };
                logPerf("会议纪要", p3);
                const auto tSummarizeEnd = std::chrono::high_resolution_clock::now();
                const float summarizeTotalMs =
                    std::chrono::duration<float, std::milli>(tSummarizeEnd - tSummarizeStart).count();

                SLOG_INFO << "[Summarize] 总耗时 " << std::fixed << std::setprecision(2) << summarizeTotalMs << " ms";

                mTaskState.store(TaskState::Completed);
                return lms::SummaryData {overview, keywords};
            } catch (const OperationCancelled&) {
                try {
                    ResetContextAfterCancel();
                    mTaskState.store(TaskState::Cancelled);
                } catch (...) {
                    mTaskState.store(TaskState::Failed);
                    throw;  // 清理失败，向上抛清理异常
                }
                throw;  // 清理成功，向上抛 OperationCancelled
            } catch (...) {
                mTaskState.store(TaskState::Failed);
                throw;
            }
        }

    }  // namespace lmshm

}  // namespace qifeng