/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <openssl/md5.h>
#include <random>
#include <sstream>
#include <thread>

#include "aas/aas_api.h"
#include "aas/aas_callback.h"
#include "aas/aas_tasks.h"
#include "aas/audio_load.h"
#include "common/config_manager.h"
#include "common/logger.h"
#include "common/utils/batch_executor.h"
#include "dag/node.h"
#include "models/manager/models_manager.h"

namespace qifeng {
    namespace aas {

        void AasParaformerJobStart(std::shared_ptr<GetAudioBase> getAudioBase, AasArchType archType, int numThreads) {
            AasJobManager::GetInstance().Start(getAudioBase, archType, numThreads);
        }

        void AasQwenJobStart(std::shared_ptr<GetAudioBase> getAudioBase, AasArchType archType, int numThreads) {
            AasJobManager::GetInstance().Start(getAudioBase, archType, numThreads);
        }

        void AasJobStop() {
            AasJobManager::GetInstance().Stop();
        }

        int StartVoiceprintRegister(qifeng::aas::ResultInfo& info) {
            return AasJobManager::GetInstance().StartVoiceprintRegister(info);
        }

        int InitializeAasResources() {
            try {
                SLOG_INFO << "Initializing AAS resources...";
                auto startTime = std::chrono::high_resolution_clock::now();
                // 这里可以添加其他资源的初始化逻辑
                qifeng::ModelsManager::GetInstance().Init();
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                SLOG_INFO << "AAS resources initialized in " << duration.count() << "ms";
                return 0;  // 成功
            } catch (const std::exception& ex) {
                SLOG_ERROR << "Failed to initialize AAS resources: " << ex.what();
                return -1;  // 失败
            }
        }
        void ReleaseAasResources() {
            try {
                SLOG_INFO << "Releasing AAS resources...";
                auto startTime = std::chrono::high_resolution_clock::now();
                // 这里可以添加其他资源的释放逻辑
                qifeng::ModelsManager::GetInstance().Stop();
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                SLOG_INFO << "AAS resources released in " << duration.count() << "ms";
            } catch (const std::exception& ex) {
                SLOG_ERROR << "Failed to release AAS resources: " << ex.what();
            }
        }

        qifeng::Status GetModelVerifyStatus() {
            return qifeng::ModelsManager::GetInstance().GetModelVerifyStatus();
        }

        // ==================== 常量定义 ====================
        enum class AasError {
            AAS_ERR_BUSY = 0,       // 忙
            AAS_ERR_CANCELLED = 1,  // 已取消
            AAS_ERR_INTERNAL = 2,   // 内部错误
        };

        // ==================== AasJobManager 实现 ====================

        AasJobManager& AasJobManager::GetInstance() {
            static AasJobManager Instance;
            return Instance;
        }

        std::vector<float> AasMeeting::MergeAudioCacheToVector() {
            // 直接访问 RingBuffer 内部数组，避免 GetAll() 的 AudioBlockVec 深拷贝
            // AudioBlockVec 包含 std::vector<float> data，GetAll() 会复制 5 个 vector 的数据
            const auto& buf = mAudioCache.GetBuffer();
            auto cap = mAudioCache.capacity();
            auto count = mAudioCache.GetCount();
            auto head = mAudioCache.GetHead();

            size_t totalSize = 0;
            for (size_t i = 0; i < count; ++i) {
                totalSize += buf[(head + i) % cap].size();
            }

            std::vector<float> result;
            result.reserve(totalSize);
            for (size_t i = 0; i < count; ++i) {
                const auto& block = buf[(head + i) % cap];
                result.insert(result.end(), block.begin(), block.end());
            }
            return result;
        }

        bool AasMeeting::UpdateAudioCache(std::vector<float>& audioSamples, bool hasVoice, TimeInfo timeInfo) {
            // 必须用引用，否则 auto 推导为值类型（拷贝），Push 写入副本，原始 mAudioCache 为空

            if (hasVoice) {  // 表明传入的是有人声的数据
                AudioBlockVec block;
                block.data = std::move(audioSamples);
                block.hasVoice = hasVoice;
                block.remainingDurationMs = timeInfo.remainingDurationMs;
                block.startTimeMs = timeInfo.startTimeMs;
                block.endTimeMs = timeInfo.endTimeMs;
                mAudioCache.Push(std::move(block));
            } else {  // 表明传入的是无人声的数据
                // 清空缓冲区
                mAudioCache.clear();
            }
            return true;
        }

        bool AasJobManager::Initialize() {
            SLOG_INFO << "Initializing AasJobManager";
            WorkflowDAGExecutor::Init(4);
            return true;
        }

        std::string AasJobManager::GenerateTaskId() {
            uint64_t id = ++mTaskIdCounter;
            return "aas_job_" + std::to_string(id);
        }

        std::string AasJobManager::CreateJob(const std::string& traceId) {
            std::lock_guard<std::mutex> lock(mJobsMutex);
            std::string taskId = GenerateTaskId();

            auto result = std::make_shared<AasResult>();
            result->traceId = traceId;
            result->submitTime = std::chrono::steady_clock::now();
            mJobResults[taskId] = result;
            mJobStates[taskId] = AasJobState::PENDING;

            SLOG_DEBUG << "Created AAS job: taskId=" << taskId << ", traceId=" << traceId;
            return taskId;
        }

