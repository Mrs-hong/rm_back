/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <functional>
#include <optional>
#include <random>
#include <thread>

#include "aas/aas_api.h"
#include "aas/aas_tasks.h"
#include "aas/session_speaker_manager.h"
#include "common/logger.h"

#include "sanisizer/sanisizer.hpp"

// hdbscan-cpp
#include "hdbscan/Runner/hdbscanParameters.hpp"
#include "hdbscan/Runner/hdbscanResult.hpp"
#include "hdbscan/Runner/hdbscanRunner.hpp"

// umappp + knncolle
#include "knncolle/knncolle.hpp"
#include "umappp/umappp.hpp"

namespace qifeng {
    namespace aas {
        const int MinClusterSize = 10;
        const int MinSamples = 10;
        const float CentroidCosineSimilarityThreshold = 0.55F;

        // ==================== VADTask 实现 ====================

        VADTask::VADTask(const std::string& nodeId) : DAGNode(nodeId) {
        }

        void VADTask::ModifyAudioSample(std::vector<float>& audioSample, int64_t& absoluteStartTimeMs,
                                        std::shared_ptr<AasMeeting>& meeting, int64_t& timeIntervalMs) {
            AudioBlockVec lastAudioBlock = meeting->GetLastAudioBlock();
            if (lastAudioBlock.remainingDurationMs > 0) {
                absoluteStartTimeMs -= lastAudioBlock.remainingDurationMs;
                audioSample.insert(audioSample.begin(), lastAudioBlock.data.begin(), lastAudioBlock.data.end());
                timeIntervalMs += lastAudioBlock.remainingDurationMs;

                // 清空缓冲区
                std::vector<float> emptyCache;
                meeting->UpdateAudioCache(emptyCache, false, {0, 0, 0});
            } else if (lastAudioBlock.remainingDurationMs == 0) {
                // 离线转写最后一个音频块
                // 只需要清空缓冲区
                std::vector<float> emptyCache;
                meeting->UpdateAudioCache(emptyCache, false, {0, 0, 0});
            }
        }

        bool VADTask::Execute(const std::shared_ptr<CommonTaskData>& sharedData) {
            SLOG_INFO << "Executing VAD Task: " << GetId();
            if (!CanExecute(sharedData)) {
                return false;
            }
            try {
                auto startTime = std::chrono::high_resolution_clock::now();
                std::vector<float>* audioSample = nullptr;
                int* sampleRate = nullptr;
                std::shared_ptr<AasMeeting>* meeting = nullptr;
                int64_t* pushIntervalMs = nullptr;
                int64_t* absoluteStartTimeMs = nullptr;
                bool* isOnline = nullptr;
                int64_t* timeIntervalMs = nullptr;
                // 架构
                AasArchType* archType = nullptr;
                ValidateVadInputParam params = {audioSample,         sampleRate, meeting,        pushIntervalMs,
                                                absoluteStartTimeMs, isOnline,   timeIntervalMs, archType};
                if (!ValidateVadInput(sharedData, params)) {
                    return false;
                }
                if ((*archType == AasArchType::QWEN)) {
                    ModifyAudioSample(*audioSample, *absoluteStartTimeMs, *meeting, *timeIntervalMs);
                }
                // 推理耗时
                auto actualOutput = RunVadFeatureInfer(*audioSample);
                actualOutput = ProcessVadSegments(actualOutput);
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                SLOG_INFO << "  VAD推理完成，耗时: " << duration.count() << "ms";
                if (actualOutput.empty()) {
                    SLOG_DEBUG << "VAD Task: VAD Process returned empty segments";
                    qifeng::aas::AasResult result;
                    auto seg = (*meeting)->mLastSegment;
                    seg.isFullSegment = 1;
                    result.segments.push_back(seg);
                    sharedData->Set("result", result);
                    sharedData->SetDataTrusted(false);
                    (*meeting)->UpdateAudioCache(*audioSample, false, {0, 0, 0});  // 清空缓冲区
                    (*meeting)->mLastFullText = "";
                    (*meeting)->mGlobalStartTimeMs = *absoluteStartTimeMs + *timeIntervalMs;
                    (*meeting)->mGlobalEndTimeMs = *absoluteStartTimeMs + *timeIntervalMs;
                    return true;
                }
                (*meeting)->UpdateAudioCache(*audioSample, false, {0, 0, 0});  // 清空缓冲区
                (*meeting)->UpdateAudioCache(*audioSample, true, {0, 0, 0});   // 更新缓存数据
                auto audioData = (*meeting)->MergeAudioCacheToVector();
                ExtractFeaturesParam param = {*absoluteStartTimeMs, *isOnline,       *sampleRate,
                                              *pushIntervalMs,      *timeIntervalMs, meeting};
                std::vector<qifeng::aas::ASRSpeechAndResultData> asrSpeechData;
                if ((*archType == AasArchType::PARAFORMER)) {
                    asrSpeechData = ExtractAsrFeatures(audioData, actualOutput, param);
                } else {
                    asrSpeechData = CalculateAsrAudioSegments(audioData, actualOutput, param);
                }
                auto svSpeechData = ExtractSvFeatures(audioData, actualOutput, param);
                sharedData->Set<std::vector<qifeng::aas::ASRSpeechAndResultData>>("asrSpeechData", asrSpeechData);
                sharedData->Set<std::vector<qifeng::aas::SVSpeechAndResultData>>("svSpeechData", svSpeechData);
                sharedData->Erase("audioSamples");
                endTime = std::chrono::high_resolution_clock::now();
                duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                SLOG_INFO << "  VAD任务完成，耗时: " << duration.count() << "ms";
                return true;
            } catch (const std::exception& e) {
                SLOG_ERROR << "  VAD推理失败: " << e.what();
                return false;
            }
            SLOG_INFO << "Finished VAD Task: " << GetId();
        }

        // ==================== ASRPuncTask 实现 ====================

        ASRPuncTask::ASRPuncTask(const std::string& nodeId) : DAGNode(nodeId) {
        }

        bool ASRPuncTask::Execute(const std::shared_ptr<CommonTaskData>& sharedData) {
            SLOG_INFO << "Executing ASR_PUNC Task: " << GetId();
            auto startTime = std::chrono::high_resolution_clock::now();

            // 检查任务是否可以执行
            if (!CanExecute(sharedData)) {
                return false;
            }

            // 获取数据
            std::vector<qifeng::aas::ASRSpeechAndResultData>* asrSpeechData =
                sharedData->Get<std::vector<qifeng::aas::ASRSpeechAndResultData>>("asrSpeechData");
            // 获取热词
            std::vector<std::string>* hotwords = sharedData->Get<std::vector<std::string>>("hotwords");
            // 校验是否为空
            if (!asrSpeechData || asrSpeechData->empty() || !hotwords) {
                SLOG_ERROR << "ASR_PUNC任务执行失败：asrSpeechData或hotwords为空";
                sharedData->SetDataTrusted(false);
                return false;
            }

            // 调用asr、punc模型进行推理
            std::string asrOutput;
            for (auto& segment : *asrSpeechData) {
                if (!hotwords || hotwords->empty()) {
                    asrOutput = qifeng::ModelsManager::GetInstance().ASRProcess(segment.speechData);
                } else {
                    asrOutput = qifeng::ModelsManager::GetInstance().ASRProcess(segment.speechData, *hotwords);
                }
                // 调用punc模型进行推理
                asrOutput = qifeng::ModelsManager::GetInstance().PUNCProcess(asrOutput);
                SLOG_DEBUG << "  ASR_PUNC实际输出: " << asrOutput;
                segment.text = asrOutput;
            }
            SLOG_INFO << "Finished ASR_PUNC Task: " << GetId();
            // 统计耗时
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            SLOG_INFO << "  ASR_PUNC推理完成，耗时: " << duration.count() << "ms";
            return true;
        }

        // ==================== QwenASRTask 实现 ====================

        QwenASRTask::QwenASRTask(const std::string& nodeId) : DAGNode(nodeId) {
        }

        bool QwenASRTask::Execute(const std::shared_ptr<CommonTaskData>& sharedData) {
            SLOG_INFO << "Executing QwenASR Task: " << GetId();
            auto startTime = std::chrono::high_resolution_clock::now();

            // 检查任务是否可以执行
            if (!CanExecute(sharedData)) {
                return false;
            }

            // 获取数据
            std::vector<qifeng::aas::ASRSpeechAndResultData>* asrSpeechData =
                sharedData->Get<std::vector<qifeng::aas::ASRSpeechAndResultData>>("asrSpeechData");
            // 校验asrSpeechData是否为空
            if (!asrSpeechData) {
                SLOG_ERROR << "ASR_PUNC任务执行失败：asrSpeechData为空";
                sharedData->SetDataTrusted(false);
                return false;
            }

            for (auto& segment : *asrSpeechData) {
                // 调用Qwen-ASR模型进行推理（离线整段识别），返回识别文本
                std::string result = qifeng::ModelsManager::GetInstance().QwenASRProcess(segment.rawAudioData);

                SLOG_DEBUG << "  QwenASR实际输出: sequence=" << result;
                segment.text = result;
            }

            SLOG_INFO << "Finished QwenASR Task: " << GetId();
            // 统计耗时
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            SLOG_INFO << "  QwenASR推理完成，耗时: " << duration.count() << "ms";
            return true;
        }

        // ==================== ExtractSpeakerEmbeddingTask 实现 ====================

        ExtractSpeakerEmbeddingTask::ExtractSpeakerEmbeddingTask(const std::string& nodeId) : DAGNode(nodeId) {
        }

        bool ExtractSpeakerEmbeddingTask::Execute(const std::shared_ptr<CommonTaskData>& sharedData) {
            SLOG_INFO << "Executing SV Task: " << GetId();

            // 统计ExtractSpeakerEmbeddingTask耗时
            auto startTime = std::chrono::high_resolution_clock::now();

            // 检查任务是否可以执行
            if (!CanExecute(sharedData)) {
                return false;
            }

            // 获取数据
            std::vector<qifeng::aas::SVSpeechAndResultData>* svSpeechData =
                sharedData->Get<std::vector<qifeng::aas::SVSpeechAndResultData>>("svSpeechData");
            // 获取archType
            AasArchType* archType = sharedData->Get<AasArchType>("archType");
            if (!archType) {
                SLOG_ERROR << "SV任务执行失败：archType为空";
                sharedData->SetDataTrusted(false);
                return false;
            }
            // 根据archType判断是否QWEN
            if (*archType == AasArchType::QWEN) {
                return true;
            }
            // 校验svSpeechData是否为空
            if (!svSpeechData) {
                SLOG_ERROR << "SV任务执行失败：svSpeechData为空";
                sharedData->SetDataTrusted(false);
                return false;
            }

            // 判断是否为流式模式
            bool streamingMode = false;
            auto* streamingFlag = sharedData->Get<bool>("streamingMode");
            if (streamingFlag && *streamingFlag) {
                streamingMode = true;
            }

            if (streamingMode) {
                // 流式模式：滑动窗口 + 批量推理
                for (auto& segment : *svSpeechData) {
                    if (segment.speechData.empty() || segment.speechData[0].empty()) {
                        SLOG_WARN << "SV流式推理：speechData为空，跳过";
                        continue;
                    }
                    auto windowResult = GenerateSlidingWindows(segment.speechData, segment.startTime, segment.endTime);
                    if (windowResult.fbanks.empty()) {
                        SLOG_WARN << "SV流式推理：滑动窗口为空，跳过";
                        continue;
                    }
                    auto embeddings = qifeng::ModelsManager::GetInstance().SVProcessBatch(windowResult.fbanks);
                    segment.svEmbedding = embeddings;
                    segment.windowStarts = std::move(windowResult.windowStarts);
                    segment.windowEnds = std::move(windowResult.windowEnds);
                    SLOG_DEBUG << "SV流式推理: seg=[" << segment.startTime << "," << segment.endTime
                               << "]ms windows=" << embeddings.size();
                }
            } else {
                // 离线模式：逐段推理（保持不变）
                for (auto& segment : *svSpeechData) {
                    auto svEmbedding = qifeng::ModelsManager::GetInstance().SVProcess(segment.speechData);
                    segment.svEmbedding = svEmbedding;
                }
            }
            SLOG_INFO << "Finished SV Task: " << GetId();
            // 统计耗时
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            SLOG_INFO << "  ExtractSpeakerEmbeddingTask耗时: " << duration.count() << "ms";

            return true;
        }

