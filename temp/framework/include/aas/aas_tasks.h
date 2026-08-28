/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_AAS_AAS_TASKS_H
#define QIFENG_FRAMEWORK_AAS_AAS_TASKS_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aas_callback.h"
#include "asr/model.h"
#include "bmcv_faiss_database.h"
#include "common/ringbuffer.h"
#include "dag/node.h"
#include "models_hm/manager/models_manager.h"
#include "session_speaker_manager.h"

#include "Eigen/Dense"
#include "Eigen/Eigenvalues"

namespace qifeng {
    namespace aas {

        struct ASRSpeechAndResultData {
            // 原始音频数据
            std::vector<float> rawAudioData;
            // 语音转文字功能的输入
            qifeng::FloatMatrix speechData;
            // 语音转文字功能的输出
            std::string text;
            // 时间戳
            int64_t startTime;
            int64_t endTime;
            // 是否是完整的片段
            bool isFullSegment;
            // 语音转写大模型流式推理生成单次返回内容
            StreamInferContent asrContent;

            bool operator<(const ASRSpeechAndResultData& other) const {
                return startTime < other.startTime;
            }
        };

        struct SVSpeechAndResultData {
            // 说话人验证功能的输入
            qifeng::FloatMatrix speechData;
            // 说话人验证功能的输出
            qifeng::FloatMatrix svEmbedding;
            // 说话人验证功能的输出(说话人身份)
            std::string svResult;
            // 说话人整数ID（聚类/识别阶段设置）
            int speakerId;
            std::string svEmbeddingMd5;
            // 时间戳
            int64_t startTime;
            int64_t endTime;
            // 是否是完整的片段
            bool isFullSegment;
            // 流式SV滑动窗口详情（调试/日志用）
            std::vector<int64_t> windowStarts;
            std::vector<int64_t> windowEnds;

            bool operator<(const SVSpeechAndResultData& other) const {
                return startTime < other.startTime;
            }
        };

        struct AudioBlockVec {
            std::vector<float> data;
            bool hasVoice = false;            // 标识该块是否包含人声（VAD 判定结果）
            int64_t startTimeMs = 0;          // 该块在原始音频中的起始时间偏移（毫秒）
            int64_t endTimeMs = 0;            // 该块在原始音频中的结束时间偏移（毫秒）
            int64_t remainingDurationMs = 0;  // 标志离线模式中，剩余音频的时长

            // 兼容原有 val.size() 调用
            [[nodiscard]] size_t size() const {
                return data.size();
            }

            // 兼容原有 for (const auto& v : val) 遍历
            [[nodiscard]] const float* begin() const {
                return data.data();
            }
            [[nodiscard]] const float* end() const {
                return data.data() + data.size();
            }
            [[nodiscard]] float* begin() {
                return data.data();
            }
            [[nodiscard]] float* end() {
                return data.data() + data.size();
            }
        };
        using AudioCacheVec = RingBuffer<AudioBlockVec, 5>;

        // 创建一个会议类，用于存储这场会议的全局变量
        class AasMeeting {
        public:
            AasMeeting() = default;
            ~AasMeeting() {
                mAudioCache.clear();
                mGlobalStartTimeMs = 0;
                mGlobalEndTimeMs = 0;
                mLastFullText.clear();
                mLastSegment = qifeng::aas::AasSegment();
            };
            AasMeeting(const AasMeeting&) = delete;
            AasMeeting& operator=(const AasMeeting&) = delete;
            AasMeeting(AasMeeting&&) = delete;
            AasMeeting& operator=(AasMeeting&&) = delete;

            /**
             * @brief 将所有缓存的音频块数据合并为一个连续的 float 向量
             * @return 合并后的音频数据向量
             */
            std::vector<float> MergeAudioCacheToVector();

            /**
             * @brief 更新缓存的音频块
             * @param audioSamples 音频样本向量
             * @param hasVoice 是否包含人声（VAD 判定结果）
             * @return 是否成功更新缓存
             */
            struct TimeInfo {
                int64_t startTimeMs = 0;
                int64_t endTimeMs = 0;
                int64_t remainingDurationMs = 0;
            };
            bool UpdateAudioCache(std::vector<float>& audioSamples, bool hasVoice, TimeInfo timeInfo);