        std::shared_ptr<AasResult> AasJobManager::GetJobResult(const std::string& taskId) {
            std::lock_guard<std::mutex> lock(mJobsMutex);
            auto it = mJobResults.find(taskId);
            if (it == mJobResults.end()) {
                return nullptr;
            }
            return it->second;
        }

        void AasJobManager::UpdateJobState(const std::string& taskId, AasJobState state) {
            std::lock_guard<std::mutex> lock(mJobsMutex);
            auto it = mJobStates.find(taskId);
            if (it != mJobStates.end()) {
                it->second = state;
            }
        }

        AasJobState AasJobManager::GetJobState(const std::string& taskId) {
            std::lock_guard<std::mutex> lock(mJobsMutex);
            auto it = mJobStates.find(taskId);
            if (it == mJobStates.end()) {
                return AasJobState::PENDING;
            }
            return it->second;
        }

        bool AasJobManager::CancelJob(const std::string& taskId) {
            std::lock_guard<std::mutex> lock(mJobsMutex);
            auto it = mJobStates.find(taskId);
            if (it == mJobStates.end()) {
                SLOG_WARN << "Job not found for cancellation: " << taskId;
                return false;
            }

            if (it->second == AasJobState::COMPLETED || it->second == AasJobState::CANCELLED) {
                SLOG_WARN << "Job already completed or cancelled: " << taskId;
                return false;
            }

            it->second = AasJobState::CANCELLED;

            // 停止底层的 DAG 执行
            auto executorIt = mJobExecutors.find(taskId);
            if (executorIt != mJobExecutors.end() && executorIt->second) {
                executorIt->second->Stop();
            }

            auto resultIt = mJobResults.find(taskId);
            if (resultIt != mJobResults.end()) {
                resultIt->second->code = static_cast<int>(AasError::AAS_ERR_CANCELLED);
                resultIt->second->message = "CANCELLED";
                resultIt->second->ready = true;
            }

            SLOG_DEBUG << "Cancelled AAS job: " << taskId;
            return true;
        }

        void AasJobManager::RegisterTaskExecutor(const std::string& taskId,
                                                 std::shared_ptr<WorkflowDAGExecutor> executor) {
            std::lock_guard<std::mutex> lock(mJobsMutex);
            mJobExecutors[taskId] = executor;
        }

        void AasJobManager::CleanupCompletedJobs() {
            // 清理已完成的任务（避免内存泄漏）
            std::lock_guard<std::mutex> lock(mJobsMutex);
            // 这里可以实现定时清理逻辑
        }

        // ==================== 辅助函数定义 ====================

        // AAS DAG 构建参数
        struct AasDagBuildParams {
            DAG& dag;
            std::string taskId;
        };

        // 构建 AAS Paraformer DAG
        bool AasJobManager::BuildAasParaformerDAG(const AasDagBuildParams& params) {
            // 创建 AAS 任务节点
            auto vadNode = std::make_shared<VADTask>("VAD_" + params.taskId);
            auto asrPuncNode = std::make_shared<ASRPuncTask>("ASR_PUNC_" + params.taskId);
            auto svNode = std::make_shared<ExtractSpeakerEmbeddingTask>("SV_" + params.taskId);
            auto aggNode = std::make_shared<AggregationTask>("AGG_" + params.taskId);

            // 添加节点到 DAG
            params.dag.AddNode(vadNode);
            params.dag.AddNode(asrPuncNode);
            params.dag.AddNode(svNode);
            params.dag.AddNode(aggNode);

            // 建立依赖关系
            params.dag.AddDependency("ASR_PUNC_" + params.taskId, "VAD_" + params.taskId);
            params.dag.AddDependency("SV_" + params.taskId, "VAD_" + params.taskId);
            params.dag.AddDependency("AGG_" + params.taskId, "ASR_PUNC_" + params.taskId);
            params.dag.AddDependency("AGG_" + params.taskId, "SV_" + params.taskId);

            // 验证 DAG
            if (!params.dag.Validate()) {
                SLOG_ERROR << "DAG validation failed for task: " << params.taskId;
                return false;
            }
            return true;
        }

        // 构建 AAS Qwen DAG
        bool AasJobManager::BuildAasQwenDAG(const AasDagBuildParams& params) {
            // 创建 AAS 任务节点
            auto vadNode = std::make_shared<VADTask>("VAD_" + params.taskId);
            auto asrNode = std::make_shared<QwenASRTask>("QwenASR_" + params.taskId);
            auto svNode = std::make_shared<ExtractSpeakerEmbeddingTask>("SV_" + params.taskId);
            auto aggNode = std::make_shared<AggregationTask>("AGG_" + params.taskId);

            // 添加节点到 DAG
            params.dag.AddNode(vadNode);
            params.dag.AddNode(asrNode);
            params.dag.AddNode(svNode);
            params.dag.AddNode(aggNode);

            // 建立依赖关系
            params.dag.AddDependency("QwenASR_" + params.taskId, "VAD_" + params.taskId);
            params.dag.AddDependency("SV_" + params.taskId, "VAD_" + params.taskId);
            params.dag.AddDependency("AGG_" + params.taskId, "QwenASR_" + params.taskId);
            params.dag.AddDependency("AGG_" + params.taskId, "SV_" + params.taskId);

            // 验证 DAG
            if (!params.dag.Validate()) {
                SLOG_ERROR << "DAG validation failed for task: " << params.taskId;
                return false;
            }
            return true;
        }

