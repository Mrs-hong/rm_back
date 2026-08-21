/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_AAS_AAS_API_H
#define QIFENG_FRAMEWORK_AAS_AAS_API_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "aas/aas_callback.h"
#include "aas/aas_tasks.h"
#include "dag/dag.h"
#include "dag/node.h"
#include "dag/workflow_dag.h"

// 前向声明
namespace qifeng {
    class WorkflowDAGExecutor;
}

namespace qifeng {
    namespace aas {

        /**
         * @brief AAS 异步句柄
         */
        struct AasAsyncHandle {
            std::string taskId;   // 内部任务ID
            std::string traceId;  // 外部追踪ID
        };

        /**
         * @brief AAS 任务状态
         */
        enum class AasJobState {
            PENDING,    // 等待中
            RUNNING,    // 运行中
            COMPLETED,  // 已完成
            CANCELLED,  // 已取消
            FAILED      // 执行失败
        };

        // 用于在异步回调中传递数据的结构
        struct AsyncTaskData {
            std::string taskId;
            std::vector<std::string> hotwords;
            std::shared_ptr<AasResult> sharedResult;
            std::shared_ptr<CommonTaskData> sharedData;
            AasResult* userResult = nullptr;  // 指向用户传入的 result（注意生命周期）
        };

        // ==================== AAS管理类 ====================

        /**
         * @brief AAS 任务管理器（内部使用）
         */
        class AasJobManager {
        public:
            static AasJobManager& GetInstance();

            /**
             * @brief 初始化管理器
             */
            bool Initialize();

            /**
             * @brief 启动管理器
             * @param numThreads 线程数（默认1）
             */
            void Start(std::shared_ptr<GetAudioBase> getAudioBase, AasArchType archType, int numThreads = 1);

            /**
             * @brief 停止管理器：不再接受新数据，等待所有在途结果（含 HYBRID 精确线
             *        异步任务）处理完成后停止
             */
            void Stop();

            /**
             * @brief 重置转写状态：先停止，再清除音频缓存、会议上下文、qwen 累积
             *        缓冲与模型上下文（不卸载模型）
             */
            void Reset();

            /**
             * @brief 1) 同步调用 AAS
             * 输入音频与选项，返回完整识别结果
             * @param taskData 包含音频样本和热词的任务数据结构体引用
             * @return AAS 执行结果
             */
            AasResult RunAasSynchronous(std::shared_ptr<CommonTaskData> sharedData);

            /**
             * @brief 2) 异步调用 AAS
             * 输入音频与选项，返回异步句柄，并通过 AasResult 存储执行结果
             * @param result 用于写回异步执行结果的 AasResult 对象引用
             * @param audioSamples 一维浮点数组，PCM 归一化到 [-1, 1]
             * @param hotwords 热词列表（默认空；若非空则触发 ASR 热词更新）
             * @return 异步句柄
             */
            AasAsyncHandle StartAasAsyncJob(AasResult& result, std::shared_ptr<CommonTaskData> sharedData);

            /**
             * @brief 3) 同步调用 SV 声纹注册
             * 输入音频与选项，返回feature，并通过 AasResult 存储执行结果
             * @param audioSamples 一维浮点数组，PCM 归一化到 [-1, 1]
             * @return 声纹嵌入向量
             */
            int StartVoiceprintRegister(qifeng::aas::ResultInfo& info);

            /**
             * @brief 4) 查询任务结果
             * 查询异步任务状态或结果
             * @param traceId 追踪ID
             * @param taskId 任务ID（默认空，查询所有）
             * @return AAS 执行结果（未完成时 code=ERR_BUSY 或 message=RUNNING）
             */
            AasResult QueryAasJobResult(std::string traceId, std::string taskId);

            /**
             * @brief 5) 取消任务
             * 停止或取消异步任务
             * @param traceId 追踪ID
             * @param taskId 任务ID（默认空，查询所有）
             * @return 是否成功取消
             */
            bool CancelAasJob(std::string traceId, std::string taskId);

            bool ExecuteAasDAG(AasResult& result, std::shared_ptr<CommonTaskData> sharedData,
                               const std::string& taskId);

            /**
             * @brief 创建新任务
             * @param traceId 追踪ID
             * @return 任务ID
             */
            std::string CreateJob(const std::string& traceId);

            /**
             * @brief 获取任务结果
             */
            std::shared_ptr<AasResult> GetJobResult(const std::string& taskId);

            /**
             * @brief 更新任务状态
             */
            void UpdateJobState(const std::string& taskId, AasJobState state);

            /**
             * @brief 获取任务状态
             */
            AasJobState GetJobState(const std::string& taskId);

            /**
             * @brief 取消任务
             */
            bool CancelJob(const std::string& taskId);

            /**
             * @brief 注册任务对应的 WorkflowDAGExecutor
             */
            void RegisterTaskExecutor(const std::string& taskId, std::shared_ptr<WorkflowDAGExecutor> executor);

            /**
             * @brief 清理已完成的任务
             */
            void CleanupCompletedJobs();

            static void GetAudioSample(std::vector<uint8_t>& data, qifeng::aas::FormatConfig& src);