        ExtractSpeakerEmbeddingTask::SlidingWindowResult
        ExtractSpeakerEmbeddingTask::GenerateSlidingWindows(const qifeng::FloatMatrix& fbank, int64_t startTime,
                                                            int64_t endTime) {
            SlidingWindowResult result;
            int totalFrames = static_cast<int>(fbank.size());
            if (totalFrames <= 0) {
                return result;
            }

            // 帧移10ms → 每帧10ms
            const int64_t frameMs = 10;
            int64_t durationMs = endTime - startTime;

            // 缓冲区不足3s（kWindowFrames帧），直接用整段作为1个窗口
            if (totalFrames <= kWindowFrames) {
                result.fbanks.push_back(fbank);
                result.windowStarts.push_back(startTime);
                result.windowEnds.push_back(endTime);
                SLOG_DEBUG << "GenerateSlidingWindows: totalFrames=" << totalFrames << " <= " << kWindowFrames
                           << ", using single window";
                return result;
            }

            // 滑动窗口：3s窗口(kWindowFrames帧) + 1s步移(kShiftFrames帧)
            int numWindows = (totalFrames - kWindowFrames) / kShiftFrames + 1;
            for (int i = 0; i < numWindows; ++i) {
                int startFrame = i * kShiftFrames;
                int endFrame = startFrame + kWindowFrames;
                if (endFrame > totalFrames) {
                    endFrame = totalFrames;
                }

                qifeng::FloatMatrix windowFbank(fbank.begin() + startFrame, fbank.begin() + endFrame);
                int64_t winStart = startTime + static_cast<int64_t>(startFrame) * frameMs;
                int64_t winEnd = startTime + static_cast<int64_t>(endFrame) * frameMs;

                result.fbanks.push_back(std::move(windowFbank));
                result.windowStarts.push_back(winStart);
                result.windowEnds.push_back(winEnd);
            }

            SLOG_DEBUG << "GenerateSlidingWindows: totalFrames=" << totalFrames << " windows=" << numWindows
                       << " duration=" << durationMs << "ms";
            return result;
        }

        // ==================== AggregationTask 实现 ====================

        BmcvFaissDatabase& AggregationTask::GetVoiceprintDatabase() {
            // 全局单例：流式场景下多次构造 AggregationTask 也只 InitDevice 一次，
            // 且生命周期覆盖整个进程，SessionSpeakerManager 持有的引用始终有效
            static BmcvFaissDatabase instance;
            static std::once_flag initFlag;
            std::call_once(initFlag, []() {
                int devRet = instance.InitDevice(0);
                if (devRet != 0) {
                    SLOG_ERROR << "Failed to init BMCV device for VP database";
                }
                SLOG_DEBUG << "Voiceprint database initialized, entries: " << instance.GetNtotal();
            });
            return instance;
        }

        std::mutex& AggregationTask::GetDatabaseMutex() {
            static std::mutex instance;
            return instance;
        }

        size_t& AggregationTask::GetLastDbFingerprint() {
            static size_t lastFingerprint = 0;
            return lastFingerprint;
        }

        AggregationTask::AggregationTask(const std::string& nodeId) : DAGNode(nodeId) {
            // 触发单例初始化（仅首次会真正 InitDevice）
            (void)GetVoiceprintDatabase();
        }

        bool AggregationTask::InitDatabase(const std::shared_ptr<CommonTaskData>& sharedData) {
            // 从sharedData中取出数据，存入库中
            std::unordered_map<std::string, std::vector<std::vector<float>>>* data =
                sharedData->Get<std::unordered_map<std::string, std::vector<std::vector<float>>>>("svDatabase");
            if (!data) {
                SLOG_ERROR << "The ptr of svDatabase is nullptr";
                return false;
            }
            // 计算声纹库内容指纹：结合 vpId 集合与每个特征首元素之和
            // 流式场景下若声纹库未变化，跳过 Reset+重灌，减少重复 IO
            size_t fingerprint = 0;
            for (const auto& item : *data) {
                // 简单哈希：vpId 字符串哈希 + 特征首元素（bit_cast 避免 UB）
                size_t vpHash = std::hash<std::string> {}(item.first);
                float firstVal = item.second.empty() || item.second[0].empty() ? 0.0F : item.second[0][0];
                size_t valBits = 0;
                std::memcpy(&valBits, &firstVal, sizeof(float));
                fingerprint ^= vpHash + 0x9E3779B97F4A7C15ULL + (valBits << 1);
            }
            auto& lastFingerprint = GetLastDbFingerprint();
            if (fingerprint == lastFingerprint && !GetVoiceprintDatabase().IsEmpty()) {
                SLOG_DEBUG << "InitDatabase: fingerprint unchanged, skip rebuild (entries="
                           << GetVoiceprintDatabase().GetNtotal() << ")";
                return true;
            }
            lastFingerprint = fingerprint;
            // 先清空旧数据，防止流式场景下每次调用都追加导致库膨胀
            auto& db = GetVoiceprintDatabase();
            db.Reset();
            for (const auto& item : *data) {
                db.AddWithId(item.second[0], item.first);  // 之所以是0，是因为现在的batch size是1
            }
            SLOG_DEBUG << "InitDatabase: rebuilt, entries=" << db.GetNtotal() << ", fingerprint=" << fingerprint;
            return true;
        }

        bool AggregationTask::Execute(const std::shared_ptr<CommonTaskData>& sharedData) {
            SLOG_INFO << "Executing Aggregation Task: " << GetId();
            auto startTime = std::chrono::high_resolution_clock::now();
            if (!CanExecute(sharedData)) {
                return false;
            }
            if (!InitDatabase(sharedData)) {
                SLOG_ERROR << "svDatabase init failed!";
                return false;
            }
            std::vector<qifeng::aas::ASRSpeechAndResultData>* asrSpeechData =
                sharedData->Get<std::vector<qifeng::aas::ASRSpeechAndResultData>>("asrSpeechData");
            std::vector<qifeng::aas::SVSpeechAndResultData>* svSpeechData =
                sharedData->Get<std::vector<qifeng::aas::SVSpeechAndResultData>>("svSpeechData");
            std::unordered_map<std::string, std::vector<std::vector<float>>>* svDataBase =
                sharedData->Get<std::unordered_map<std::string, std::vector<std::vector<float>>>>("svDatabase");
            std::string* sessionId = sharedData->Get<std::string>("sessionId");
            bool* isOnline = sharedData->Get<bool>("isOnline");
            std::shared_ptr<AasMeeting>* meeting = sharedData->Get<std::shared_ptr<AasMeeting>>("meeting");
            if (!asrSpeechData || !svSpeechData || !svDataBase || !sessionId || !isOnline || !meeting || !*meeting) {
                SLOG_ERROR
                    << "Aggregation任务执行失败：asrSpeechData或svSpeechData或svDataBase或sessionId或isOnline或meeting为空";
                sharedData->SetDataTrusted(false);
                return false;
            }
            // 获取archType
            AasArchType* archType = sharedData->Get<AasArchType>("archType");
            if (!archType) {
                SLOG_ERROR << "Aggregation任务执行失败：archType为空";
                sharedData->SetDataTrusted(false);
                return false;
            }
            // 根据archType判断是否QWEN
            if (*archType == AasArchType::PARAFORMER) {
                int res = SpeakerVerify(*svSpeechData, *sessionId);
                if (res != 0) {
                    SLOG_ERROR << "SpeakerVerify failed: " << res;
                    sharedData->SetDataTrusted(false);
                    return false;
                }
            }
            qifeng::aas::AasResult result;
            result.code = 0;
            for (size_t i = 0; i < asrSpeechData->size(); ++i) {
                result.segments.emplace_back(
                    qifeng::aas::AasSegment {.startTime = asrSpeechData->at(i).startTime,
                                             .endTime = asrSpeechData->at(i).endTime,
                                             .text = std::move(asrSpeechData->at(i).text),
                                             .speakerLabel = svSpeechData->at(i).speakerId,
                                             .speakerName = std::move(svSpeechData->at(i).svResult),
                                             .svEmbedding = std::move(svSpeechData->at(i).svEmbedding),
                                             .svEmbeddingMd5 = std::move(svSpeechData->at(i).svEmbeddingMd5),
                                             .isFullSegment = asrSpeechData->at(i).isFullSegment,
                                             .asrContent = std::move(asrSpeechData->at(i).asrContent)});
            }
            // if (*isOnline) {
            //     ModifyAggregationResult(result, *meeting);
            // }
            sharedData->Set("result", result);
            sharedData->Erase("asrSpeechData");
            sharedData->Erase("svSpeechData");
            SLOG_INFO << "Finished Aggregation Task: " << GetId();
            // 统计耗时
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            SLOG_INFO << "  AggregationTask耗时: " << duration.count() << "ms";
            return true;
        }

        // 计算 UTF-8 字符串的 Unicode 字符数
        int AggregationTask::Utf8CharCount(const std::string& s) {
            int count = 0;
            for (std::size_t i = 0; i < s.size();) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if ((c & 0xE0) == 0xC0)
                    i += 2;
                else if ((c & 0xF0) == 0xE0)
                    i += 3;
                else if ((c & 0xF8) == 0xF0)
                    i += 4;
                else
                    i += 1;
                count++;
            }
            return count;
        }