            /**
             * @brief 获取缓存中最后一个音频块
             * @return 最后一个 AudioBlockVec 的 const 引用（缓存为空时行为未定义，调用前请检查 mAudioCache.empty()）
             */
            AudioBlockVec& GetLastAudioBlock() {
                return mAudioCache.GetBackBlock();
            }

            /**
             * @brief 检查缓存是否为空
             * @return 如果缓存为空则返回 true，否则返回 false
             */
            bool IsCacheEmpty() const {
                return mAudioCache.empty();
            }

        public:
            // 缓存10秒音频
            AudioCacheVec mAudioCache;
            int64_t mGlobalStartTimeMs = 0;
            int64_t mGlobalEndTimeMs = 0;
            std::string mLastFullText;
            // 记录返回给外部的segement
            qifeng::aas::AasSegment mLastSegment;
            //记录上一次的info
            std::shared_ptr<ResultInfo> mLastInfo;
        };

        /**
         * @brief VAD 任务节点（语音活动检测）
         */
        class VADTask : public DAGNode {
        public:
            explicit VADTask(const std::string& nodeId);
            VADTask(const VADTask&) = delete;
            VADTask& operator=(const VADTask&) = delete;
            VADTask(VADTask&&) = delete;
            VADTask& operator=(VADTask&&) = delete;
            ~VADTask() = default;

            bool Execute(const std::shared_ptr<CommonTaskData>& sharedData) override;

        private:
            // 以下是为了实现VADTask的Execute方法而定义的辅助函数
            void ModifyAudioSample(std::vector<float>& audioSample, int64_t& absoluteStartTimeMs,
                                   std::shared_ptr<AasMeeting>& meeting, int64_t& timeIntervalMs);
            std::vector<std::vector<int>> ProcessVadSegments(std::vector<std::vector<int>>& segments,
                                                             int max_segment_time = 15000);
            struct ValidateVadInputParam {
                std::vector<float>*& audioSample;
                int*& sampleRate;
                std::shared_ptr<AasMeeting>*& meeting;
                int64_t*& pushIntervalMs;
                int64_t*& absoluteStartTimeMs;
                bool*& isOnline;
                int64_t*& timeIntervalMs;
                AasArchType*& archType;
            };
            bool ValidateVadInput(const std::shared_ptr<CommonTaskData>& sharedData, ValidateVadInputParam& params);
            bool GetConfigData(const std::shared_ptr<CommonTaskData>& sharedData, int64_t*& absoluteStartTimeMs,
                               bool*& isOnline, int64_t*& timeIntervalMs);
            std::vector<std::vector<int>> RunVadFeatureInfer(const std::vector<float>& audioSample);
            std::vector<float> ExtractAudioSegment(const std::vector<float>& audioSample, int sampleRate, int startMs,
                                                   int endMs);
            struct ExtractFeaturesParam {
                int64_t absoluteStartTimeMs;
                bool isOnline;
                int sampleRate;
                int64_t pushIntervalMs;
                int64_t timeIntervalMs;
                std::shared_ptr<AasMeeting>* meeting;
            };
            std::vector<float> MergeSegmentsToAudio(const std::vector<float>& audioSample, int sampleRate,
                                                    const std::vector<std::vector<int>>& segments);
            std::vector<qifeng::aas::ASRSpeechAndResultData> ExtractAsrFeatures(const std::vector<float>& audioSample,
                                                                                std::vector<std::vector<int>>& segments,
                                                                                const ExtractFeaturesParam& param);
            std::vector<qifeng::aas::SVSpeechAndResultData> ExtractSvFeatures(const std::vector<float>& audioSample,
                                                                              std::vector<std::vector<int>>& segments,
                                                                              const ExtractFeaturesParam& param);

            std::vector<qifeng::aas::ASRSpeechAndResultData>
            CalculateAsrAudioSegments(const std::vector<float>& audioSample, std::vector<std::vector<int>>& segments,
                                      const ExtractFeaturesParam& param);
        };

        /**
         * @brief ASR_PUNC 任务节点（带标点的语音识别）
         */
        class ASRPuncTask : public DAGNode {
        public:
            ASRPuncTask() = delete;
            ASRPuncTask(const ASRPuncTask&) = delete;
            ASRPuncTask& operator=(const ASRPuncTask&) = delete;
            ASRPuncTask(ASRPuncTask&&) = delete;
            ASRPuncTask& operator=(ASRPuncTask&&) = delete;
            explicit ASRPuncTask(const std::string& nodeId);
            ~ASRPuncTask() override = default;