        // 构建 SV DAG
        bool AasJobManager::BuildSVDAG(const AasDagBuildParams& params) {
            // 创建 SV 任务节点
            auto vadNode = std::make_shared<VADTask>("VAD_" + params.taskId);
            auto svNode = std::make_shared<ExtractSpeakerEmbeddingTask>("SV_" + params.taskId);

            // 添加节点到 DAG
            params.dag.AddNode(vadNode);
            params.dag.AddNode(svNode);

            // 建立依赖关系
            params.dag.AddDependency("SV_" + params.taskId, "VAD_" + params.taskId);

            // 验证 DAG
            if (!params.dag.Validate()) {
                SLOG_ERROR << "DAG validation failed for task: " << params.taskId;
                return false;
            }
            return true;
        }

        // 从 sharedData 提取结果到 AasResult
        void AasJobManager::ExtractResult(AasResult& result, const std::shared_ptr<CommonTaskData>& sharedData) {
            if (sharedData) {
                // 直接从sharedData提取结果
                auto* storedResult = sharedData->Get<AasResult>("result");
                if (storedResult) {
                    result = std::move(*storedResult);
                }
            }
        }
        void AasJobManager::ExtractResultForSV(AasResult& result, const std::shared_ptr<CommonTaskData>& sharedData) {
            if (sharedData) {
                // 直接从sharedData提取结果，整个流程只是走了VAD和SV
                std::vector<qifeng::aas::SVSpeechAndResultData>* svSpeechData =
                    sharedData->Get<std::vector<qifeng::aas::SVSpeechAndResultData>>("svSpeechData");
                // 把svSpeechData中的已有的结果封装到result中去
                if (!svSpeechData || svSpeechData->empty()) {
                    return;
                }
                const auto& firstEmbedding = svSpeechData->front().svEmbedding;
                if (firstEmbedding.empty() || firstEmbedding[0].empty()) {
                    return;
                }
                size_t rows = firstEmbedding.size();
                size_t cols = firstEmbedding[0].size();
                result.svEmbedding.assign(rows, std::vector<float>(cols, 0.0F));
                for (size_t i = 0; i < rows; ++i) {
                    ComputeEmbeddingRow(result.svEmbedding[i], i, cols, *svSpeechData);
                }
                // 通过svEmbedding生成对应的MD5码
                std::string md5 = ComputeMd5(result.svEmbedding);
                result.svEmbeddingMd5 = md5;
            }
        }

        void AasJobManager::ComputeEmbeddingRow(std::vector<float>& outRow, size_t rowIdx, size_t cols,
                                                const std::vector<qifeng::aas::SVSpeechAndResultData>& svSpeechData) {
            float count = static_cast<float>(svSpeechData.size());
            for (size_t j = 0; j < cols; ++j) {
                outRow[j] = SumEmbeddingElement(svSpeechData, rowIdx, j) / count;
            }
        }

        float AasJobManager::SumEmbeddingElement(const std::vector<qifeng::aas::SVSpeechAndResultData>& svSpeechData,
                                                 size_t rowIdx, size_t colIdx) {
            float sum = 0.0F;
            for (const auto& seg : svSpeechData) {
                if (rowIdx < seg.svEmbedding.size() && colIdx < seg.svEmbedding[rowIdx].size()) {
                    sum += seg.svEmbedding[rowIdx][colIdx];
                }
            }
            return sum;
        }

        std::string AasJobManager::ComputeMd5(const std::vector<std::vector<float>>& data) {
            MD5_CTX ctx;
            MD5_Init(&ctx);
            for (const auto& row : data) {
                MD5_Update(&ctx, row.data(), row.size() * sizeof(float));
            }
            std::array<unsigned char, MD5_DIGEST_LENGTH> digest {};
            MD5_Final(digest.data(), &ctx);
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (unsigned char byte : digest) {
                oss << std::setw(2) << static_cast<int>(byte);
            }
            return oss.str();
        }

        // 更新 AasResult 的值
        void AasJobManager::UpdateAasResult(AasResult* result, int code, const std::string& message) {
            if (result) {
                result->code = code;
                result->message = message;
                result->ready = true;
            }
        }
        // 复制 AasResult 的内容
        void AasJobManager::CopyAasResult(AasResult* target, const AasResult& source) {
            if (target) {
                target->code = source.code;
                target->message = source.message;
                target->text = source.text;
                target->speakerLabel = source.speakerLabel;
                target->svEmbedding = source.svEmbedding;
                target->segments = source.segments;
                target->ready = true;
            }
        }
        // 处理异步 DAG 回调 - 取消场景
        void AasJobManager::HandleAsyncDagCancelled(const std::shared_ptr<qifeng::aas::AsyncTaskData>& taskData) {
            SLOG_DEBUG << "AAS processing cancelled for task: " << taskData->taskId;
            UpdateAasResult(taskData->sharedResult.get(), static_cast<int>(AasError::AAS_ERR_CANCELLED), "CANCELLED");
            UpdateAasResult(taskData->userResult, static_cast<int>(AasError::AAS_ERR_CANCELLED), "CANCELLED");
        }

