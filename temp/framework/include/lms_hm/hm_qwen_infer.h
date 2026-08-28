/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * File: hm_qwen_infer.h
 * Description:
 *   Qwen3.6（Qwen3_5ForConditionalGeneration）推理引擎头文件。
 *
 *   适配 HM runtime（tcim）：
 *   - prefill / decode 两个子模型共享 WeightManager
 *   - full-attention KV cache（model_layers_*_self_attn_k/vcache）通过
 *     SetDummyTensors + SetDevInput 与 prefill 共享设备内存
 *   - linear-attention 的 conv_cache / recurrent_state 通过
 *     SetDevInput / SetDevOutput 进行循环状态在块内原地更新
 */

#ifndef QIFENG_FRAMEWORK_LMS_HM_HM_QWEN_INFER_H
#define QIFENG_FRAMEWORK_LMS_HM_HM_QWEN_INFER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "houmo/tcim/tcim_runtime.h"

#include "houmo/tcim/tcim_dev_ctrl.h"
#include "lms/metting.h"
#include "lms_hm/hm_tokenizer.h"
#include "lms_hm/hm_utils.h"
namespace qifeng {

    namespace lmshm {

        /**
         * @brief 性能统计信息。
         */
        struct PerfInfos {
            int inputTokens = 0;         // 输入 token 数
            int outputTokens = 0;        // 输出 token 数
            float ttftTime = 0.0f;       // 首 token 延迟（ms）
            float prefillTime = 0.0f;    // prefill 阶段耗时（ms）
            float decodeTime = 0.0f;     // decode 阶段耗时（ms）
            float embeddingTime = 0.0f;  // embedding 阶段耗时（ms）
        };

        /**
         * @brief 推理任务状态。
         */
        enum class TaskState {
            Idle = 0,    // 空闲，无任务执行
            Running,     // 正在执行任务
            Cancelling,  // 已收到取消请求，正在取消
            Cancelled,   // 任务已被取消（上下文已清理，可再次启动）
            Completed,   // 任务正常完成
            Failed,      // 任务执行失败（异常）
        };

        /**
         * @brief TPU 内存使用情况（单位：字节）
         */
        struct TpuMemUsage {
            uint64_t mTotal = 0;  // TPU 总内存（字节）
            uint64_t mUsed = 0;   // TPU 已用内存（字节）
            uint64_t mFree = 0;   // TPU 可用内存（字节）
        };

        struct TpuInfo {
            TpuMemUsage mMemUsage;
            // 频率（MHz）
            uint32_t mFreqMHz = 0;
            // 温度（摄氏度）
            uint32_t mTempCelsius = 0;
        };

        /**
         * @brief 任务被取消时抛出的异常。
         *
         * 调用方可通过捕获该异常区分"取消"与"正常完成/失败"。
         */
        class OperationCancelled : public std::runtime_error {
        public:
            OperationCancelled() : std::runtime_error("operation cancelled") {
            }
        };

        /**
         * @brief 流式生成回调（引擎自有的轻量回调，供框架层适配流式接口）。
         *
         * @param[in] chunk 增量生成的文本片段
         * @return 是否继续生成；返回 false 表示提前终止
         */
        using HmStreamCallback = std::function<bool(const std::string& chunk)>;

        /**
         * @brief 进度观察回调：Summarize 开始时上报一次预估总耗时。
         */
        using ProgressObserver = std::function<void(lms::ProgressStat)>;

        /**
         * @brief 从配置文件读取的模型路径配置。
         */
        struct ModelPathConfig {
            std::string prefillModel = "";   // prefill 模型路径
            std::string decodeModel = "";    // decode 模型路径
            std::string tokenizerJson = "";  // tokenizer.json 路径
            std::string embeddingBin = "";   // embedding 权重（fp16 .bin）路径
            int deviceId = 1;                // HM 逻辑设备 id（默认 0）
        };

        /**
         * @brief 模型文件校验结果。
         */
        struct ModelVerifyStatus {
            std::atomic<bool> corrupted {false};  // 是否有模型损坏
            std::string info;                     // 损坏详情（文件路径、期望MD5、实际MD5）

            ModelVerifyStatus() = default;
            ModelVerifyStatus(const ModelVerifyStatus&) = delete;
            ModelVerifyStatus& operator=(const ModelVerifyStatus&) = delete;
            ModelVerifyStatus(ModelVerifyStatus&& other) noexcept;
            ModelVerifyStatus& operator=(ModelVerifyStatus&& other) noexcept;
        };

        struct MettingInfo {
            std::string date;       ///< 会议时间
            std::string location;   ///< 会议地点
            std::string host;       ///< 主持人
            std::string attendees;  ///< 参会人员
            std::string text;       ///< 会议原文（必填）

            MettingInfo(const std::string& dateInput, const std::string& locationInput, const std::string& hostInput,
                        const std::string& attendeesInput, const std::string& textInput);