            bool Execute(const std::shared_ptr<CommonTaskData>& sharedData) override;

        private:
            // ASR_PUNC 任务特定参数可以放在这里
        };

        /**
         * @brief QwenASR 任务节点（带标点的语音识别）
         */
        class QwenASRTask : public DAGNode {
        public:
            QwenASRTask() = delete;
            QwenASRTask(const QwenASRTask&) = delete;
            QwenASRTask& operator=(const QwenASRTask&) = delete;
            QwenASRTask(QwenASRTask&&) = delete;
            QwenASRTask& operator=(QwenASRTask&&) = delete;
            explicit QwenASRTask(const std::string& nodeId);
            ~QwenASRTask() override = default;

            bool Execute(const std::shared_ptr<CommonTaskData>& sharedData) override;

        private:
            // QwenASR 任务特定参数可以放在这里
        };

        /**
         * @brief SV 任务节点（说话人验证）
         */
        class ExtractSpeakerEmbeddingTask : public DAGNode {
        public:
            ExtractSpeakerEmbeddingTask() = delete;
            ExtractSpeakerEmbeddingTask(const ExtractSpeakerEmbeddingTask&) = delete;
            ExtractSpeakerEmbeddingTask& operator=(const ExtractSpeakerEmbeddingTask&) = delete;
            ExtractSpeakerEmbeddingTask(ExtractSpeakerEmbeddingTask&&) = delete;
            ExtractSpeakerEmbeddingTask& operator=(ExtractSpeakerEmbeddingTask&&) = delete;
            explicit ExtractSpeakerEmbeddingTask(const std::string& nodeId);
            ~ExtractSpeakerEmbeddingTask() override = default;

            bool Execute(const std::shared_ptr<CommonTaskData>& sharedData) override;

        private:
            // 流式模式：对 Fbank 特征做滑动窗口切片（3s窗口 + 1s步移）
            // framesPerMs = 0.1（帧移10ms），3s=300帧，1s=100帧
            static constexpr int kWindowFrames = 300;  // 3s 窗口帧数
            static constexpr int kShiftFrames = 100;   // 1s 步移帧数

            struct SlidingWindowResult {
                std::vector<qifeng::FloatMatrix> fbanks;  // 各窗口 Fbank
                std::vector<int64_t> windowStarts;        // 各窗口起始时间(ms)
                std::vector<int64_t> windowEnds;          // 各窗口结束时间(ms)
            };
            SlidingWindowResult GenerateSlidingWindows(const qifeng::FloatMatrix& fbank, int64_t startTime,
                                                       int64_t endTime);
        };

        /**
         * @brief Aggregation 任务节点（结果聚合）
         */
        class AggregationTask : public DAGNode {
        public:
            AggregationTask() = delete;
            AggregationTask(const AggregationTask&) = delete;
            AggregationTask& operator=(const AggregationTask&) = delete;
            AggregationTask(AggregationTask&&) = delete;
            AggregationTask& operator=(AggregationTask&&) = delete;
            explicit AggregationTask(const std::string& nodeId);
            ~AggregationTask() override = default;

            bool Execute(const std::shared_ptr<CommonTaskData>& sharedData) override;

        private:
            // 声纹库单例：全局唯一，避免每次构造 AggregationTask 时反复 InitDevice，
            // 并保证流式场景下 SessionSpeakerManager 引用的数据库生命周期稳定
            static BmcvFaissDatabase& GetVoiceprintDatabase();
            static std::mutex& GetDatabaseMutex();
            // 计算声纹库内容指纹，用于增量更新判断；返回 lastFingerprint 引用以便缓存
            static size_t& GetLastDbFingerprint();

            // 修改最终的结果
            void ModifyAggregationResult(qifeng::aas::AasResult& asrRecords, std::shared_ptr<AasMeeting>& meeting);
            std::pair<int, int> FindTail(const std::string& text, int maxChars = 200);
            std::string Utf8Tail(const std::string& s, int maxChars);
            int Utf8CharCount(const std::string& s);
            struct Param {
                std::string str;
                std::pair<int, int> res;
            };
            void GenerateSegemnt(qifeng::aas::AasResult& asrRecords, const Param& param,
                                 qifeng::aas::AasSegment& result, std::shared_ptr<AasMeeting>& meeting);
            void FinalizeFullSegment(qifeng::aas::AasResult& asrRecords, qifeng::aas::AasSegment& result,
                                     std::string str, std::shared_ptr<AasMeeting>& meeting);