        private:
            // 辅助函数
            struct PrepareTaskDataParams {
                std::shared_ptr<ResultInfo> info;
                std::vector<float>& audioSamples;
                qifeng::aas::FormatConfig dst;
                std::shared_ptr<AasMeeting> meeting;
                AasArchType archType;
            };
            void PrepareTaskData(std::shared_ptr<CommonTaskData>& sharedData, const PrepareTaskDataParams& params);
            void AasThreadFunc(std::shared_ptr<GetAudioBase> getAudioBase, std::shared_ptr<AasMeeting> meeting,
                               AasArchType archType);
            void OfflineLastRun(std::shared_ptr<AasMeeting> meeting, std::shared_ptr<ResultInfo> info,
                                AasArchType archType);
            // ---- HYBRID 混合路线（双线并行）----
            // 实时线：当前音频块走 paraformer（低延迟），结果标记 REALTIME；
            // 精确线：音频累积到 buffer2，满 hybrid_qwen_chunk_ms 后异步触发一次 qwen 转写
            void HybridProcess(std::shared_ptr<AasMeeting> meeting, std::shared_ptr<ResultInfo> info,
                               std::vector<float>& audioSamples, const qifeng::aas::FormatConfig& dst);
            // 精确线单次 qwen 转写任务（BatchExecutor 线程中执行），结果标记 PRECISE
            // meeting 用于跨 chunk overlap 文本去重（mLastPreciseText）
            void HybridQwenTask(std::shared_ptr<AasMeeting> meeting, std::vector<float>& audioSamples,
                                std::shared_ptr<ResultInfo> info, const qifeng::aas::FormatConfig& dst,
                                int64_t durationMs);
            // 停止前冲刷精确线剩余累积缓冲（不足 1S 直接丢弃），同步执行
            void FlushHybridQwenBuffer(std::shared_ptr<AasMeeting> meeting);
            struct AasDagBuildParams {
                DAG& dag;
                std::string taskId;
            };
            bool BuildAasParaformerDAG(const AasDagBuildParams& params);
            bool BuildAasQwenDAG(const AasDagBuildParams& params);
            bool BuildSVDAG(const AasDagBuildParams& params);
            bool BuildAasDAG(DAG& dag, const std::string& taskId, const std::shared_ptr<CommonTaskData>& sharedData,
                             AasResult& result);
            AasResult ExecuteSvDag(AasResult& result, const std::string& taskId,
                                   const std::shared_ptr<CommonTaskData>& sharedData);
            void ExtractResult(AasResult& result, const std::shared_ptr<CommonTaskData>& sharedData);
            void ExtractResultForSV(AasResult& result, const std::shared_ptr<CommonTaskData>& sharedData);
            // 声纹注册路径：直接调用 SV 模型层做滑窗+均值特征提取，不走 DAG
            void ExtractVoiceprint(AasResult& result, const std::vector<float>& audioSamples);
            void ComputeEmbeddingRow(std::vector<float>& outRow, size_t rowIdx, size_t cols,
                                     const std::vector<SVSpeechAndResultData>& svSpeechData);
            float SumEmbeddingElement(const std::vector<SVSpeechAndResultData>& svSpeechData, size_t rowIdx,
                                      size_t colIdx);
            static std::string ComputeMd5(const std::vector<std::vector<float>>& data);
            void UpdateAasResult(AasResult* result, int code, const std::string& message);
            void CopyAasResult(AasResult* target, const AasResult& source);
            void HandleAsyncDagCancelled(const std::shared_ptr<AsyncTaskData>& taskData);
            void HandleAsyncDagFailed(const std::shared_ptr<AsyncTaskData>& taskData);
            void HandleAsyncDagSuccess(const std::shared_ptr<AsyncTaskData>& taskData);
            void HandleAsyncDagCallback(bool success, const std::shared_ptr<AsyncTaskData>& taskData);
            bool ModifyTime(qifeng::aas::AasResult& aasResult, std::shared_ptr<ResultInfo> info);

        private:
            AasJobManager() = default;
            ~AasJobManager() = default;
            AasJobManager(const AasJobManager&) = delete;
            AasJobManager& operator=(const AasJobManager&) = delete;
            AasJobManager(AasJobManager&&) = delete;
            AasJobManager& operator=(AasJobManager&&) = delete;

            std::string GenerateTaskId();

            mutable std::mutex mJobsMutex;
            std::unordered_map<std::string, std::shared_ptr<AasResult>> mJobResults;
            std::unordered_map<std::string, AasJobState> mJobStates;
            std::unordered_map<std::string, std::shared_ptr<WorkflowDAGExecutor>> mJobExecutors;
            std::atomic<uint64_t> mTaskIdCounter {0};
            std::atomic<bool> mIsStop {false};
            // 当前活动会议（Start 时创建，Stop/Reset 时用于冲刷缓冲、等待在途任务）
            std::shared_ptr<AasMeeting> mActiveMeeting;
            std::mutex mMeetingMutex;
        };

    }  // namespace aas
}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_AAS_AAS_API_H