            /**
             * @brief 校验会议信息字段是否合法
             * @return 如果校验通过返回 std::nullopt，否则返回错误描述字符串
             */
            std::optional<lms::SummaryError> Validate() const;
        };

        struct MettingHints {
            std::string topic;           ///< 会议议题（空则自动提取）
            std::string exampleSummary;  ///< 纪要范文文本（空则不学习风格）
        };

        /**
         * @brief Qwen3.6 推理引擎。
         *
         * 输入布局（与 Python 参考实现 qwen3.5 demo 保持一致）：
         *  - prefill 输入: 0=input, 1=time_pos, 2=height_pos, 3=width_pos,
         *                  4=valid_length, 5=current_length, 6=linear_attn_mask,
         *                  7..=cache（kv/conv/recurrent）
         *  - decode 输入:  0=input, 1=time_pos, 2=height_pos, 3=width_pos,
         *                  4=valid_length, 5=current_length, 6=linear_attn_mask,
         *                  7..=cache
         */
        class HmQwenInfer {
        public:
            /**
             * @param prefillModelPath     prefill 模型路径
             * @param decodeModelPath      decode 模型路径
             * @param tokenizerJsonPath    tokenizer.json 路径
             * @param embeddingWeightPath  embedding 权重（fp16 .bin）路径
             * @param deviceId             HM 逻辑设备 id（默认 0）
             */
            HmQwenInfer(const ModelPathConfig& config);

            HmQwenInfer(const HmQwenInfer&) = delete;
            HmQwenInfer& operator=(const HmQwenInfer&) = delete;
            HmQwenInfer(HmQwenInfer&&) noexcept = delete;
            HmQwenInfer& operator=(HmQwenInfer&&) noexcept = delete;

            ~HmQwenInfer();

            /**
             * @brief 获取 TPU 信息接口（M50 / Houmo）
             * @param[in] deviceId Houmo 逻辑设备 id（默认 0）
             * @return TPU 信息（内存单位：字节，频率单位：MHz，温度单位：摄氏度）；
             *         查询失败时返回全 0
             */
            static TpuInfo GetTpuUsage(int deviceId = 0) noexcept {
                TpuInfo info;
                try {
                    // 必须显式指定 Xh2HalBackend（M50），否则默认 backend 不暴露内存遥测
                    auto dev = tcim::dev_ctrl::HalDeviceFactory::Create("Xh2HalBackend");
                    if (!dev) {
                        return TpuInfo {};
                    }
                    tcim::dev_ctrl::MemInfo memInfo {};
                    if (dev->GetMemInfo(deviceId, &memInfo) != tcim::Status::OK) {
                        return TpuInfo {};
                    }
                    // Houmo MemInfo 单位为 MB，统一转换为字节
                    info.mMemUsage.mTotal = static_cast<uint64_t>(memInfo.mem_total) * 1024ULL * 1024ULL;
                    info.mMemUsage.mUsed = static_cast<uint64_t>(memInfo.mem_used) * 1024ULL * 1024ULL;
                    info.mMemUsage.mFree = static_cast<uint64_t>(memInfo.mem_avail) * 1024ULL * 1024ULL;
                    // Houmo 频率单位为 Hz，转换为 MHz
                    uint64_t freqHz = 0;
                    if (dev->GetIpuFrequency(deviceId, &freqHz) == tcim::Status::OK) {
                        info.mFreqMHz = static_cast<uint32_t>(freqHz / 1000);
                    }
                    // 温度：Houmo dev_ctrl 未提供温度遥测接口，保持默认 0
                } catch (...) {
                    return TpuInfo {};
                }
                return info;
            }

            /**
             * @brief 校验模型文件的 MD5 校验码（独立静态方法，可并行于模型加载执行）。
             *
             * 对 prefill_model / decode_model / embedding_bin 三个文件做 MD5 校验，
             * 期望校验码存放于与模型文件同目录、同名、以 .sha256 结尾的文件中
             * （该文件虽然以 .sha256 结尾，内容实际保存的是 MD5 校验码）。
             * 校验结果写入静态成员 sVerifyStatus，不返回值。
             *
             * @param[in] config 模型路径配置
             */
            static void VerifyModelFiles(const ModelPathConfig& config);

            /** 模型文件校验结果（由 VerifyModelFiles 后台写入，加载完成后读取）。 */
            static ModelVerifyStatus sVerifyStatus;

            /** 获取模型文件校验结果（跨线程安全，返回只读状态）。 */
            const ModelVerifyStatus& GetVerifyStatus() const;

            /**
             * @brief 获取 prefill(0) / decode(1) 子模型。
             */
            std::shared_ptr<tcim::Module> GetModule(int modelType);

            /**
             * @brief 获取 tokenizer。
             */
            std::shared_ptr<HmTokenizer> GetTokenizer();