            // 以下是为了实现AggregationTask的Execute方法而定义的辅助函数
            bool InitDatabase(const std::shared_ptr<CommonTaskData>& sharedData);
            int SpeakerVerify(std::vector<SVSpeechAndResultData>& svRecords, const std::string& sessionId);
            void SpeakerVerifyStreaming(std::vector<SVSpeechAndResultData>& svRecords, const std::string& sessionId);
            void ResolveSpeakerLabels(std::vector<SVSpeechAndResultData>& svRecords);
            std::set<int> CollectSpeakerIds(const std::vector<SVSpeechAndResultData>& svRecords);
            std::map<int, std::string> CollectExistingLabels(const std::vector<SVSpeechAndResultData>& svRecords,
                                                             const std::set<int>& allIds);
            std::map<int, std::vector<float>>
            ComputeSpeakerCentroidsById(const std::vector<SVSpeechAndResultData>& svRecords);
            void MatchSpeakersFromDatabase(const std::map<int, std::vector<float>>& idToCentroid,
                                           std::map<int, std::string>& idToVpId);
            void AssignStrangerLabels(const std::set<int>& allIds, std::map<int, std::string>& idToVpId);
            void ApplyLabelsToRecords(std::vector<SVSpeechAndResultData>& svRecords,
                                      const std::map<int, std::string>& idToVpId);
            void OverlapVoting(std::vector<SVSpeechAndResultData>& svRecords);
            void MergeShortSegments(std::vector<SVSpeechAndResultData>& svRecords);
            bool TryMergeOneShortSegment(std::vector<SVSpeechAndResultData>& svRecords);
            std::vector<SVSpeechAndResultData>
            SpeakerVerifyOffline(const std::vector<SVSpeechAndResultData>& svRecords);
            std::vector<int> SpectralCluster(const std::vector<std::vector<float>>& features);
            std::vector<std::vector<float>> PPruning(const std::vector<std::vector<float>>& simMat);
            std::vector<std::vector<float>> Symmetrize(const std::vector<std::vector<float>>& A);
            std::vector<std::vector<float>> GetLaplacian(const std::vector<std::vector<float>>& M);
            std::pair<std::vector<std::vector<float>>, int>
            GetSpectralEmbeddings(const std::vector<std::vector<float>>& L);
            std::vector<float> GetEigenGaps(const std::vector<float>& eigVals);
            std::vector<int> KMeans(const std::vector<std::vector<float>>& data, int k);
            std::pair<std::vector<int>, float> KMeansSingleRun(const std::vector<std::vector<float>>& data, int k);
            std::vector<std::vector<float>> KMeansInitPlusPlus(const std::vector<std::vector<float>>& data, int k);
            std::vector<int> CorrectLabels(const std::vector<int>& labels);
            std::vector<int> UmapHdbscanCluster(const std::vector<std::vector<float>>& features);
            std::vector<int> MergeByCosineSimilarity(const std::vector<int>& labels,
                                                     const std::vector<std::vector<float>>& features, float cosThr);
            Eigen::MatrixXf ConvertToEigenMatrix(const std::vector<std::vector<float>>& mat);
            bool ComputeEigenDecomposition(const Eigen::MatrixXf& matrix, Eigen::VectorXf& outEigenvalues,
                                           Eigen::MatrixXf& outEigenvectors);
            std::vector<float> ExtractTopEigenvalues(const Eigen::VectorXf& eigenvalues, size_t k);
            int DetermineClusterCount(const std::vector<float>& eigVals, size_t numPoints);
            std::vector<std::vector<float>> ExtractEmbeddings(const Eigen::MatrixXf& eigenvectors, int numClusters);
            int FindNearestCluster(const std::vector<float>& point, const std::vector<std::vector<float>>& centroids);
            bool AssignClusters(const std::vector<std::vector<float>>& data,
                                const std::vector<std::vector<float>>& centroids, std::vector<int>& labels);
            void UpdateCentroids(const std::vector<std::vector<float>>& data, const std::vector<int>& labels, int k,
                                 std::vector<std::vector<float>>& centroids);
            float ComputeInertia(const std::vector<std::vector<float>>& data, const std::vector<int>& labels,
                                 const std::vector<std::vector<float>>& centroids);
            float CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);
            std::vector<std::vector<float>> BuildSimilarityMatrix(const std::vector<std::vector<float>>& features);
            void LogSimilarityStats(const std::vector<std::vector<float>>& simMat);
            std::vector<float> NormalizeFeaturesL2(const std::vector<std::vector<float>>& features);
            std::vector<std::vector<float>> RunUmapReduction(const std::vector<float>& normalizedData, size_t dim,
                                                             size_t n, size_t nComponents);
            std::vector<std::vector<double>>
            ComputeCosineDistanceMatrix(const std::vector<std::vector<float>>& features);
            std::vector<int> RunHdbscanWithDistanceMatrix(const std::vector<std::vector<double>>& distanceMat,
                                                          size_t n);
            void AccumulateFeature(std::vector<std::vector<float>>& centers, std::vector<int>& counts, size_t label,
                                   const std::vector<float>& feature);
            void AverageCenters(std::vector<std::vector<float>>& centers, const std::vector<int>& counts);
            std::pair<std::vector<std::vector<float>>, std::vector<int>>
            ComputeSpeakerCenters(const std::vector<int>& labels, const std::vector<std::vector<float>>& features,
                                  size_t spkNum);
            void NormalizeVectorL2(std::vector<float>& vec);
            void NormalizeCentersL2(std::vector<std::vector<float>>& centers, const std::vector<int>& counts);
            std::pair<int, int> FindMostSimilarPair(const std::vector<std::vector<float>>& normCenters,
                                                    const std::vector<int>& counts, float& outMaxAffinity);
            void MergeLabels(std::vector<int>& labels, const std::pair<int, int>& mergePair);
            std::vector<std::vector<float>> ExtractEmbeddings(const std::vector<SVSpeechAndResultData>& svRecords);
            void LogPairwiseSimilarity(const std::vector<std::vector<float>>& features);
            std::vector<int> ClusterFeatures(const std::vector<std::vector<float>>& features);
            int CountClusters(const std::vector<int>& labels);
            struct RttmEntry {
                float start;
                float end;
                int clusterId;
            };
            std::vector<RttmEntry> BuildRttmSegments(const std::vector<SVSpeechAndResultData>& svRecords,
                                                     const std::vector<int>& labels);
            std::vector<RttmEntry> MergeAdjacentSegments(const std::vector<RttmEntry>& segList);
            size_t FindFeatureDimension(const std::vector<std::vector<float>>& features);
            void AccumulateClusterFeatures(std::vector<std::vector<float>>& centroids, std::vector<int>& counts,
                                           const std::vector<std::vector<float>>& features,
                                           const std::vector<int>& labels);
            void AverageClusterCentroids(std::vector<std::vector<float>>& centroids, const std::vector<int>& counts);
            std::pair<std::vector<std::vector<float>>, std::vector<int>>
            ComputeClusterCentroids(const std::vector<std::vector<float>>& features, const std::vector<int>& labels,
                                    int numClusters);
            float CosineSimilarity2(const std::vector<float>& a, const std::vector<float>& b);
            std::map<int, std::string> MatchKnownSpeakers(const std::vector<std::vector<float>>& centroids,
                                                          const std::vector<int>& counts, int numClusters);
            std::optional<std::pair<int, std::string>>
            TryMatchSingleCluster(const std::vector<float>& centroid, int cid, const std::vector<float>& neighborVec);
            std::vector<SVSpeechAndResultData> BuildResultFromSegments(const std::vector<RttmEntry>& merged,
                                                                       int numClusters,
                                                                       const std::map<int, std::string>& clusterToVpId);
            void LogRttmRaw(const std::vector<SVSpeechAndResultData>& svRecords, const std::vector<int>& labels);
            void LogRttmMerged(const std::vector<RttmEntry>& merged);
            void LogRttmFinal(const std::vector<SVSpeechAndResultData>& result);
            void LogClusterInfo(int numClusters, const std::vector<int>& clusterCounts);

        private:
            SessionSpeakerManager::Config mSessionConfig;
            int mMergeShortSegmentMaxMs = 500;  // MergeShortSegments 仅合并时长小于此阈值(ms)的中间段
            float mEigengapSignificanceRatio = 0.7F;
            int mMinSpeakers = 2;
            int mMaxSpeakers = 20;
            float mMergeCosThreshold = 0.7F;
        };

    }  // namespace aas
}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_AAS_AAS_TASKS_H