        // 取 UTF-8 字符串最后 maxChars 个字符（按 Unicode 字符数）
        std::string AggregationTask::Utf8Tail(const std::string& s, int maxChars) {
            int totalChars = Utf8CharCount(s);
            if (totalChars <= maxChars) {
                return s;
            }
            // 从后往前跳过 totalChars - maxChars 个字符
            int skip = totalChars - maxChars;
            int count = 0;
            std::size_t i = 0;
            for (; i < s.size() && count < skip;) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if ((c & 0xE0) == 0xC0)
                    i += 2;
                else if ((c & 0xF0) == 0xE0)
                    i += 3;
                else if ((c & 0xF8) == 0xF0)
                    i += 4;
                else
                    i += 1;
                count++;
            }
            return s.substr(i);
        }

        std::pair<int, int> AggregationTask::FindTail(const std::string& text, int maxChars) {
            // 取最后 maxChars 个字符（按 Unicode 字符数，不是字节数）
            std::string tail = Utf8Tail(text, maxChars);

            // 从句尾往前找最后一个句尾标点（。！？；…\n）
            // UTF-8 编码: 。=E38082 ！=EFBC81 ？=EFBC9F ；=EFBC9B …=E280A6
            const std::pair<const char*, int> puncts[] = {
                {"\xE3\x80\x82", 3},  // 。
                {"\xEF\xBC\x81", 3},  // ！
                {"\xEF\xBC\x9F", 3},  // ？
                {"\xEF\xBC\x9B", 3},  // ；
                {"\xE2\x80\xA6", 3},  // …
                {"\n", 1},            // \n
            };

            // 从尾部往前找最后一个句尾标点（与 Python 版逻辑一致）
            int punctPos = -1;
            int punctLen = 0;
            for (const auto& [p, len] : puncts) {
                std::size_t pos = tail.rfind(p);
                if (pos != std::string::npos) {
                    int charPos = static_cast<int>(pos);
                    if (charPos > punctPos) {
                        punctPos = charPos;
                        punctLen = len;
                    }
                }
            }
            return std::make_pair(punctPos, punctLen);
        }

        void AggregationTask::GenerateSegemnt(qifeng::aas::AasResult& asrRecords, const Param& param,
                                              qifeng::aas::AasSegment& result, std::shared_ptr<AasMeeting>& meeting) {
            auto text1 = param.str.substr(0, static_cast<std::size_t>(param.res.first) +
                                                 static_cast<std::size_t>(param.res.second));
            auto text2 = param.str.substr(static_cast<std::size_t>(param.res.first) +
                                          static_cast<std::size_t>(param.res.second));
            qifeng::aas::AasSegment newSegment;
            newSegment.endTime = result.endTime;
            if (meeting->mGlobalStartTimeMs == meeting->mGlobalEndTimeMs) {
                newSegment.startTime = (result.endTime - result.startTime) / 2 + result.startTime;
                result.endTime = newSegment.startTime;
            } else {
                newSegment.startTime = result.startTime;
                result.endTime = meeting->mGlobalEndTimeMs;
                result.startTime = meeting->mGlobalStartTimeMs;
            }
            newSegment.isFullSegment = 0;
            newSegment.speakerLabel = result.speakerLabel;
            newSegment.speakerName = result.speakerName;
            newSegment.svEmbedding = result.svEmbedding;
            newSegment.svEmbeddingMd5 = result.svEmbeddingMd5;
            newSegment.text = text2 + result.asrContent.unfixText;

            result.text = text1;

            meeting->mGlobalStartTimeMs = newSegment.startTime;
            meeting->mGlobalEndTimeMs = newSegment.endTime;
            meeting->mLastFullText = text2;
            result.isFullSegment = 1;

            asrRecords.segments.push_back(newSegment);
            meeting->mLastSegment = newSegment;
        }

        void AggregationTask::FinalizeFullSegment(qifeng::aas::AasResult& asrRecords, qifeng::aas::AasSegment& result,
                                                  std::string str, std::shared_ptr<AasMeeting>& meeting) {
            result.text = str;
            auto tmpStartTime = result.startTime;
            auto tmpEndTime = result.endTime;
            if (meeting->mGlobalStartTimeMs != meeting->mGlobalEndTimeMs) {
                result.startTime = meeting->mGlobalStartTimeMs;
                result.endTime = meeting->mGlobalEndTimeMs;
                meeting->mGlobalStartTimeMs = tmpStartTime;
                meeting->mGlobalEndTimeMs = tmpEndTime;
            } else {
                meeting->mGlobalStartTimeMs = tmpEndTime;
                meeting->mGlobalEndTimeMs = tmpEndTime;
            }
            result.isFullSegment = 1;

            meeting->mLastFullText = "";

            if (!result.asrContent.unfixText.empty()) {
                // 构建一个新的segment
                qifeng::aas::AasSegment newSegment;
                newSegment.startTime = meeting->mGlobalStartTimeMs;
                newSegment.endTime = meeting->mGlobalEndTimeMs;
                newSegment.isFullSegment = 0;
                newSegment.speakerLabel = result.speakerLabel;
                newSegment.speakerName = result.speakerName;
                newSegment.svEmbedding = result.svEmbedding;
                newSegment.svEmbeddingMd5 = result.svEmbeddingMd5;
                newSegment.text = result.asrContent.unfixText;
                asrRecords.segments.push_back(newSegment);
            }
            meeting->mLastSegment = asrRecords.segments.back();
        }

        void AggregationTask::ModifyAggregationResult(qifeng::aas::AasResult& asrRecords,
                                                      std::shared_ptr<AasMeeting>& meeting) {
            // 获取AasManager单例
            if (asrRecords.segments.size() != 1) {
                SLOG_ERROR << "asrRecords.segments.size() != 1";
                return;
            }
            auto& result = asrRecords.segments.back();
            auto str = meeting->mLastFullText + result.asrContent.fixText;
            std::pair<int, int> res = FindTail(str);
            if (res.first >= 0) {  // 找到句尾标点
                // 判断这个句尾标点符号是不是str的最后的字符
                if (res.first + res.second == static_cast<int>(str.size())) {  // 句尾标点是最后一个字符
                    FinalizeFullSegment(asrRecords, result, str, meeting);
                } else {  // 句尾标点不是最后一个字符
                    GenerateSegemnt(asrRecords, Param {str, res}, result, meeting);
                }
            } else {  // 没有找到句尾标点，证明这句话还没有结束
                meeting->mGlobalEndTimeMs = result.endTime;
                result.isFullSegment = 0;
                result.text = str + result.asrContent.unfixText;
                result.startTime = meeting->mGlobalStartTimeMs;
                meeting->mLastFullText += result.asrContent.fixText;
                meeting->mLastSegment = result;
            }
        }

        // AggregationTask的辅助函数
        std::set<int> AggregationTask::CollectSpeakerIds(const std::vector<SVSpeechAndResultData>& svRecords) {
            std::set<int> allIds;
            for (const auto& rec : svRecords) {
                allIds.insert(rec.speakerId);
            }
            return allIds;
        }

        std::map<int, std::string>
        AggregationTask::CollectExistingLabels(const std::vector<SVSpeechAndResultData>& svRecords,
                                               const std::set<int>& allIds) {
            std::map<int, std::string> idToVpId;
            for (int sid : allIds) {
                for (const auto& rec : svRecords) {
                    if (rec.speakerId == sid && !rec.svResult.empty()) {
                        idToVpId[sid] = rec.svResult;
                        break;
                    }
                }
            }
            return idToVpId;
        }

        std::map<int, std::vector<float>>
        AggregationTask::ComputeSpeakerCentroidsById(const std::vector<SVSpeechAndResultData>& svRecords) {
            std::map<int, std::vector<std::vector<float>>> idToEmbeddings;
            for (const auto& rec : svRecords) {
                if (!rec.svEmbedding.empty()) {
                    idToEmbeddings[rec.speakerId].push_back(rec.svEmbedding[0]);
                }
            }

            std::map<int, std::vector<float>> idToCentroid;
            for (const auto& [sid, embs] : idToEmbeddings) {
                if (embs.empty()) {
                    continue;
                }
                size_t dim = embs[0].size();
                std::vector<float> centroid(dim, 0.0F);
                for (const auto& emb : embs) {
                    for (size_t d = 0; d < dim; ++d) {
                        centroid[d] += emb[d];
                    }
                }
                float invCount = 1.0F / static_cast<float>(embs.size());
                for (size_t d = 0; d < dim; ++d) {
                    centroid[d] *= invCount;
                }
                idToCentroid[sid] = centroid;
            }
            return idToCentroid;
        }

        void AggregationTask::MatchSpeakersFromDatabase(const std::map<int, std::vector<float>>& idToCentroid,
                                                        std::map<int, std::string>& idToVpId) {
            auto& db = GetVoiceprintDatabase();
            if (db.IsEmpty() || idToCentroid.empty()) {
                return;
            }

            std::vector<std::vector<float>> centroidQueries;
            std::vector<int> queryIds;
            for (const auto& [sid, centroid] : idToCentroid) {
                if (idToVpId.find(sid) != idToVpId.end()) {
                    continue;
                }
                centroidQueries.push_back(centroid);
                queryIds.push_back(sid);
            }

            if (centroidQueries.empty()) {
                return;
            }

            std::lock_guard<std::mutex> lock(GetDatabaseMutex());
            auto [batchIndices, batchDistances] = db.Search(centroidQueries, 1);

            for (size_t q = 0; q < centroidQueries.size(); ++q) {
                int sid = queryIds[q];
                if (batchIndices[q].empty() || batchIndices[q][0] < 0) {
                    continue;
                }

                int64_t vectorId = static_cast<int64_t>(batchIndices[q][0]);
                std::vector<float> neighborVec = db.Reconstruct(vectorId);

                if (neighborVec.size() != centroidQueries[q].size()) {
                    continue;
                }

                float cosSim = CosineSimilarity2(centroidQueries[q], neighborVec);
                if (cosSim <= CentroidCosineSimilarityThreshold) {
                    continue;
                }

                std::string vpId = db.GetVpId(vectorId);
                if (vpId.empty()) {
                    continue;
                }

                idToVpId[sid] = vpId;
                SLOG_DEBUG << "ResolveSpeakerLabels: id=" << std::to_string(sid) << " -> known speaker vpId=" << vpId
                           << ", cosSim=" << std::to_string(cosSim);
            }
        }

        void AggregationTask::AssignStrangerLabels(const std::set<int>& allIds, std::map<int, std::string>& idToVpId) {
            int strangerCounter = 1;
            for (int sid : allIds) {
                if (idToVpId.find(sid) == idToVpId.end()) {
                    idToVpId[sid] = "陌生人_" + std::to_string(strangerCounter++);
                }
            }
        }

        void AggregationTask::ApplyLabelsToRecords(std::vector<SVSpeechAndResultData>& svRecords,
                                                   const std::map<int, std::string>& idToVpId) {
            for (auto& rec : svRecords) {
                auto it = idToVpId.find(rec.speakerId);
                if (it != idToVpId.end()) {
                    rec.svResult = it->second;
                } else {
                    rec.svResult = "陌生人_0";
                }
            }
        }

        void AggregationTask::ResolveSpeakerLabels(std::vector<SVSpeechAndResultData>& svRecords) {
            if (svRecords.empty()) {
                return;
            }

            auto allIds = CollectSpeakerIds(svRecords);
            auto idToVpId = CollectExistingLabels(svRecords, allIds);
            auto idToCentroid = ComputeSpeakerCentroidsById(svRecords);

            MatchSpeakersFromDatabase(idToCentroid, idToVpId);
            AssignStrangerLabels(allIds, idToVpId);

            SLOG_DEBUG << "ResolveSpeakerLabels: speaker label mapping:";
            for (const auto& [sid, label] : idToVpId) {
                int count = 0;
                for (const auto& rec : svRecords) {
                    if (rec.speakerId == sid) {
                        count++;
                    }
                }
                SLOG_DEBUG << "  id=" << std::to_string(sid) << " -> label=" << label
                           << " (segments=" << std::to_string(count) << ")";
            }

            ApplyLabelsToRecords(svRecords, idToVpId);
        }
        void AggregationTask::SpeakerVerifyStreaming(std::vector<SVSpeechAndResultData>& svRecords,
                                                     const std::string& sessionId) {
            SLOG_DEBUG << "SpeakerVerifyStreaming: sessionId=" << sessionId
                       << ", svRecords=" << std::to_string(svRecords.size());
            auto sessionManager = SessionSpeakerManagerRegistry::GetInstance().GetOrCreate(sessionId, mSessionConfig);
            sessionManager->SetKnownDatabase(GetVoiceprintDatabase(), GetDatabaseMutex());
            // 每次流式请求清空窗口缓存，避免不同chunk因相对时间相同而错误命中缓存
            sessionManager->ClearCachedWindows();

            const int64_t kMinDurationMs = 2000;  // 最短有效语音段 2s
            int lastSpeakerId = -1;

            int cachedCount = 0;
            int newCount = 0;
            int shortCount = 0;
            for (size_t i = 0; i < svRecords.size(); ++i) {
                auto& rec = svRecords[i];

                // 不足2s的短段：直接复用上一次的说话人结果
                int64_t durationMs = rec.endTime - rec.startTime;
                if (durationMs < kMinDurationMs) {
                    if (lastSpeakerId >= 0) {
                        shortCount++;
                        rec.speakerId = lastSpeakerId;
                        SLOG_DEBUG << "SpeakerVerifyStreaming: sv[" << i << "]"
                                   << " start=" << rec.startTime << " end=" << rec.endTime << " short=" << durationMs
                                   << "ms <2s, reuse lastSpeakerId=" << lastSpeakerId;
                        continue;
                    }
                    SLOG_DEBUG << "SpeakerVerifyStreaming: sv[" << i << "]"
                               << " start=" << rec.startTime << " end=" << rec.endTime << " short=" << durationMs
                               << "ms <2s, no previous result, fallback to inference";
                }

                int sid = sessionManager->GetCachedWindowLabel(rec.startTime, rec.endTime);
                if (sid >= 0) {
                    cachedCount++;
                    lastSpeakerId = sid;
                    SLOG_DEBUG << "SpeakerVerifyStreaming: sv[" << i << "]"
                               << " start=" << rec.startTime << " end=" << rec.endTime
                               << " CACHED -> speakerId=" << sid;
                    rec.speakerId = sid;
                    continue;
                }

                newCount++;
                int64_t timestamp = rec.startTime;
                float activation = std::min(1.0F, static_cast<float>(durationMs) / 3000.0F);

                // svEmbedding 包含 N 个滑动窗口的 embedding
                // 投票机制：每个窗口独立识别，多数表决得出一个说话人
                int votedSid = -1;
                if (rec.svEmbedding.empty()) {
                    SLOG_WARN << "SpeakerVerifyStreaming: sv[" << i << "] svEmbedding为空，跳过";
                    rec.speakerId = -1;
                    continue;
                }

                if (rec.svEmbedding.size() == 1) {
                    // 单窗口：直接识别
                    votedSid = sessionManager->IdentifyInt(rec.svEmbedding[0], timestamp, activation);
                    SLOG_DEBUG << "SpeakerVerifyStreaming: sv[" << i << "] single window -> speakerId=" << votedSid;
                } else {
                    // 多窗口投票
                    std::map<int, int> voteCounts;  // speakerId -> 票数
                    for (size_t w = 0; w < rec.svEmbedding.size(); ++w) {
                        int winSid = sessionManager->IdentifyInt(rec.svEmbedding[w], timestamp, activation);
                        voteCounts[winSid]++;
                        SLOG_DEBUG << "SpeakerVerifyStreaming: sv[" << i << "] window[" << w
                                   << "] start=" << (w < rec.windowStarts.size() ? rec.windowStarts[w] : 0)
                                   << " -> speakerId=" << winSid;
                    }
                    // 多数表决：取票数最高的 speakerId
                    int maxVotes = 0;
                    for (const auto& [candidate, votes] : voteCounts) {
                        if (votes > maxVotes) {
                            maxVotes = votes;
                            votedSid = candidate;
                        }
                    }
                    SLOG_DEBUG << "SpeakerVerifyStreaming: sv[" << i << "] voting: " << voteCounts.size()
                               << " candidates, winner=" << votedSid << " votes=" << maxVotes << "/"
                               << rec.svEmbedding.size();
                }

                // 计算平均 embedding，用于 AddToBuffer
                std::vector<float> avgEmbedding(rec.svEmbedding[0].size(), 0.0F);
                for (const auto& emb : rec.svEmbedding) {
                    for (size_t d = 0; d < emb.size(); ++d) {
                        avgEmbedding[d] += emb[d];
                    }
                }
                float invCount = 1.0F / static_cast<float>(rec.svEmbedding.size());
                for (float& v : avgEmbedding) {
                    v *= invCount;
                }

                sessionManager->AddToBuffer(
                    votedSid, BufferEntry(avgEmbedding, timestamp, rec.startTime, rec.endTime, activation));
                sessionManager->CacheWindowLabel(rec.startTime, rec.endTime, votedSid);
                rec.speakerId = votedSid;
                lastSpeakerId = votedSid;

                float embNorm = 0.0F;
                for (float v : avgEmbedding) {
                    embNorm += v * v;
                }
                embNorm = std::sqrt(embNorm);
                SLOG_DEBUG << "SpeakerVerifyStreaming: sv[" << i << "]"
                           << " start=" << rec.startTime << " end=" << rec.endTime
                           << " windows=" << rec.svEmbedding.size() << " avgEmbNorm=" << embNorm
                           << " -> speakerId=" << votedSid;
            }
            SLOG_DEBUG << "SpeakerVerifyStreaming: total=" << svRecords.size() << ", new=" << newCount
                       << ", cached=" << cachedCount << ", short=" << shortCount;
            std::sort(svRecords.begin(), svRecords.end());
            for (size_t i = 0; i < svRecords.size(); ++i) {
                SLOG_DEBUG << "  sv[" << i << "] start=" << svRecords[i].startTime << " end=" << svRecords[i].endTime
                           << " speakerId=" << svRecords[i].speakerId;
            }
            OverlapVoting(svRecords);
            MergeShortSegments(svRecords);
            for (size_t i = 0; i < svRecords.size(); ++i) {
                SLOG_DEBUG << "  sv[" << i << "] start=" << svRecords[i].startTime << " end=" << svRecords[i].endTime
                           << " speakerId=" << svRecords[i].speakerId;
            }
            // 清理长期未命中的孤儿说话人，释放临时说话人槽位
            int64_t currentTimestamp =
                svRecords.empty() ? sessionManager->GetMaxProcessedEndMs() : svRecords.back().endTime;
            sessionManager->CleanupOrphans(currentTimestamp);
            SLOG_DEBUG << "SpeakerVerifyStreaming: tempSpeakerCount=" << sessionManager->GetTempSpeakerCount()
                       << ", activeSpeakerCount=" << sessionManager->GetActiveSpeakerCount()
                       << ", freeCenters=" << sessionManager->GetFreeCenterCount();
        }

        std::vector<std::vector<float>> AggregationTask::PPruning(const std::vector<std::vector<float>>& simMat) {
            if (simMat.empty()) {
                return {};
            }

            size_t n = simMat.size();
            std::vector<std::vector<float>> prunedMat = simMat;

            float pval = 0.022F;
            if (static_cast<float>(n) * pval < 6) {
                pval = 6.0F / static_cast<float>(n);
            }

            int nElems = static_cast<int>((1.0F - pval) * static_cast<float>(n));
            nElems = std::min(nElems, static_cast<int>(n) - 6);

            for (size_t i = 0; i < n; ++i) {
                std::vector<std::pair<float, int>> vals;
                for (size_t j = 0; j < n; ++j) {
                    vals.emplace_back(static_cast<float>(prunedMat[i][j]), static_cast<int>(j));
                }

                std::sort(vals.begin(), vals.end());

                for (int k = 0; k < nElems; ++k) {
                    prunedMat[i][static_cast<size_t>(vals[static_cast<size_t>(k)].second)] = 0.0F;
                }
            }

            return prunedMat;
        }

        std::vector<std::vector<float>> AggregationTask::Symmetrize(const std::vector<std::vector<float>>& A) {
            if (A.empty()) {
                return {};
            }

            size_t n = A.size();
            std::vector<std::vector<float>> symMat(n, std::vector<float>(n, 0.0F));

            for (size_t i = 0; i < n; ++i) {
                for (size_t j = 0; j < n; ++j) {
                    symMat[i][j] = 0.5F * (A[i][j] + A[j][i]);
                }
            }

            return symMat;
        }

        std::vector<std::vector<float>> AggregationTask::GetLaplacian(const std::vector<std::vector<float>>& M) {
            if (M.empty()) {
                return {};
            }

            size_t n = M.size();
            std::vector<std::vector<float>> l = M;

            for (size_t i = 0; i < n; ++i) {
                l[i][i] = 0.0F;
            }

            std::vector<float> d(n, 0.0F);
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = 0; j < n; ++j) {
                    d[i] += std::abs(l[i][j]);
                }
            }

            for (size_t i = 0; i < n; ++i) {
                for (size_t j = 0; j < n; ++j) {
                    if (i == j) {
                        l[i][j] = d[i];
                    } else {
                        l[i][j] = -l[i][j];
                    }
                }
            }

            return l;
        }

        std::vector<float> AggregationTask::GetEigenGaps(const std::vector<float>& eigVals) {
            std::vector<float> gaps;
            if (eigVals.size() < 2) {
                return gaps;
            }

            for (size_t i = 0; i < eigVals.size() - 1; ++i) {
                gaps.push_back(eigVals[i + 1] - eigVals[i]);
            }

            return gaps;
        }

        Eigen::MatrixXf AggregationTask::ConvertToEigenMatrix(const std::vector<std::vector<float>>& mat) {
            size_t n = mat.size();
            Eigen::MatrixXf result(n, n);
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = 0; j < n; ++j) {
                    result(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j)) = mat[i][j];
                }
            }
            return result;
        }

        bool AggregationTask::ComputeEigenDecomposition(const Eigen::MatrixXf& matrix, Eigen::VectorXf& outEigenvalues,
                                                        Eigen::MatrixXf& outEigenvectors) {
            Eigen::SelfAdjointEigenSolver<Eigen::MatrixXf> solver(matrix);
            if (solver.info() != Eigen::Success) {
                SLOG_ERROR << "Eigenvalue decomposition failed in spectral clustering";
                return false;
            }
            outEigenvalues = solver.eigenvalues();
            outEigenvectors = solver.eigenvectors();
            return true;
        }

        std::vector<float> AggregationTask::ExtractTopEigenvalues(const Eigen::VectorXf& eigenvalues, size_t k) {
            std::vector<float> eigVals(k);
            for (size_t i = 0; i < k; ++i) {
                eigVals[i] = eigenvalues(static_cast<Eigen::Index>(i));
            }

            std::string eigStr = "SpectralCluster: eigenvalues (first " + std::to_string(k) + ") = [";
            for (size_t i = 0; i < k; ++i) {
                if (i > 0) {
                    eigStr += ", ";
                }
                eigStr += std::to_string(eigVals[i]);
            }
            eigStr += "]";
            SLOG_DEBUG << eigStr;
            return eigVals;
        }

        int AggregationTask::DetermineClusterCount(const std::vector<float>& eigVals, size_t numPoints) {
            std::vector<float> gaps = GetEigenGaps(eigVals);

            std::string gapStr = "SpectralCluster: eigengaps = [";
            for (size_t i = 0; i < gaps.size(); ++i) {
                if (i > 0) {
                    gapStr += ", ";
                }
                gapStr += std::to_string(gaps[i]);
            }
            gapStr += "]";
            SLOG_DEBUG << gapStr;

            int numClusters = 1;
            if (!gaps.empty()) {
                auto maxIt = std::max_element(gaps.begin(), gaps.end());
                float maxGap = *maxIt;
                int maxGapIdx = static_cast<int>(std::distance(gaps.begin(), maxIt));
                SLOG_DEBUG << "SpectralCluster: max gap index=" << maxGapIdx << " max gap value=" << maxGap;

                float sumGap = 0.0F;
                for (float g : gaps) {
                    sumGap += g;
                }
                float meanGap = sumGap / static_cast<float>(gaps.size());
                float threshold = std::max(mEigengapSignificanceRatio * maxGap, meanGap);

                int bestIdx = maxGapIdx;
                for (size_t i = static_cast<size_t>(maxGapIdx) + 1; i < gaps.size(); ++i) {
                    if (gaps[i] >= threshold) {
                        bestIdx = static_cast<int>(i);
                    }
                }
                numClusters = bestIdx + 1;
                SLOG_DEBUG << "SpectralCluster: meanGap=" << meanGap << " threshold=" << threshold
                           << " bestIdx=" << bestIdx << " => numClusters=" << numClusters;
            }

            numClusters = std::max(numClusters, mMinSpeakers);
            numClusters = std::min(numClusters, mMaxSpeakers);
            numClusters = std::min(static_cast<int>(numClusters), static_cast<int>(numPoints));
            SLOG_DEBUG << "SpectralCluster: after bounds [min=" << mMinSpeakers << ", max=" << mMaxSpeakers
                       << "] => numClusters=" << numClusters;
            return numClusters;
        }

        std::vector<std::vector<float>> AggregationTask::ExtractEmbeddings(const Eigen::MatrixXf& eigenvectors,
                                                                           int numClusters) {
            size_t n = static_cast<size_t>(eigenvectors.rows());
            std::vector<std::vector<float>> emb(n, std::vector<float>(static_cast<size_t>(numClusters)));
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = 0; j < static_cast<size_t>(numClusters); ++j) {
                    emb[i][j] = eigenvectors(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j));
                }
            }
            return emb;
        }

        std::pair<std::vector<std::vector<float>>, int>
        AggregationTask::GetSpectralEmbeddings(const std::vector<std::vector<float>>& l) {
            if (l.empty()) {
                return {{}, 1};
            }

            auto lEigen = ConvertToEigenMatrix(l);

            Eigen::VectorXf eigenvalues;
            Eigen::MatrixXf eigenvectors;
            if (!ComputeEigenDecomposition(lEigen, eigenvalues, eigenvectors)) {
                return {{}, 1};
            }

            size_t k = static_cast<size_t>(std::min(11, static_cast<int>(l.size())));
            auto eigVals = ExtractTopEigenvalues(eigenvalues, k);

            int numClusters = DetermineClusterCount(eigVals, l.size());

            auto emb = ExtractEmbeddings(eigenvectors, numClusters);
            return {emb, numClusters};
        }

        std::vector<std::vector<float>> AggregationTask::KMeansInitPlusPlus(const std::vector<std::vector<float>>& data,
                                                                            int k) {
            size_t n = data.size();
            size_t d = data[0].size();

            std::vector<std::vector<float>> centroids;
            std::vector<float> minDistances(n, std::numeric_limits<float>::max());

            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, static_cast<int>(n - 1));
            size_t firstIdx = static_cast<size_t>(dis(gen));
            centroids.push_back(data[firstIdx]);

            for (int c = 1; c < k; ++c) {
                float totalDist = 0.0F;
                for (size_t i = 0; i < n; ++i) {
                    float dist = 0.0F;
                    for (size_t j = 0; j < d; ++j) {
                        float diff = data[i][j] - centroids.back()[j];
                        dist += diff * diff;
                    }
                    minDistances[i] = std::min(minDistances[i], dist);
                    totalDist += minDistances[i];
                }

                std::uniform_real_distribution<float> probDis(0.0F, totalDist);
                float threshold = probDis(gen);
                float cumsum = 0.0F;
                size_t nextIdx = 0;
                for (size_t i = 0; i < n; ++i) {
                    cumsum += minDistances[i];
                    if (cumsum >= threshold) {
                        nextIdx = i;
                        break;
                    }
                }
                centroids.push_back(data[nextIdx]);
            }

            return centroids;
        }

        int AggregationTask::FindNearestCluster(const std::vector<float>& point,
                                                const std::vector<std::vector<float>>& centroids) {
            float minDist = std::numeric_limits<float>::max();
            int bestCluster = 0;
            for (size_t c = 0; c < centroids.size(); ++c) {
                float dist = 0.0F;
                for (size_t j = 0; j < point.size(); ++j) {
                    float diff = point[j] - centroids[c][j];
                    dist += diff * diff;
                }
                if (dist < minDist) {
                    minDist = dist;
                    bestCluster = static_cast<int>(c);
                }
            }
            return bestCluster;
        }

        bool AggregationTask::AssignClusters(const std::vector<std::vector<float>>& data,
                                             const std::vector<std::vector<float>>& centroids,
                                             std::vector<int>& labels) {
            bool changed = false;
            for (size_t i = 0; i < data.size(); ++i) {
                int bestCluster = FindNearestCluster(data[i], centroids);
                if (labels[i] != bestCluster) {
                    labels[i] = bestCluster;
                    changed = true;
                }
            }
            return changed;
        }

        void AggregationTask::UpdateCentroids(const std::vector<std::vector<float>>& data,
                                              const std::vector<int>& labels, int k,
                                              std::vector<std::vector<float>>& centroids) {
            size_t d = data[0].size();
            std::vector<std::vector<float>> newCentroids(static_cast<size_t>(k), std::vector<float>(d, 0.0F));
            std::vector<int> counts(static_cast<size_t>(k), 0);

            for (size_t i = 0; i < data.size(); ++i) {
                size_t c = static_cast<size_t>(labels[i]);
                counts[c]++;
                for (size_t j = 0; j < d; ++j) {
                    newCentroids[c][j] += data[i][j];
                }
            }

            for (size_t c = 0; c < static_cast<size_t>(k); ++c) {
                if (counts[c] > 0) {
                    for (size_t j = 0; j < d; ++j) {
                        newCentroids[c][j] /= static_cast<float>(counts[c]);
                    }
                    centroids[c] = newCentroids[c];
                }
            }
        }

        float AggregationTask::ComputeInertia(const std::vector<std::vector<float>>& data,
                                              const std::vector<int>& labels,
                                              const std::vector<std::vector<float>>& centroids) {
            float inertia = 0.0F;
            for (size_t i = 0; i < data.size(); ++i) {
                for (size_t j = 0; j < data[i].size(); ++j) {
                    float diff = data[i][j] - centroids[static_cast<size_t>(labels[i])][j];
                    inertia += diff * diff;
                }
            }
            return inertia;
        }

        std::pair<std::vector<int>, float> AggregationTask::KMeansSingleRun(const std::vector<std::vector<float>>& data,
                                                                            int k) {
            size_t n = data.size();
            auto centroids = KMeansInitPlusPlus(data, k);
            std::vector<int> labels(n, 0);

            const int maxIters = 300;
            for (int iter = 0; iter < maxIters; ++iter) {
                if (!AssignClusters(data, centroids, labels)) {
                    break;
                }
                UpdateCentroids(data, labels, k, centroids);
            }

            float inertia = ComputeInertia(data, labels, centroids);
            return {labels, inertia};
        }

        std::vector<int> AggregationTask::KMeans(const std::vector<std::vector<float>>& data, int k) {
            size_t n = data.size();

            if (n == 0 || k <= 0 || static_cast<size_t>(k) > n) {
                return std::vector<int>(n, 0);
            }

            int nInit = 10;
            std::vector<int> bestLabels;
            float bestInertia = std::numeric_limits<float>::max();

            for (int run = 0; run < nInit; ++run) {
                auto [labels, inertia] = KMeansSingleRun(data, k);
                if (inertia < bestInertia) {
                    bestInertia = inertia;
                    bestLabels = labels;
                }
            }

            return bestLabels;
        }

        std::vector<int> AggregationTask::CorrectLabels(const std::vector<int>& labels) {
            std::vector<int> correctedLabels = labels;
            std::map<int, int> labelMap;
            int nextLabel = 0;

            for (size_t i = 0; i < correctedLabels.size(); ++i) {
                int oldLabel = correctedLabels[i];

                if (labelMap.find(oldLabel) == labelMap.end()) {
                    labelMap[oldLabel] = nextLabel++;
                }

                correctedLabels[i] = labelMap[oldLabel];
            }

            return correctedLabels;
        }

        float AggregationTask::CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
            float dot = 0.0F;
            float normA = 0.0F;
            float normB = 0.0F;
            for (size_t k = 0; k < a.size(); ++k) {
                dot += a[k] * b[k];
                normA += a[k] * a[k];
                normB += b[k] * b[k];
            }
            float denom = std::sqrt(normA) * std::sqrt(normB);
            return (denom > 0) ? dot / denom : 0.0F;
        }

        std::vector<std::vector<float>>
        AggregationTask::BuildSimilarityMatrix(const std::vector<std::vector<float>>& features) {
            size_t n = features.size();
            std::vector<std::vector<float>> simMat(n, std::vector<float>(n, 0.0F));
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = i; j < n; ++j) {
                    if (i == j) {
                        simMat[i][j] = 1.0F;
                    } else {
                        float cosSim = CosineSimilarity(features[i], features[j]);
                        simMat[i][j] = cosSim;
                        simMat[j][i] = cosSim;
                    }
                }
            }
            return simMat;
        }

        void AggregationTask::LogSimilarityStats(const std::vector<std::vector<float>>& simMat) {
            size_t n = simMat.size();
            float sumSim = 0.0F;
            float minSim = 2.0F;
            float maxSim = -2.0F;
            size_t cnt = 0;
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = i + 1; j < n; ++j) {
                    sumSim += simMat[i][j];
                    minSim = std::min(minSim, simMat[i][j]);
                    maxSim = std::max(maxSim, simMat[i][j]);
                    cnt++;
                }
            }
            SLOG_DEBUG << "SpectralCluster: simMat stats n=" << n
                       << " avg=" << (cnt > 0 ? sumSim / static_cast<float>(cnt) : 0.0F) << " min=" << minSim
                       << " max=" << maxSim;
        }

        std::vector<int> AggregationTask::SpectralCluster(const std::vector<std::vector<float>>& features) {
            if (features.empty()) {
                return {};
            }
            size_t n = features.size();
            if (n <= 1) {
                return std::vector<int>(n, 0);
            }

            auto simMat = BuildSimilarityMatrix(features);
            LogSimilarityStats(simMat);

            auto prunedMat = PPruning(simMat);
            auto symMat = Symmetrize(prunedMat);
            auto laplacian = GetLaplacian(symMat);
            auto [embeddings, numClusters] = GetSpectralEmbeddings(laplacian);

            SLOG_DEBUG << "SpectralCluster: GetSpectralEmbeddings returned numClusters=" << numClusters;

            numClusters = std::min(numClusters, static_cast<int>(n));
            if (numClusters < 1) {
                numClusters = 1;
            }

            auto labels = KMeans(embeddings, numClusters);
            return CorrectLabels(labels);
        }

        std::vector<float> AggregationTask::NormalizeFeaturesL2(const std::vector<std::vector<float>>& features) {
            size_t n = features.size();
            size_t dim = features[0].size();
            std::vector<float> normalizedData(dim * n);
            for (size_t i = 0; i < n; ++i) {
                float norm = 0.0F;
                for (size_t j = 0; j < dim; ++j) {
                    norm += features[i][j] * features[i][j];
                }
                norm = std::sqrt(norm);
                for (size_t j = 0; j < dim; ++j) {
                    normalizedData[j + i * dim] = (norm > 1e-10F) ? features[i][j] / norm : 0.0F;
                }
            }
            return normalizedData;
        }

        std::vector<std::vector<float>> AggregationTask::RunUmapReduction(const std::vector<float>& normalizedData,
                                                                          size_t dim, size_t n, size_t nComponents) {
            auto metric = std::make_shared<knncolle::EuclideanDistance<float, float>>();
            knncolle::VptreeBuilder<int, float, float> builder(metric);

            umappp::Options umapOpts;
            umapOpts.num_neighbors = 20;
            umapOpts.min_dist = 0.0;
            umapOpts.initialize_method = umappp::InitializeMethod::SPECTRAL;
            umapOpts.initialize_random_on_spectral_fail = true;

            std::vector<float> embedding(nComponents * n, 0.0F);
            auto status = umappp::initialize<int, float>(dim, static_cast<int>(n), normalizedData.data(), builder,
                                                         nComponents, embedding.data(), std::move(umapOpts));
            status.run(embedding.data());
            SLOG_DEBUG << "UMAP completed: n=" << n << ", dim=" << dim << " -> " << nComponents;

            std::vector<std::vector<float>> umapFeatures(n, std::vector<float>(nComponents));
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = 0; j < nComponents; ++j) {
                    umapFeatures[i][j] = embedding[j + i * nComponents];
                }
            }
            return umapFeatures;
        }

        std::vector<std::vector<double>>
        AggregationTask::ComputeCosineDistanceMatrix(const std::vector<std::vector<float>>& features) {
            size_t n = features.size();
            size_t dim = features[0].size();

            std::vector<double> norms(n, 0.0);
            for (size_t i = 0; i < n; ++i) {
                double s = 0.0;
                for (size_t k = 0; k < dim; ++k) {
                    s += static_cast<double>(features[i][k]) * features[i][k];
                }
                norms[i] = std::sqrt(s);
            }

            std::vector<std::vector<double>> distanceMat(n, std::vector<double>(n, 0.0));
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = i + 1; j < n; ++j) {
                    double dot = 0.0;
                    for (size_t k = 0; k < dim; ++k) {
                        dot += static_cast<double>(features[i][k]) * features[j][k];
                    }
                    double cosDist = 1.0 - dot / (norms[i] * norms[j] + 1e-10);
                    distanceMat[i][j] = cosDist;
                    distanceMat[j][i] = cosDist;
                }
            }
            return distanceMat;
        }

        std::vector<int>
        AggregationTask::RunHdbscanWithDistanceMatrix(const std::vector<std::vector<double>>& distanceMat, size_t n) {
            hdbscanParameters parameters;
            parameters.distances = distanceMat;
            parameters.minPoints = static_cast<uint32_t>(MinSamples);
            parameters.minClusterSize = static_cast<uint32_t>(MinClusterSize);

            hdbscanRunner runner;
            hdbscanResult result = runner.run(parameters);

            std::vector<int> labels(n);
            for (size_t i = 0; i < n; ++i) {
                labels[i] = (result.labels[i] == 0) ? -1 : result.labels[i] - 1;
            }

            SLOG_DEBUG << "UMAP+HDBSCAN clustering: n=" << n
                       << ", clusters=" << std::set<int>(labels.begin(), labels.end()).size()
                       << ", noise=" << std::count(labels.begin(), labels.end(), -1);
            return labels;
        }

        std::vector<int> AggregationTask::UmapHdbscanCluster(const std::vector<std::vector<float>>& features) {
            if (features.empty()) {
                return {};
            }
            size_t n = features.size();
            if (n < static_cast<size_t>(MinClusterSize)) {
                return std::vector<int>(n, 0);
            }

            size_t dim = features[0].size();
            size_t nComponents = std::min(static_cast<size_t>(60), n - 2);

            auto normalizedData = NormalizeFeaturesL2(features);
            auto umapFeatures = RunUmapReduction(normalizedData, dim, n, nComponents);
            auto distanceMat = ComputeCosineDistanceMatrix(umapFeatures);
            return RunHdbscanWithDistanceMatrix(distanceMat, n);
        }

        void AggregationTask::AccumulateFeature(std::vector<std::vector<float>>& centers, std::vector<int>& counts,
                                                size_t label, const std::vector<float>& feature) {
            if (counts[label] == 0) {
                centers[label] = feature;
            } else {
                for (size_t j = 0; j < feature.size(); ++j) {
                    centers[label][j] += feature[j];
                }
            }
            counts[label]++;
        }

        void AggregationTask::AverageCenters(std::vector<std::vector<float>>& centers, const std::vector<int>& counts) {
            for (size_t i = 0; i < centers.size(); ++i) {
                if (counts[i] > 0) {
                    for (size_t j = 0; j < centers[i].size(); ++j) {
                        centers[i][j] /= static_cast<float>(counts[i]);
                    }
                }
            }
        }

        std::pair<std::vector<std::vector<float>>, std::vector<int>>
        AggregationTask::ComputeSpeakerCenters(const std::vector<int>& labels,
                                               const std::vector<std::vector<float>>& features, size_t spkNum) {
            std::vector<std::vector<float>> centers(spkNum);
            std::vector<int> counts(spkNum, 0);

            for (size_t i = 0; i < labels.size(); ++i) {
                size_t label = static_cast<size_t>(labels[i]);
                if (label < spkNum) {
                    AccumulateFeature(centers, counts, label, features[i]);
                }
            }

            AverageCenters(centers, counts);
            return {centers, counts};
        }

        void AggregationTask::NormalizeVectorL2(std::vector<float>& vec) {
            float norm = 0.0F;
            for (float val : vec) {
                norm += val * val;
            }
            norm = std::sqrt(norm);
            if (norm > 1e-8F) {
                for (size_t j = 0; j < vec.size(); ++j) {
                    vec[j] /= norm;
                }
            }
        }

        void AggregationTask::NormalizeCentersL2(std::vector<std::vector<float>>& centers,
                                                 const std::vector<int>& counts) {
            for (size_t i = 0; i < centers.size(); ++i) {
                if (counts[i] > 0) {
                    NormalizeVectorL2(centers[i]);
                }
            }
        }

        std::pair<int, int> AggregationTask::FindMostSimilarPair(const std::vector<std::vector<float>>& normCenters,
                                                                 const std::vector<int>& counts,
                                                                 float& outMaxAffinity) {
            outMaxAffinity = -1.0F;
            std::pair<int, int> mergePair = {-1, -1};

            for (size_t i = 0; i < normCenters.size(); ++i) {
                if (counts[i] == 0) {
                    continue;
                }
                for (size_t j = i + 1; j < normCenters.size(); ++j) {
                    if (counts[j] == 0) {
                        continue;
                    }

                    float affinity = 0.0F;
                    for (size_t k = 0; k < normCenters[i].size(); ++k) {
                        affinity += normCenters[i][k] * normCenters[j][k];
                    }

                    if (affinity > outMaxAffinity) {
                        outMaxAffinity = affinity;
                        mergePair = {static_cast<int>(i), static_cast<int>(j)};
                    }
                }
            }
            return mergePair;
        }

        void AggregationTask::MergeLabels(std::vector<int>& labels, const std::pair<int, int>& mergePair) {
            for (size_t i = 0; i < labels.size(); ++i) {
                if (labels[i] == mergePair.second) {
                    labels[i] = mergePair.first;
                } else if (labels[i] > mergePair.second) {
                    labels[i]--;
                }
            }
        }

        std::vector<int> AggregationTask::MergeByCosineSimilarity(const std::vector<int>& labels,
                                                                  const std::vector<std::vector<float>>& features,
                                                                  float cosThr) {
            if (labels.empty() || features.empty()) {
                return labels;
            }

            std::vector<int> mergedLabels = labels;
            int maxLabel = *std::max_element(mergedLabels.begin(), mergedLabels.end());

            while (true) {
                size_t spkNum = static_cast<size_t>(maxLabel + 1);
                if (spkNum <= 1) {
                    break;
                }

                auto [spkCenters, spkCounts] = ComputeSpeakerCenters(mergedLabels, features, spkNum);
                NormalizeCentersL2(spkCenters, spkCounts);

                float maxAffinity = -1.0F;
                auto mergePair = FindMostSimilarPair(spkCenters, spkCounts, maxAffinity);

                if (maxAffinity < cosThr || mergePair.first == -1) {
                    break;
                }

                MergeLabels(mergedLabels, mergePair);
                maxLabel--;
            }

            return mergedLabels;
        }

        std::vector<std::vector<float>>
        AggregationTask::ExtractEmbeddings(const std::vector<SVSpeechAndResultData>& svRecords) {
            std::vector<std::vector<float>> features;
            for (const auto& record : svRecords) {
                features.push_back(record.svEmbedding[0]);
            }
            return features;
        }

        void AggregationTask::LogPairwiseSimilarity(const std::vector<std::vector<float>>& features) {
            if (features.size() < 2) {
                return;
            }
            size_t n = features.size();
            size_t dim = features[0].size();
            float sumSim = 0.0F;
            float maxSim = -2.0F;
            float minSim = 2.0F;
            size_t pairCount = 0;
            for (size_t i = 0; i < std::min(n, static_cast<size_t>(100)); ++i) {
                for (size_t j = i + 1; j < std::min(n, static_cast<size_t>(100)); ++j) {
                    float dot = 0.0F;
                    float ni = 0.0F;
                    float nj = 0.0F;
                    for (size_t d = 0; d < dim; ++d) {
                        dot += features[i][d] * features[j][d];
                        ni += features[i][d] * features[i][d];
                        nj += features[j][d] * features[j][d];
                    }
                    float cs = dot / (std::sqrt(ni) * std::sqrt(nj) + 1e-8F);
                    sumSim += cs;
                    maxSim = std::max(maxSim, cs);
                    minSim = std::min(minSim, cs);
                    pairCount++;
                }
            }
            float avgSim = pairCount > 0 ? sumSim / static_cast<float>(pairCount) : 0.0F;
            SLOG_DEBUG << "SpeakerVerifyOffline: pairwise cosine sim (first 100 samples) avg=" << avgSim
                       << " min=" << minSim << " max=" << maxSim << " pairs=" << pairCount;
        }

        std::vector<int> AggregationTask::ClusterFeatures(const std::vector<std::vector<float>>& features) {
            std::vector<int> labels;
            if (features.size() < 2048) {
                labels = SpectralCluster(features);
            } else {
                labels = UmapHdbscanCluster(features);
            }
            labels = MergeByCosineSimilarity(labels, features, mMergeCosThreshold);
            return CorrectLabels(labels);
        }

        int AggregationTask::CountClusters(const std::vector<int>& labels) {
            int numClusters = 0;
            for (int l : labels) {
                if (l + 1 > numClusters) {
                    numClusters = l + 1;
                }
            }
            return numClusters;
        }

        std::vector<AggregationTask::RttmEntry>
        AggregationTask::BuildRttmSegments(const std::vector<SVSpeechAndResultData>& svRecords,
                                           const std::vector<int>& labels) {
            std::vector<RttmEntry> segList;
            segList.reserve(svRecords.size());
            for (size_t i = 0; i < svRecords.size(); ++i) {
                float st = static_cast<float>(svRecords[i].startTime) / 1000.0F;
                float ed = static_cast<float>(svRecords[i].endTime - svRecords[i].startTime) / 1000.0F;
                segList.push_back({st, ed, labels[i]});
            }
            return segList;
        }

        std::vector<AggregationTask::RttmEntry>
        AggregationTask::MergeAdjacentSegments(const std::vector<RttmEntry>& segList) {
            std::vector<RttmEntry> merged;
            merged.reserve(segList.size());
            for (size_t i = 0; i < segList.size(); ++i) {
                float segSt = segList[i].start;
                float segEd = segList[i].end;
                int clusterId = segList[i].clusterId;

                if (merged.empty()) {
                    merged.push_back({segSt, segEd, clusterId});
                } else if (clusterId == merged.back().clusterId) {
                    if (segSt > merged.back().end) {
                        merged.push_back({segSt, segEd, clusterId});
                    } else {
                        merged.back().end = segEd;
                    }
                } else {
                    if (segSt < merged.back().end) {
                        float p = (merged.back().end + segSt) / 2.0F;
                        merged.back().end = p;
                        segSt = p;
                    }
                    merged.push_back({segSt, segEd, clusterId});
                }
            }
            return merged;
        }

        size_t AggregationTask::FindFeatureDimension(const std::vector<std::vector<float>>& features) {
            for (const auto& feat : features) {
                if (!feat.empty()) {
                    return feat.size();
                }
            }
            return 0;
        }

        void AggregationTask::AccumulateClusterFeatures(std::vector<std::vector<float>>& centroids,
                                                        std::vector<int>& counts,
                                                        const std::vector<std::vector<float>>& features,
                                                        const std::vector<int>& labels) {
            for (size_t i = 0; i < features.size(); ++i) {
                size_t label = static_cast<size_t>(labels[i]);
                if (label >= centroids.size()) {
                    continue;
                }
                counts[label]++;
                if (features[i].size() != centroids[label].size()) {
                    continue;
                }
                for (size_t d = 0; d < features[i].size(); ++d) {
                    centroids[label][d] += features[i][d];
                }
            }
        }

        void AggregationTask::AverageClusterCentroids(std::vector<std::vector<float>>& centroids,
                                                      const std::vector<int>& counts) {
            for (size_t c = 0; c < centroids.size(); ++c) {
                if (counts[c] > 0) {
                    float invCount = 1.0F / static_cast<float>(counts[c]);
                    for (size_t d = 0; d < centroids[c].size(); ++d) {
                        centroids[c][d] *= invCount;
                    }
                }
            }
        }

        std::pair<std::vector<std::vector<float>>, std::vector<int>>
        AggregationTask::ComputeClusterCentroids(const std::vector<std::vector<float>>& features,
                                                 const std::vector<int>& labels, int numClusters) {
            size_t dim = FindFeatureDimension(features);
            std::vector<std::vector<float>> centroids(static_cast<size_t>(numClusters), std::vector<float>(dim, 0.0F));
            std::vector<int> counts(static_cast<size_t>(numClusters), 0);

            AccumulateClusterFeatures(centroids, counts, features, labels);
            AverageClusterCentroids(centroids, counts);

            return {centroids, counts};
        }

        float AggregationTask::CosineSimilarity2(const std::vector<float>& a, const std::vector<float>& b) {
            if (a.size() != b.size() || a.empty()) {
                return 0.0F;
            }
            float dot = 0.0F;
            float normA = 0.0F;
            float normB = 0.0F;
            for (size_t d = 0; d < a.size(); ++d) {
                dot += a[d] * b[d];
                normA += a[d] * a[d];
                normB += b[d] * b[d];
            }
            return dot / (std::sqrt(normA) * std::sqrt(normB) + 1e-8F);
        }

        std::optional<std::pair<int, std::string>>
        AggregationTask::TryMatchSingleCluster(const std::vector<float>& centroid, int cid,
                                               const std::vector<float>& neighborVec) {
            if (neighborVec.size() != centroid.size()) {
                return std::nullopt;
            }
            float cosSim = CosineSimilarity2(centroid, neighborVec);
            if (cosSim > CentroidCosineSimilarityThreshold) {
                return std::make_pair(cid, std::to_string(cosSim));
            }
            SLOG_DEBUG << "SpeakerVerifyOffline: cluster " << std::to_string(cid)
                       << " best cosSim=" << std::to_string(cosSim)
                       << " < threshold=" << std::to_string(CentroidCosineSimilarityThreshold) << ", remains unknown";
            return std::nullopt;
        }

        std::map<int, std::string> AggregationTask::MatchKnownSpeakers(const std::vector<std::vector<float>>& centroids,
                                                                       const std::vector<int>& counts,
                                                                       int numClusters) {
            std::map<int, std::string> clusterToVpId;
            auto& db = GetVoiceprintDatabase();
            if (db.IsEmpty() || numClusters <= 0) {
                return clusterToVpId;
            }

            std::vector<std::vector<float>> centroidQueries;
            std::vector<int> queryClusterIds;
            for (size_t c = 0; c < static_cast<size_t>(numClusters); ++c) {
                if (counts[c] > 0 && !centroids[c].empty()) {
                    centroidQueries.push_back(centroids[c]);
                    queryClusterIds.push_back(static_cast<int>(c));
                }
            }

            if (centroidQueries.empty()) {
                return clusterToVpId;
            }

            std::lock_guard<std::mutex> lock(GetDatabaseMutex());
            auto [batchIndices, batchDistances] = db.Search(centroidQueries, 1);

            for (size_t q = 0; q < centroidQueries.size(); ++q) {
                if (batchIndices[q].empty() || batchIndices[q][0] < 0) {
                    continue;
                }

                int64_t vectorId = static_cast<int64_t>(batchIndices[q][0]);
                std::vector<float> neighborVec = db.Reconstruct(vectorId);
                int cid = queryClusterIds[q];

                auto match = TryMatchSingleCluster(centroidQueries[q], cid, neighborVec);
                if (!match.has_value()) {
                    continue;
                }

                std::string vpId = db.GetVpId(vectorId);
                if (vpId.empty()) {
                    continue;
                }

                clusterToVpId[cid] = vpId;
                SLOG_DEBUG << "SpeakerVerifyOffline: cluster " << std::to_string(cid)
                           << " -> known speaker vpId=" << vpId << ", cosSim=" << match->second;
            }
            return clusterToVpId;
        }

        std::vector<SVSpeechAndResultData>
        AggregationTask::BuildResultFromSegments(const std::vector<RttmEntry>& merged, int numClusters,
                                                 const std::map<int, std::string>& clusterToVpId) {
            std::vector<SVSpeechAndResultData> result;
            for (const auto& seg : merged) {
                int startMs = static_cast<int>(seg.start * 1000.0F);
                int endMs = static_cast<int>(seg.end * 1000.0F);
                int sid = (seg.clusterId >= 0 && seg.clusterId < numClusters) ? seg.clusterId : 0;
                std::string vpId;
                auto it = clusterToVpId.find(sid);
                if (it != clusterToVpId.end()) {
                    vpId = it->second;
                }
                result.push_back({{{}}, {{}}, vpId, sid, "", startMs, endMs});
            }
            return result;
        }

        void AggregationTask::LogRttmRaw(const std::vector<SVSpeechAndResultData>& svRecords,
                                         const std::vector<int>& labels) {
            std::stringstream rttmSs;
            rttmSs << "RTTM raw (subsegment-level, before make_rttms):" << std::endl;
            for (size_t i = 0; i < svRecords.size(); ++i) {
                float startSec = static_cast<float>(svRecords[i].startTime) / 1000.0F;
                float durSec = static_cast<float>(svRecords[i].endTime - svRecords[i].startTime) / 1000.0F;
                rttmSs << "SPEAKER rec 0 " << std::fixed << std::setprecision(3) << startSec << " " << durSec
                       << " <NA> <NA> " << labels[i] << " <NA> <NA>" << std::endl;
            }
            SLOG_DEBUG << rttmSs.str();
        }

        void AggregationTask::LogRttmMerged(const std::vector<RttmEntry>& merged) {
            std::stringstream rttmSs;
            rttmSs << "RTTM merged (after make_rttms):" << std::endl;
            for (const auto& seg : merged) {
                float dur = seg.end - seg.start;
                rttmSs << "SPEAKER rec 0 " << std::fixed << std::setprecision(3) << seg.start << " " << dur
                       << " <NA> <NA> " << seg.clusterId << " <NA> <NA>" << std::endl;
            }
            SLOG_DEBUG << rttmSs.str();
        }

        void AggregationTask::LogRttmFinal(const std::vector<SVSpeechAndResultData>& result) {
            std::stringstream rttmSs;
            rttmSs << "RTTM (cluster IDs):" << std::endl;
            for (const auto& rec : result) {
                float startSec = static_cast<float>(rec.startTime) / 1000.0F;
                float durSec = static_cast<float>(rec.endTime - rec.startTime) / 1000.0F;
                rttmSs << "SPEAKER rec 0 " << std::fixed << std::setprecision(3) << startSec << " " << durSec
                       << " <NA> <NA> " << rec.speakerId << " <NA> <NA>" << std::endl;
            }
            SLOG_DEBUG << rttmSs.str();
        }

        void AggregationTask::LogClusterInfo(int numClusters, const std::vector<int>& clusterCounts) {
            SLOG_DEBUG << "SpeakerVerifyOffline: numClusters=" << std::to_string(numClusters);
            for (size_t c = 0; c < static_cast<size_t>(numClusters); ++c) {
                SLOG_DEBUG << "  cluster " << c << " (members=" << clusterCounts[c] << ")";
            }
        }

        std::vector<SVSpeechAndResultData>
        AggregationTask::SpeakerVerifyOffline(const std::vector<SVSpeechAndResultData>& svRecords) {
            SLOG_DEBUG << "SpeakerVerifyOffline: offline mode, svRecords=" << svRecords.size();
            if (svRecords.empty()) {
                return {};
            }

            auto features = ExtractEmbeddings(svRecords);
            SLOG_DEBUG << "SpeakerVerifyOffline: embeddings total=" << features.size()
                       << ", dim=" << (features.empty() ? 0 : features[0].size());

            LogPairwiseSimilarity(features);

            auto labels = ClusterFeatures(features);
            int numClusters = CountClusters(labels);
            SLOG_DEBUG << "SpeakerVerifyOffline: clusters=" << numClusters;

            LogRttmRaw(svRecords, labels);

            auto segList = BuildRttmSegments(svRecords, labels);
            auto merged = MergeAdjacentSegments(segList);

            LogRttmMerged(merged);

            auto [clusterCentroids, clusterCounts] = ComputeClusterCentroids(features, labels, numClusters);
            LogClusterInfo(numClusters, clusterCounts);

            auto clusterToVpId = MatchKnownSpeakers(clusterCentroids, clusterCounts, numClusters);

            auto result = BuildResultFromSegments(merged, numClusters, clusterToVpId);

            OverlapVoting(result);
            MergeShortSegments(result);

            LogRttmFinal(result);

            SLOG_DEBUG << "SpeakerVerifyOffline: completed, records=" << std::to_string(result.size());
            return result;
        }

        int AggregationTask::SpeakerVerify(std::vector<SVSpeechAndResultData>& svRecords,
                                           const std::string& sessionId) {
            SLOG_DEBUG << "start speakrverify, sessionId=" << sessionId;
            if (svRecords.empty()) {
                SLOG_DEBUG << "empty svRecords";
                return -1;
            }

            SpeakerVerifyStreaming(svRecords, sessionId);
            ResolveSpeakerLabels(svRecords);  // 相对陌生人编号

            // 计算绝对陌生人编号
            auto sessionManager = SessionSpeakerManagerRegistry::GetInstance().Get(sessionId);
            if (sessionManager) {
                std::map<int, std::string> sessionLabels = sessionManager->GetSpeakerLabels();
                // 调试：打印 GetSpeakerLabels 返回的完整映射
                {
                    std::stringstream labelSs;
                    labelSs << "GetSpeakerLabels mapping: ";
                    for (const auto& [sid, label] : sessionLabels) {
                        labelSs << sid << "->" << label << " ";
                    }
                    SLOG_DEBUG << labelSs.str();
                }
                for (auto& rec : svRecords) {
                    auto it = sessionLabels.find(rec.speakerId);
                    if (it != sessionLabels.end()) {
                        SLOG_DEBUG << "SpeakerVerify: speakerId=" << rec.speakerId << " -> " << it->second;
                        rec.svResult = it->second;
                    } else {
                        SLOG_WARN << "SpeakerVerify: speakerId=" << rec.speakerId << " NOT found in sessionLabels";
                    }
                }
            }
            return 0;
        }

        void AggregationTask::OverlapVoting(std::vector<SVSpeechAndResultData>& svRecords) {
            if (svRecords.size() <= 2) {
                return;
            }
            std::vector<int> votedLabels(svRecords.size());
            for (size_t i = 0; i < svRecords.size(); ++i) {
                votedLabels[i] = svRecords[i].speakerId;
            }
            for (size_t i = 0; i < svRecords.size(); ++i) {
                std::map<int, float> weightedVotes;
                for (size_t j = 0; j < svRecords.size(); ++j) {
                    if (j == i) {
                        continue;
                    }
                    int64_t overlapStart = std::max(svRecords[i].startTime, svRecords[j].startTime);
                    int64_t overlapEnd = std::min(svRecords[i].endTime, svRecords[j].endTime);
                    if (overlapEnd > overlapStart) {
                        float overlap = static_cast<float>(overlapEnd - overlapStart);
                        weightedVotes[svRecords[j].speakerId] += overlap;
                    }
                }
                if (weightedVotes.empty()) {
                    continue;
                }
                float totalWeight = 0.0F;
                float maxWeight = -1.0F;
                int bestLabel = svRecords[i].speakerId;
                for (const auto& [label, weight] : weightedVotes) {
                    totalWeight += weight;
                    if (weight > maxWeight) {
                        maxWeight = weight;
                        bestLabel = label;
                    }
                }
                if (bestLabel != svRecords[i].speakerId && maxWeight > totalWeight * 0.5F) {
                    votedLabels[i] = bestLabel;
                    SLOG_DEBUG << "OverlapVoting: [" << i << "] " << svRecords[i].speakerId << " -> " << bestLabel
                               << ", maxWeight=" << maxWeight << ", totalWeight=" << totalWeight;
                }
            }
            int changeCount = 0;
            for (size_t i = 0; i < svRecords.size(); ++i) {
                if (svRecords[i].speakerId != votedLabels[i]) {
                    svRecords[i].speakerId = votedLabels[i];
                    changeCount++;
                }
            }
            if (changeCount > 0) {
                SLOG_DEBUG << "OverlapVoting: " << changeCount << " labels changed";
            }
        }

        bool AggregationTask::TryMergeOneShortSegment(std::vector<SVSpeechAndResultData>& svRecords) {
            for (size_t i = 1; i < svRecords.size() - 1; ++i) {
                if (svRecords[i].speakerId != svRecords[i - 1].speakerId &&
                    svRecords[i].speakerId != svRecords[i + 1].speakerId &&
                    svRecords[i - 1].speakerId == svRecords[i + 1].speakerId) {
                    int64_t durationMs = svRecords[i].endTime - svRecords[i].startTime;
                    if (durationMs > mMergeShortSegmentMaxMs) {
                        continue;
                    }
                    SLOG_DEBUG << "MergeShortSegments: [" << std::to_string(i) << "] "
                               << std::to_string(svRecords[i].speakerId) << " -> "
                               << std::to_string(svRecords[i - 1].speakerId)
                               << " (duration=" << std::to_string(durationMs) << "ms)";
                    svRecords[i].speakerId = svRecords[i - 1].speakerId;
                    return true;
                }
            }
            return false;
        }

        void AggregationTask::MergeShortSegments(std::vector<SVSpeechAndResultData>& svRecords) {
            if (svRecords.size() < 3) {
                return;
            }

            int mergeCount = 0;
            while (TryMergeOneShortSegment(svRecords)) {
                mergeCount++;
            }
            if (mergeCount > 0) {
                SLOG_DEBUG << "MergeShortSegments: " << mergeCount << " segments merged";
            }
        }

        // VAD的辅助函数的实现

        std::vector<std::vector<int>> VADTask::ProcessVadSegments(std::vector<std::vector<int>>& segments,
                                                                  int max_segment_time) {
            std::vector<std::vector<int>> result;
            // 如果没有分段，直接返回
            if (segments.empty()) {
                return result;
            }

            // 1. 按 start 排序
            std::sort(segments.begin(), segments.end());

            // 2. 合并重叠分段
            std::vector<std::vector<int>> merged;
            for (const auto& seg : segments) {
                // 如果 merged 为空 或 当前段与上一段不重叠
                if (merged.empty() || seg[0] > merged.back()[1]) {
                    merged.push_back(seg);
                } else {
                    // 有重叠，扩展上一段的 end
                    merged.back()[1] = std::max(merged.back()[1], seg[1]);
                }
            }

            // 3. 处理每一个分段
            for (const auto& seg : merged) {
                int start = seg[0];
                int end = seg[1];
                // 如果分段长度小于最大限制，直接加入
                if (end - start <= max_segment_time) {
                    result.push_back({start, end, 1});
                    continue;
                }
                // 4. 对长分段进行切分
                for (int cur = start; cur < end; cur += max_segment_time) {
                    int segEnd = std::min(cur + max_segment_time, end);
                    // 过滤掉过短片段（小于1秒）
                    if (segEnd - cur > 1000) {
                        // 判断是否是最后一个片段
                        // if ((end - segEnd) > 1000) {
                        //     result.push_back({cur, segEnd, 0});
                        // } else {
                        // 最后一个片段，直接加入
                        result.push_back({cur, segEnd, 1});
                        // }
                    }
                }
            }
            // 将最后一个分段的状态设为0
            if (!result.empty()) {
                result.back()[2] = 0;
            }
            return result;
        }

        bool VADTask::ValidateVadInput(const std::shared_ptr<CommonTaskData>& sharedData,
                                       ValidateVadInputParam& params) {
            params.meeting = sharedData->Get<std::shared_ptr<AasMeeting>>("meeting");
            if (!params.meeting) {
                SLOG_ERROR << "VAD Task: Meeting is null";
                sharedData->SetDataTrusted(false);
                return false;
            }
            params.audioSample = sharedData->Get<std::vector<float>>("audioSamples");
            if (!params.audioSample || params.audioSample->empty()) {
                SLOG_ERROR << "VAD Task: Audio sample is empty";
                sharedData->SetDataTrusted(false);
                return false;
            }
            params.sampleRate = sharedData->Get<int>("sampleRate");
            if (!params.sampleRate || *params.sampleRate <= 0) {
                SLOG_ERROR << "VAD Task: Invalid sample rate";
                sharedData->SetDataTrusted(false);
                return false;
            }
            params.pushIntervalMs = sharedData->Get<int64_t>("pushIntervalMs");
            if (!params.pushIntervalMs || *params.pushIntervalMs <= 0) {
                SLOG_ERROR << "VAD Task: Invalid pushIntervalMs";
                sharedData->SetDataTrusted(false);
                return false;
            }
            params.absoluteStartTimeMs = sharedData->Get<int64_t>("absoluteStartTimeMs");
            if (!params.absoluteStartTimeMs) {
                SLOG_ERROR << "VAD Task: absoluteStartTimeMs is null";
                sharedData->SetDataTrusted(false);
                return false;
            }
            params.isOnline = sharedData->Get<bool>("isOnline");
            if (!params.isOnline) {
                SLOG_ERROR << "VAD Task: isOnline is null";
                sharedData->SetDataTrusted(false);
                return false;
            }
            params.timeIntervalMs = sharedData->Get<int64_t>("timeIntervalMs");
            if (!params.timeIntervalMs || *params.timeIntervalMs <= 0) {
                SLOG_ERROR << "VAD Task: Invalid timeIntervalMs";
                sharedData->SetDataTrusted(false);
                return false;
            }
            params.archType = sharedData->Get<AasArchType>("archType");
            if (!params.archType) {
                SLOG_ERROR << "VAD Task: archType is null";
                sharedData->SetDataTrusted(false);
                return false;
            }
            return true;
        }

        std::vector<std::vector<int>> VADTask::RunVadFeatureInfer(const std::vector<float>& audioSample) {
            // Silero VAD 直接从原始音频检测，不再需要 Fbank+LFR+CMVN 特征
            qifeng::FloatMatrix speechFeat;
            return qifeng::ModelsManager::GetInstance().VADProcess(speechFeat, audioSample);
        }

        std::vector<float> VADTask::ExtractAudioSegment(const std::vector<float>& audioSample, int sampleRate,
                                                        int startMs, int endMs) {
            int startSample = static_cast<int>(startMs * sampleRate / 1000.0);
            int endSample = static_cast<int>(endMs * sampleRate / 1000.0);
            startSample = std::max(0, startSample);
            endSample = std::min(static_cast<int>(audioSample.size()), endSample);

            if (endSample <= startSample) {
                return {};
            }
            return std::vector<float>(audioSample.begin() + startSample, audioSample.begin() + endSample);
        }

        std::vector<float> VADTask::MergeSegmentsToAudio(const std::vector<float>& audioSample, int sampleRate,
                                                         const std::vector<std::vector<int>>& segments) {
            std::vector<float> result;
            for (const auto& seg : segments) {
                auto extract = ExtractAudioSegment(audioSample, sampleRate, seg[0], seg[1]);
                result.insert(result.end(), extract.begin(), extract.end());
            }
            return result;
        }

        std::vector<qifeng::aas::ASRSpeechAndResultData>
        VADTask::ExtractAsrFeatures(const std::vector<float>& audioSample, std::vector<std::vector<int>>& segments,
                                    const ExtractFeaturesParam& param) {
            std::vector<qifeng::aas::ASRSpeechAndResultData> asrSpeechData;
            for (const auto& segment : segments) {
                int startMs = segment[0];
                int endMs = segment[1];
                SLOG_DEBUG << "  ASR每个片段的时间区间: " << startMs << "ms ~ " << endMs << "ms";
                auto audioSegment = ExtractAudioSegment(audioSample, param.sampleRate, startMs, endMs);
                if (!audioSegment.empty()) {
                    auto res = qifeng::ModelsManager::GetInstance().AsrFeatureProcess(audioSegment);
                    asrSpeechData.push_back({{},
                                             res,
                                             "",
                                             startMs + param.absoluteStartTimeMs,
                                             endMs + param.absoluteStartTimeMs,
                                             static_cast<bool>(segment[2])});
                } else {
                    SLOG_WARN << "  ASR片段无效，跳过: [" << startMs + param.absoluteStartTimeMs << ","
                              << endMs + param.absoluteStartTimeMs << "]";
                }
            }
            return asrSpeechData;
        }

        std::vector<qifeng::aas::SVSpeechAndResultData>
        VADTask::ExtractSvFeatures(const std::vector<float>& audioSample, std::vector<std::vector<int>>& segments,
                                   const ExtractFeaturesParam& param) {
            std::vector<qifeng::aas::SVSpeechAndResultData> svSpeechData;
            for (const auto& segment : segments) {
                int startMs = segment[0];
                int endMs = segment[1];
                SLOG_DEBUG << "  SV每个片段的时间区间: " << startMs << "ms ~ " << endMs << "ms";
                auto audioSegment = ExtractAudioSegment(audioSample, param.sampleRate, startMs, endMs);
                if (!audioSegment.empty()) {
                    auto res = qifeng::ModelsManager::GetInstance().VpFeatureProcess(audioSegment);
                    svSpeechData.push_back({res, {{}}, "", 0, "", startMs, endMs, 0});
                } else {
                    SLOG_WARN << "  SV片段无效，跳过: [" << startMs << "," << endMs << "]";
                }
            }
            return svSpeechData;
        }

        std::vector<qifeng::aas::ASRSpeechAndResultData>
        VADTask::CalculateAsrAudioSegments(const std::vector<float>& audioSample,
                                           std::vector<std::vector<int>>& segments, const ExtractFeaturesParam& param) {
            std::vector<qifeng::aas::ASRSpeechAndResultData> asrSpeechData;

            if (param.pushIntervalMs > param.timeIntervalMs) {
                int64_t startTimeMs = segments[0][0] + param.absoluteStartTimeMs;
                int64_t endTimeMs = segments.back()[1] + param.absoluteStartTimeMs;
                auto mergedAudio = MergeSegmentsToAudio(audioSample, param.sampleRate, segments);
                asrSpeechData.push_back({mergedAudio, {{}}, "", startTimeMs, endTimeMs, 0});
                std::vector<float> emptyCache;
                (*param.meeting)->UpdateAudioCache(emptyCache, false, {0, 0, 0});  // 清空缓冲区
                return asrSpeechData;
            } else {
                // 先判断是否有分段
                if (segments.size() != 1) {  // 不只有一个片段
                    segments.pop_back();
                }
                int64_t startTimeMs = segments[0][0];
                int64_t endTimeMs = segments.back()[1];
                auto segmentAudio = MergeSegmentsToAudio(audioSample, param.sampleRate, segments);
                int64_t remainDurationMs = param.timeIntervalMs - endTimeMs;
                if (remainDurationMs > 0) {
                    std::vector<float> seg =
                        ExtractAudioSegment(audioSample, param.sampleRate, static_cast<int>(endTimeMs),
                                            static_cast<int>(endTimeMs + remainDurationMs));
                    (*param.meeting)
                        ->UpdateAudioCache(seg, true,
                                           {endTimeMs + param.absoluteStartTimeMs,
                                            endTimeMs + param.absoluteStartTimeMs + remainDurationMs,
                                            remainDurationMs});  // 更新缓存数据
                }
                startTimeMs += param.absoluteStartTimeMs;
                endTimeMs += param.absoluteStartTimeMs;
                asrSpeechData.push_back({segmentAudio, {{}}, "", startTimeMs, endTimeMs, 0});
                return asrSpeechData;
            }
        }

    }  // namespace aas
}  // namespace qifeng