            /**
             * @brief 卸载模型资源（释放 prefill/decode 模型与 tokenizer）。
             */
            void Unload();

            lms::SummaryResult Summarize(const MettingInfo& meetingInfo, const MettingHints& hints = {},
                                         const ProgressObserver& progressObserver = {});

            /**
             * @brief 请求取消当前正在执行的任务（线程安全、非阻塞）。
             *
             * 可在任意线程调用。若当前有任务在运行，将其标记为取消，运行中的
             * Summarize()/GenerateReply() 会在下一个检查点抛出 OperationCancelled；
             * 若当前无任务运行，该调用被忽略（不会误取消后续任务）。
             */
            void Cancel();

            /**
             * @brief 获取当前任务状态（线程安全）。
             */
            TaskState GetTaskState() const;

            /**
             * @brief 重置对话上下文（复位上下文长度并清空循环状态），线程安全。
             */
            void ResetContext();

        private:
            // ---- 初始化阶段 ----
            void WireCaches();                       // 将 prefill 与 decode 的 cache 张量关联
            void ClearCache();                       // 清空 linear-attention 的循环状态
            void SetDecodeCurrentLength();           // 设置 decode 的 current_length = 1
            int OutputArgMax(tcim::Module* module);  // 取 logits 输出 argmax（贪心，用于首 token）
            // 取 logits，对已生成 token 施加 repetition_penalty / presence_penalty 后取 argmax。
            int OutputArgMaxWithPenalties(tcim::Module* module, const std::vector<int32_t>& generatedIds,
                                          float repetitionPenalty, float presencePenalty);

            // ---- 运行阶段 ----
            // 一次完整的 prefill + decode。silent=true 时不打印流式输出；
            // callback 非空时以增量文本片段回调外层。
            std::string GenerateInternal(const std::string& msg, const std::string_view& systemPrompt, bool silent,
                                         PerfInfos& perf, const HmStreamCallback& callback);

            // 检查取消请求，若已请求取消则抛出 OperationCancelled。
            void CheckCancellation() const;

            // 取消后的上下文清理（复位 context_length 并清空循环状态）。
            void ResetContextAfterCancel();

            void PrefillSetInputDatas(void* data, int32_t validLength, int32_t currentLength);
            void PrefillInfer();
            void PrefillGetOutputDatas(std::vector<int32_t>& ids);

            void DecodeSetInputDatas(void* data, int32_t contextLength);
            void DecodeInfer();
            void DecodeGetOutputDatas(std::vector<int32_t>& ids, const std::vector<int32_t>& generatedIds);

            // 根据模型输入信息创建 host 张量（复用外部缓冲）。
            tcim::Tensor MakeHostInput(tcim::Module* module, const std::string& name, void* ptr);

            // ---- 模型路径 ----
            std::string mPrefillModelPath;
            std::string mDecodeModelPath;

            // ---- 模型维度 ----
            int mPrefillLength = 0;     // 单次 prefill chunk 长度
            int mEmbeddingLength = 0;   // embedding 维度
            int mContextMaxLength = 0;  // 最大上下文长度
            int mBatch = 0;             // batch 大小
            int mEosTokenId = 0;        // 结束 token id
            int mArgmaxDimLen = 0;      // logits 最后一维长度（词表大小）

            // ---- 标量输入 ----
            int32_t mDecodeCurrentLength = 1;  // decode current_length（恒为 1）
            float mRepetitionPenalty = 1.1f;   // 重复惩罚系数
            float mPresencePenalty = 1.5f;     // 出现惩罚系数
            int32_t mPrefillValidLength = 0;
            int32_t mPrefillCurrentLength = 0;
            int32_t mDecodeValidLength = 0;

            int mContextLength = 0;  // 当前上下文长度

            // ---- 运行时对象 ----
            std::shared_ptr<HmTokenizer> mTokenizer;
            tcim::Module::WeightManager mWeightManager;
            std::shared_ptr<tcim::Module> mPrefillModule;
            std::shared_ptr<tcim::Module> mDecodeModule;

            // ---- 每步复用的 host 缓冲 ----
            std::vector<int32_t> mPrefillPosBuffer;
            std::vector<int32_t> mDecodePosBuffer;
            std::vector<tensor_type> mPrefillAttnMaskBuffer;
            std::vector<tensor_type> mDecodeAttnMaskBuffer;

            // ---- 任务状态与取消控制（线程安全） ----
            std::atomic<bool> mCancelRequested {false};           // 取消请求标志
            std::atomic<TaskState> mTaskState {TaskState::Idle};  // 当前任务状态
            std::mutex mInferMutex;                               // 串行化任务执行

            // ---- 后台模型校验线程 ----
            std::thread mVerifyThread;  // 构造函数启动的 MD5 校验线程，析构时 join 回收
        };

    }  // namespace lmshm

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_LMS_HM_HM_QWEN_INFER_H