        // 处理异步 DAG 回调 - 失败场景
        void AasJobManager::HandleAsyncDagFailed(const std::shared_ptr<qifeng::aas::AsyncTaskData>& taskData) {
            SLOG_ERROR << "DAG execution failed for task: " << taskData->taskId;
            UpdateAasResult(taskData->sharedResult.get(), static_cast<int>(AasError::AAS_ERR_INTERNAL),
                            "DAG_EXECUTION_FAILED");
            UpdateAasResult(taskData->userResult, static_cast<int>(AasError::AAS_ERR_INTERNAL), "DAG_EXECUTION_FAILED");
            AasJobManager::GetInstance().UpdateJobState(taskData->taskId, AasJobState::FAILED);
        }

        // 处理异步 DAG 回调 - 成功场景
        void AasJobManager::HandleAsyncDagSuccess(const std::shared_ptr<qifeng::aas::AsyncTaskData>& taskData) {
            AasResult tempResult;
            ExtractResult(tempResult, taskData->sharedData);
            CopyAasResult(taskData->sharedResult.get(), tempResult);
            CopyAasResult(taskData->userResult, tempResult);
            AasJobManager::GetInstance().UpdateJobState(taskData->taskId, AasJobState::COMPLETED);
            SLOG_DEBUG << "AAS DAG processing completed for task: " << taskData->taskId;
        }

        // 处理异步 DAG 回调
        void AasJobManager::HandleAsyncDagCallback(bool success,
                                                   const std::shared_ptr<qifeng::aas::AsyncTaskData>& taskData) {
            SLOG_DEBUG << "AAS DAG async callback for task: " << taskData->taskId
                       << ", success=" << (success ? "yes" : "no");
            auto& jobManager = AasJobManager::GetInstance();
            if (jobManager.GetJobState(taskData->taskId) == AasJobState::CANCELLED) {
                HandleAsyncDagCancelled(taskData);
                return;
            }
            if (!success) {
                HandleAsyncDagFailed(taskData);
                return;
            }
            HandleAsyncDagSuccess(taskData);
        }

        // ==================== 同步执行 AAS ====================

        // 根据 archType 构建对应的 DAG
        bool AasJobManager::BuildAasDAG(DAG& dag, const std::string& taskId,
                                        const std::shared_ptr<CommonTaskData>& sharedData, AasResult& result) {
            // 从sharedData中提取AasArchType
            AasArchType* archType = sharedData->Get<AasArchType>("archType");
            if (!archType) {
                result.code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                result.message = "MISSING_ARCH_TYPE";
                return false;
            }
            // 根据archType选择DAG构建函数
            AasDagBuildParams params {.dag = dag, .taskId = taskId};
            if (*archType == AasArchType::PARAFORMER) {
                if (!BuildAasParaformerDAG(params)) {
                    result.code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                    result.message = "DAG_VALIDATION_FAILED";
                    return false;
                }
            } else if (*archType == AasArchType::QWEN) {
                if (!BuildAasQwenDAG(params)) {
                    result.code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                    result.message = "DAG_VALIDATION_FAILED";
                    return false;
                }
            } else {
                result.code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                result.message = "UNSUPPORTED_ARCH_TYPE";
                return false;
            }
            return true;
        }

        bool AasJobManager::ExecuteAasDAG(AasResult& result, std::shared_ptr<CommonTaskData> sharedData,
                                          const std::string& taskId) {
            auto& manager = AasJobManager::GetInstance();
            manager.UpdateJobState(taskId, AasJobState::RUNNING);
            SLOG_DEBUG << "Starting AAS DAG processing for task: " << taskId;
            // 创建 DAG 和共享数据
            DAG dag;

            // 设置数据为可信
            sharedData->SetDataTrusted(true);
            // 根据archType构建DAG
            if (!BuildAasDAG(dag, taskId, sharedData, result)) {
                return false;
            }
            // 检查是否被取消
            if (manager.GetJobState(taskId) == AasJobState::CANCELLED) {
                SLOG_DEBUG << "AAS processing cancelled for task: " << taskId;
                result.code = static_cast<int>(AasError::AAS_ERR_CANCELLED);
                result.message = "CANCELLED";
                return false;
            }
            // 执行 DAG
            WorkflowDAGExecutor executor;
            // 设置线程池大小，通过config获取
            std::string section = "dag";
            std::string key = "compute_threads";
            auto threadPoolSize = ConfigManager::GetInstance().GetIntWithConstraint(section, key);
            executor.SetComputeThreadCount(threadPoolSize);
            bool success = executor.Execute(dag, sharedData);
            // 检查是否被取消
            if (manager.GetJobState(taskId) == AasJobState::CANCELLED) {
                SLOG_DEBUG << "AAS processing cancelled for task: " << taskId;
                result.code = static_cast<int>(AasError::AAS_ERR_CANCELLED);
                result.message = "CANCELLED";
                return false;
            }
            if (!success) {
                SLOG_ERROR << "DAG execution failed for task: " << taskId;
                result.code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                result.message = "DAG_EXECUTION_FAILED";
                return false;
            }
            ExtractResult(result, sharedData);
            manager.UpdateJobState(taskId, AasJobState::COMPLETED);
            result.ready = true;
            SLOG_DEBUG << "AAS DAG processing completed for task: " << taskId;
            return true;
        }

        // ==================== API 实现 ====================

        AasResult AasJobManager::RunAasSynchronous(std::shared_ptr<CommonTaskData> sharedData) {
            AasResult result;
            std::string taskId = "sync_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            result.traceId = taskId;

            // 使用 DAG 图进行同步处理
            if (!ExecuteAasDAG(result, sharedData, taskId)) {
                return result;
            }

            // 清空 sharedData 中的 traceId
            sharedData->Clear();

            return result;
        }

        AasAsyncHandle AasJobManager::StartAasAsyncJob(AasResult& result, std::shared_ptr<CommonTaskData> sharedData) {
            auto& manager = AasJobManager::GetInstance();
            if (result.traceId.empty()) {
                result.traceId = "async_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            }
            sharedData->Set("traceId", result.traceId);
            std::string taskId = manager.CreateJob(result.traceId);
            manager.UpdateJobState(taskId, AasJobState::RUNNING);
            auto sharedResult = manager.GetJobResult(taskId);
            if (sharedResult) {
                sharedResult->traceId = result.traceId;
                sharedResult->submitTime = std::chrono::steady_clock::now();
            }
            SLOG_DEBUG << "Starting AAS DAG processing for task: " << taskId;
            auto dag = std::make_shared<DAG>();
            sharedData->SetDataTrusted(true);  // 设置数据为可信
            AasDagBuildParams params {.dag = *dag, .taskId = taskId};
            if (!BuildAasParaformerDAG(params)) {
                if (sharedResult) {
                    sharedResult->code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                    sharedResult->message = "DAG_VALIDATION_FAILED";
                    sharedResult->ready = true;
                }
                manager.UpdateJobState(taskId, AasJobState::FAILED);
                AasAsyncHandle handle;
                handle.taskId = taskId;
                handle.traceId = result.traceId;
                return handle;
            }
            auto executor = std::make_shared<WorkflowDAGExecutor>();
            auto taskData = std::make_shared<AsyncTaskData>();
            taskData->taskId = taskId;
            taskData->hotwords = *sharedData->Get<std::vector<std::string>>("hotwords");
            taskData->sharedResult = sharedResult;
            taskData->sharedData = sharedData;
            taskData->userResult = &result;  // 保存用户 result 的指针
            manager.RegisterTaskExecutor(taskId, executor);
            std::string section = "dag";
            std::string key = "compute_threads";
            auto threadPoolSize = ConfigManager::GetInstance().GetIntWithConstraint(section, key);
            executor->SetComputeThreadCount(threadPoolSize);
            executor->ExecuteAsync(*dag, sharedData,
                                   [this, taskData](bool success) { this->HandleAsyncDagCallback(success, taskData); });
            result.ready = false;
            AasAsyncHandle handle;
            handle.taskId = taskId;
            handle.traceId = result.traceId;
            SLOG_DEBUG << "Started AAS async job: taskId=" << taskId << ", traceId=" << result.traceId;
            return handle;
        }

        int AasJobManager::StartVoiceprintRegister(qifeng::aas::ResultInfo& info) {
            AasResult result;
            std::vector<uint8_t> pcmData = info.mData;
            if (pcmData.empty()) {
                SLOG_ERROR << "The data of BMS is null";
                return -1;
            }

            qifeng::aas::FormatConfig dst;
            dst.mSampleRate = static_cast<uint32_t>(ConfigManager::GetInstance().GetInt("aas", "sample_rate"));
            dst.mChannels = static_cast<uint16_t>(ConfigManager::GetInstance().GetInt("aas", "channels"));
            dst.mBitDepth = static_cast<uint16_t>(ConfigManager::GetInstance().GetInt("aas", "bit_depth"));

            qifeng::aas::Request request;
            request = qifeng::aas::PcmData {
                .raw = pcmData.data(),
                .size = static_cast<int32_t>(pcmData.size()),
                .sampleRate = info.mConfig.mSampleRate,
                .channels = info.mConfig.mChannels,
                .bitDepth = info.mConfig.mBitDepth,
            };

            std::vector<uint8_t> audioSamplesBytes = AudioLoad::GetPCMData(request, dst);
            if (audioSamplesBytes.empty()) {
                SLOG_ERROR << "ResamplePcm failed: sample is empty";
                return -1;
            }

            std::vector<float> audioSamples;
            size_t floatCountInner = audioSamplesBytes.size() / sizeof(float);
            if (floatCountInner > 0) {
                audioSamples.assign(reinterpret_cast<const float*>(audioSamplesBytes.data()),
                                    reinterpret_cast<const float*>(audioSamplesBytes.data()) + floatCountInner);
            }

            std::string taskId = "sv_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            result.traceId = taskId;

            // 做成回调：声纹注册路径不走 DAG，直接调用 SV 模型层做滑窗+均值特征提取。
            BatchExecutor& batchExecutor = BatchExecutor::GetInstance();
            batchExecutor.AddTask([this, result, audioSamples = std::move(audioSamples), info]() {
                AasResult tempResult = result;
                ExtractVoiceprint(tempResult, audioSamples);
                info.mCallBack(tempResult, info.mBmsInfo);
            });
            return 0;
        }

        void AasJobManager::ExtractVoiceprint(AasResult& result, const std::vector<float>& audioSamples) {
            try {
                if (audioSamples.empty()) {
                    SLOG_ERROR << "ExtractVoiceprint: audioSamples is empty";
                    result.code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                    result.message = "AUDIO_EMPTY";
                    result.ready = true;
                    return;
                }
                // 1. 提取 Fbank 声纹特征
                qifeng::FloatMatrix fbank = qifeng::ModelsManager::GetInstance().VpFeatureProcess(audioSamples);
                if (fbank.empty() || fbank[0].empty()) {
                    SLOG_ERROR << "ExtractVoiceprint: fbank is empty";
                    result.code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                    result.message = "FBANK_EMPTY";
                    result.ready = true;
                    return;
                }
                // 2. 滑动窗口切分：3s 窗口(300帧) + 1s 步移(100帧)，与 SVTask 保持一致
                const int kWindowFrames = 300;
                const int kShiftFrames = 100;
                int totalFrames = static_cast<int>(fbank.size());
                std::vector<qifeng::FloatMatrix> windowFbanks;
                if (totalFrames <= kWindowFrames) {
                    // 不足 3s，整段作为单窗口
                    windowFbanks.push_back(fbank);
                } else {
                    int numWindows = (totalFrames - kWindowFrames) / kShiftFrames + 1;
                    windowFbanks.reserve(numWindows);
                    for (int i = 0; i < numWindows; ++i) {
                        int startFrame = i * kShiftFrames;
                        int endFrame = std::min(startFrame + kWindowFrames, totalFrames);
                        windowFbanks.emplace_back(fbank.begin() + startFrame, fbank.begin() + endFrame);
                    }
                }
                SLOG_DEBUG << "ExtractVoiceprint: totalFrames=" << totalFrames << " windows=" << windowFbanks.size();
                // 3. 批量 SV 推理
                qifeng::FloatMatrix embeddings = qifeng::ModelsManager::GetInstance().SVProcessBatch(windowFbanks);
                if (embeddings.empty() || embeddings[0].empty()) {
                    SLOG_ERROR << "ExtractVoiceprint: embeddings is empty";
                    result.code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                    result.message = "SV_INFER_EMPTY";
                    result.ready = true;
                    return;
                }
                // 4. 对所有窗口 embedding 求均值，得到单个代表向量
                size_t dim = embeddings[0].size();
                std::vector<float> avgEmbedding(dim, 0.0F);
                for (const auto& emb : embeddings) {
                    if (emb.size() == dim) {
                        for (size_t d = 0; d < dim; ++d) {
                            avgEmbedding[d] += emb[d];
                        }
                    }
                }
                float invCount = 1.0F / static_cast<float>(embeddings.size());
                for (size_t d = 0; d < dim; ++d) {
                    avgEmbedding[d] *= invCount;
                }
                result.svEmbedding.assign(1, std::move(avgEmbedding));
                result.svEmbeddingMd5 = ComputeMd5(result.svEmbedding);
                result.code = 0;
                result.ready = true;
                SLOG_DEBUG << "ExtractVoiceprint: success, dim=" << dim << " windows=" << embeddings.size();
            } catch (const std::exception& e) {
                SLOG_ERROR << "ExtractVoiceprint exception: " << e.what();
                result.code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                result.message = "EXTRACT_EXCEPTION";
                result.ready = true;
            }
        }

        AasResult AasJobManager::ExecuteSvDag(AasResult& result, const std::string& taskId,
                                              const std::shared_ptr<CommonTaskData>& sharedData) {
            auto& manager = AasJobManager::GetInstance();
            manager.UpdateJobState(taskId, AasJobState::RUNNING);
            SLOG_DEBUG << "Starting AAS DAG processing for task: " << taskId;

            DAG dag;
            AasDagBuildParams params {.dag = dag, .taskId = taskId};
            if (!BuildSVDAG(params)) {
                result.code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                result.message = "DAG_VALIDATION_FAILED";
                return result;
            }

            if (manager.GetJobState(taskId) == AasJobState::CANCELLED) {
                SLOG_DEBUG << "AAS processing cancelled for task: " << taskId;
                result.code = static_cast<int>(AasError::AAS_ERR_CANCELLED);
                result.message = "CANCELLED";
                return result;
            }

            WorkflowDAGExecutor executor;
            bool success = executor.Execute(dag, sharedData);

            if (manager.GetJobState(taskId) == AasJobState::CANCELLED) {
                SLOG_DEBUG << "AAS processing cancelled for task: " << taskId;
                result.code = static_cast<int>(AasError::AAS_ERR_CANCELLED);
                result.message = "CANCELLED";
                return result;
            }

            if (!success) {
                SLOG_ERROR << "DAG execution failed for task: " << taskId;
                result.code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                result.message = "DAG_EXECUTION_FAILED";
                return result;
            }

            ExtractResultForSV(result, sharedData);
            manager.UpdateJobState(taskId, AasJobState::COMPLETED);
            result.ready = true;
            SLOG_DEBUG << "AAS DAG processing completed for task: " << taskId;
            return result;
        }

        AasResult AasJobManager::QueryAasJobResult(std::string traceId, std::string taskId) {
            SLOG_DEBUG << "Querying AAS job result: taskId=" << taskId << ", traceId=" << traceId;

            auto& manager = AasJobManager::GetInstance();
            auto sharedResult = manager.GetJobResult(taskId);

            AasResult result;
            result.traceId = traceId;

            if (!sharedResult) {
                result.code = static_cast<int>(AasError::AAS_ERR_INTERNAL);
                result.message = "JOB_NOT_FOUND";
                return result;
            }

            // 检查状态
            AasJobState state = manager.GetJobState(taskId);
            if (state == AasJobState::PENDING || state == AasJobState::RUNNING) {
                result.code = static_cast<int>(AasError::AAS_ERR_BUSY);
                result.message = "RUNNING";
                return result;
            }

            // 返回完成的结果
            result.code = sharedResult->code;
            result.message = sharedResult->message;
            result.text = sharedResult->text;
            result.speakerLabel = sharedResult->speakerLabel;
            result.svEmbedding = sharedResult->svEmbedding;
            result.segments = sharedResult->segments;
            result.ready = sharedResult->ready;

            return result;
        }

        bool AasJobManager::CancelAasJob(std::string traceId, std::string taskId) {
            SLOG_DEBUG << "Cancelling AAS job: taskId=" << taskId << ", traceId=" << traceId;
            return AasJobManager::GetInstance().CancelJob(taskId);
        }

        bool AasJobManager::ModifyTime(qifeng::aas::AasResult& aasResult, std::shared_ptr<ResultInfo> info) {
            for (size_t i = 0; i < aasResult.segments.size(); ++i) {
                aasResult.segments[i].startTime += info->mBmsInfo.mStartTime;
                aasResult.segments[i].endTime += info->mBmsInfo.mStartTime;
                if (aasResult.segments[i].endTime > info->mBmsInfo.mEndTime) {
                    SLOG_ERROR << "Segment end time exceeds BMS end time, adjusting: originalEndTime="
                               << aasResult.segments[i].endTime << ", bmsEndTime=" << info->mBmsInfo.mEndTime;
                    aasResult.segments[i].endTime = info->mBmsInfo.mEndTime;
                }
            }
            return true;
        }

        // taskData数据准备
        void AasJobManager::PrepareTaskData(std::shared_ptr<CommonTaskData>& sharedData,
                                            const PrepareTaskDataParams& params) {
            sharedData->Set<std::vector<float>>("audioSamples", params.audioSamples);
            sharedData->Set<std::unordered_map<std::string, std::vector<std::vector<float>>>>(
                "svDatabase", params.info->mSvDataBase->svDatabase);
            sharedData->Set<int>("sampleRate", static_cast<int>(params.dst.mSampleRate));
            sharedData->Set<std::vector<std::string>>("hotwords", params.info->mHotwords->hotwords);
            sharedData->Set<std::string>("sessionId", params.info->mSessionId);
            sharedData->Set<bool>("isOnline", params.info->mBmsInfo.isOnline);
            sharedData->Set<int64_t>("absoluteStartTimeMs", params.info->mBmsInfo.mStartTime);
            sharedData->Set<int64_t>("timeIntervalMs",
                                     params.info->mBmsInfo.mEndTime - params.info->mBmsInfo.mStartTime);
            // 通过taskData传递会议信息
            sharedData->Set<std::shared_ptr<AasMeeting>>("meeting", params.meeting);
            sharedData->Set<int64_t>("pushIntervalMs", params.info->mBmsInfo.mPushIntervalMs);
            sharedData->Set<AasArchType>("archType", params.archType);
        }

        void AasJobManager::OfflineLastRun(std::shared_ptr<AasMeeting> meeting, std::shared_ptr<ResultInfo> info,
                                           AasArchType archType) {
            auto lastInfo = meeting->mLastInfo;
            auto& lastBlock = meeting->GetLastAudioBlock();
            // 清空缓冲区
            lastBlock.remainingDurationMs = 0;
            lastInfo->mBmsInfo.mStartTime = lastBlock.startTimeMs;
            lastInfo->mBmsInfo.mEndTime = lastBlock.endTimeMs;
            qifeng::aas::FormatConfig dst;
            dst.mSampleRate =
                static_cast<uint32_t>(ConfigManager::GetInstance().GetIntWithConstraint("aas", "sample_rate"));
            dst.mChannels = static_cast<uint16_t>(ConfigManager::GetInstance().GetIntWithConstraint("aas", "channels"));
            dst.mBitDepth =
                static_cast<uint16_t>(ConfigManager::GetInstance().GetIntWithConstraint("aas", "bit_depth"));
            auto taskData = std::make_shared<qifeng::CommonTaskData>();
            PrepareTaskData(
                taskData,
                {.info = info, .audioSamples = lastBlock.data, .dst = dst, .meeting = meeting, .archType = archType});
            qifeng::aas::AasResult aasResult = qifeng::aas::AasJobManager::GetInstance().RunAasSynchronous(taskData);
            // 计算md5
            for (auto& seg : aasResult.segments) {
                seg.svEmbeddingMd5 = ComputeMd5(seg.svEmbedding);
            }
            info->mCallBack(aasResult, info->mBmsInfo);
        }

        void AasJobManager::AasThreadFunc(std::shared_ptr<GetAudioBase> getAudioBase,
                                          std::shared_ptr<AasMeeting> meeting, AasArchType archType) {
            while (true) {
                std::shared_ptr<ResultInfo> info = getAudioBase->GetAudio(true);
                if (mIsStop.load() && !info) {
                    // 整场会议从未收到有效音频(如录音不足1s即停止), mLastInfo 为空, 直接退出避免空指针解引用
                    if (!meeting->mLastInfo) {
                        break;
                    }
                    if (meeting->mLastInfo->mBmsInfo.isOnline) {
                        // 实时操作会给底下qwen_asr传递标志位
                        // 最后一次调用lms回调之后break
                    } else {
                        if (!meeting->IsCacheEmpty()) {
                            OfflineLastRun(meeting, meeting->mLastInfo, archType);
                        }
                    }
                    break;
                }
                if (!info) {
                    SLOG_ERROR << "AudioBasePtr is nullptr";
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                meeting->mLastInfo = info;
                std::vector<uint8_t> pcmData = info->mData;
                if (pcmData.empty()) {
                    // 表明当前没有数据，先简单处理线程休息100ms，后面可以通过指数退避算法进行休眠时间的动态调整
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                } else {
                    qifeng::aas::FormatConfig dst;
                    dst.mSampleRate =
                        static_cast<uint32_t>(ConfigManager::GetInstance().GetIntWithConstraint("aas", "sample_rate"));
                    dst.mChannels =
                        static_cast<uint16_t>(ConfigManager::GetInstance().GetIntWithConstraint("aas", "channels"));
                    dst.mBitDepth =
                        static_cast<uint16_t>(ConfigManager::GetInstance().GetIntWithConstraint("aas", "bit_depth"));
                    qifeng::aas::Request request;
                    request = qifeng::aas::PcmData {
                        .raw = pcmData.data(),
                        .size = static_cast<int32_t>(pcmData.size()),
                        .sampleRate = info->mConfig.mSampleRate,
                        .channels = info->mConfig.mChannels,
                        .bitDepth = info->mConfig.mBitDepth,
                    };
                    std::vector<uint8_t> audioSamplesBytes = AudioLoad::GetPCMData(request, dst);
                    if (audioSamplesBytes.empty()) {
                        SLOG_ERROR << "ResamplePcm failed: sample is empty";
                        continue;
                    }
                    std::vector<float> audioSamples;
                    size_t floatCount = audioSamplesBytes.size() / sizeof(float);
                    if (floatCount > 0) {
                        audioSamples.assign(reinterpret_cast<const float*>(audioSamplesBytes.data()),
                                            reinterpret_cast<const float*>(audioSamplesBytes.data()) + floatCount);
                    }
                    auto taskData = std::make_shared<qifeng::CommonTaskData>();
                    PrepareTaskData(taskData, {.info = info,
                                               .audioSamples = audioSamples,
                                               .dst = dst,
                                               .meeting = meeting,
                                               .archType = archType});
                    qifeng::aas::AasResult aasResult =
                        qifeng::aas::AasJobManager::GetInstance().RunAasSynchronous(taskData);
                    // 计算md5
                    for (auto& seg : aasResult.segments) {
                        seg.svEmbeddingMd5 = ComputeMd5(seg.svEmbedding);
                    }
                    info->mCallBack(aasResult, info->mBmsInfo);
                }
            }
        }

        void AasJobManager::Start(std::shared_ptr<GetAudioBase> getAudioBase, AasArchType archType, int numThreads) {
            // 创建modelcontext
            qifeng::ModelsManager::GetInstance().CreateContext();
            // 创建会议
            std::shared_ptr<AasMeeting> meeting = std::make_shared<AasMeeting>();
            // 运行 AAS 循环节点
            BatchExecutor& batchExecutor = BatchExecutor::GetInstance();
            mIsStop.store(false);
            for (int i = 0; i < numThreads; i++) {
                batchExecutor.AddTask([this, getAudioBase, meeting, archType]() {
                    this->AasThreadFunc(getAudioBase, meeting, archType);
                });
            }
        }

        void AasJobManager::Stop() {
            mIsStop.store(true);
            qifeng::ModelsManager::GetInstance().ResetModelContext();
        }

    }  // namespace aas
}  // namespace qifeng
