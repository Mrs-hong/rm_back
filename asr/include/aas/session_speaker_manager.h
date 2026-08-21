/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMWORK_AAS_SESSION_SPEAKER_MANAGER_H
#define QIFENG_FRAMWORK_AAS_SESSION_SPEAKER_MANAGER_H

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bmcv_faiss_database.h"
#include "common/logger.h"

namespace qifeng {
    namespace aas {

        struct BufferEntry {
            std::vector<float> embedding;
            int64_t timestamp;
            int64_t start;
            int64_t end;
            float activation;

            BufferEntry(const std::vector<float>& emb, int64_t ts, int64_t s, int64_t e, float act = 1.0F)
                : embedding(emb), timestamp(ts), start(s), end(e), activation(act) {
            }
        };

        struct TempSpeakerEntry {
            std::vector<float> center;
            int id;
            int globalId;
            int64_t timestamp;
            int hitCount;
            float totalActivation;
            bool isActive;
            bool isBlocked;
            bool isFixed;  // 是否已提升为固定说话人，固定后不再更新聚类中心
            std::vector<BufferEntry> buffer;

            TempSpeakerEntry(const std::vector<float>& emb, int sid, int gid, int ts, float act = 1.0F)
                : center(emb), id(sid), globalId(gid), timestamp(ts), hitCount(1), totalActivation(act), isActive(true),
                  isBlocked(false), isFixed(false) {
            }
        };

        class SessionSpeakerManager {
        public:
            struct Config {
                float knownThreshold;
                float tauActive;
                float rhoUpdate;
                float deltaNew;
                int maxTempSpeakers;  // 最大临时说话人总数（含已知+未知）
                int orphanAgeThresholdMs;
                int minHitCount;
                int promotionThreshold;   // 临时说话人观察次数阈值，达到后提升为固定说话人
                float fixedUpdateRatio;    // 固定说话人中心慢更新系数（0~1），默认 0.15
                int maxUnknownSpeakers;    // 陌生人最大数量，超过后不再创建新陌生人，默认 10

                Config()
                    : knownThreshold(0.43F), tauActive(0.5F), rhoUpdate(0.3F), deltaNew(0.6F), maxTempSpeakers(20),
                      orphanAgeThresholdMs(300000), minHitCount(1), promotionThreshold(15), fixedUpdateRatio(0.15F),
                      maxUnknownSpeakers(10) {
                }
            };

            explicit SessionSpeakerManager(const std::string& sessionId, const Config& config = Config());
            SessionSpeakerManager(const SessionSpeakerManager&) = delete;
            SessionSpeakerManager& operator=(const SessionSpeakerManager&) = delete;
            SessionSpeakerManager(SessionSpeakerManager&&) = delete;
            SessionSpeakerManager& operator=(SessionSpeakerManager&&) = delete;
            ~SessionSpeakerManager() = default;

            int IdentifyInt(const std::vector<float>& embedding, int64_t timestamp, float activation = 1.0F);

            void SetKnownDatabase(BmcvFaissDatabase& knownDb, std::mutex& dbMutex) {
                mKnownDb = &knownDb;
                mDbMutex = &dbMutex;
            }

            int MatchExisting(const std::vector<float>& embedding) const;

            void CleanupOrphans(int currentTimestamp);
            void Finalize();

            int GetTempSpeakerCount() const;
            int GetActiveSpeakerCount() const;
            int GetFreeCenterCount() const;
            const std::string& GetSessionId() const {
                return mSessionId;
            }
            int64_t GetMaxProcessedEndMs() const;

            std::map<int, std::string> GetSpeakerLabels() const;

            void AddToBuffer(int speakerId, const BufferEntry& bufferEntry);
            std::vector<float> AggregateBuffer(int speakerId) const;

            void CacheWindowLabel(int64_t start, int64_t end, int speakerId);
            int GetCachedWindowLabel(int64_t start, int64_t end) const;
            void ClearCachedWindows();

        private:
            float ComputeCosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) const;
            float ComputeCosineDistance(const std::vector<float>& a, const std::vector<float>& b) const;
            std::vector<float> HammingWeightedAverage(const std::vector<std::vector<float>>& embeddings) const;
            int GenerateNewGlobalId();
            int GenerateNewId();
            int GetStrangerCount() const;
            bool CanCreateNewSpeaker() const;
            int FindClosestActiveSpeaker(const std::vector<float>& embedding) const;
            int GetNextCenterPosition() const;
            void UpdateCenter(TempSpeakerEntry& entry, const std::vector<float>& embedding, float activation);
            std::pair<int, int> AddCenter(const std::vector<float>& embedding, int64_t timestamp, float activation);
            bool IsKnownSpeaker(int speakerId) const;
            std::string GetVpIdForSpeaker(int speakerId) const;
            bool IsOrphan(const TempSpeakerEntry& entry, int currentTimestamp) const;
            int HandleInactiveSpeaker(const std::vector<float>& embedding, float activation);
            void UpdateKnownSpeakerEntry(int knownId, const std::vector<float>& embedding, int64_t timestamp,
                                         float activation);
            void UpdateKnownCenter(const std::string& vpId, const TempSpeakerEntry& entry,
                                   const std::vector<float>& embedding, float activation);
            int MatchKnownCentroid(const std::vector<float>& embedding, int64_t timestamp, float activation,
                                   bool isLongEnough);
            int MatchKnownDatabase(const std::vector<float>& embedding, int64_t timestamp, float activation);
            int FindBestMatch(const std::vector<float>& embedding) const;
            int HandleNoMatch(const std::vector<float>& embedding, int64_t timestamp, float activation,
                              bool isLongEnough);

        private:
            std::string mSessionId;
            Config mConfig;

            std::vector<TempSpeakerEntry> mTempSpeakers;
            std::unordered_set<int> mActiveCenters;
            std::unordered_set<int> mBlockedCenters;

            std::unordered_map<std::string, int> mKnownVpIdToGlobalId;
            std::unordered_map<std::string, int> mKnownVpIdToId;
            std::unordered_map<std::string, std::vector<float>> mKnownSpeakerCenters;

            int64_t mMaxProcessedEndMs;
            std::map<std::pair<int, int>, int> mProcessedWindows;

            int mStrangerCounter;
            int mNextGlobalId;
            mutable std::mutex mMutex;
            std::mutex* mDbMutex = nullptr;
            BmcvFaissDatabase* mKnownDb = nullptr;
        };

        class SessionSpeakerManagerRegistry {
        public:
            static SessionSpeakerManagerRegistry& GetInstance() {
                static SessionSpeakerManagerRegistry Instance;
                return Instance;
            }

            std::shared_ptr<SessionSpeakerManager>
            GetOrCreate(const std::string& sessionId,
                        const SessionSpeakerManager::Config& config = SessionSpeakerManager::Config()) {
                std::lock_guard<std::mutex> lock(mRegistryMutex);

                auto it = mManagers.find(sessionId);
                if (it != mManagers.end()) {
                    return it->second;
                }

                auto manager = std::make_shared<SessionSpeakerManager>(sessionId, config);
                mManagers[sessionId] = manager;
                SLOG_INFO << "SessionSpeakerManagerRegistry: Created new session " << sessionId;
                return manager;
            }

            std::shared_ptr<SessionSpeakerManager> Get(const std::string& sessionId) {
                std::lock_guard<std::mutex> lock(mRegistryMutex);
                auto it = mManagers.find(sessionId);
                if (it != mManagers.end()) {
                    return it->second;
                }
                return nullptr;
            }

            void Remove(const std::string& sessionId) {
                std::lock_guard<std::mutex> lock(mRegistryMutex);
                auto it = mManagers.find(sessionId);
                if (it != mManagers.end()) {
                    it->second->Finalize();
                    mManagers.erase(it);
                    SLOG_INFO << "SessionSpeakerManagerRegistry: Removed session " << sessionId;
                }
            }

            void FinalizeAll() {
                std::lock_guard<std::mutex> lock(mRegistryMutex);
                for (auto& pair : mManagers) {
                    pair.second->Finalize();
                }
                mManagers.clear();
                SLOG_INFO << "SessionSpeakerManagerRegistry: All sessions finalized";
            }

            void CleanupAllOrphans(int currentTimestamp) {
                std::lock_guard<std::mutex> lock(mRegistryMutex);
                for (auto& pair : mManagers) {
                    pair.second->CleanupOrphans(currentTimestamp);
                }
            }

            size_t GetSessionCount() const {
                std::lock_guard<std::mutex> lock(mRegistryMutex);
                return mManagers.size();
            }

        private:
            SessionSpeakerManagerRegistry() = default;
            SessionSpeakerManagerRegistry(const SessionSpeakerManagerRegistry&) = delete;
            SessionSpeakerManagerRegistry& operator=(const SessionSpeakerManagerRegistry&) = delete;
            SessionSpeakerManagerRegistry(SessionSpeakerManagerRegistry&&) = delete;
            SessionSpeakerManagerRegistry& operator=(SessionSpeakerManagerRegistry&&) = delete;
            ~SessionSpeakerManagerRegistry() = default;

            std::unordered_map<std::string, std::shared_ptr<SessionSpeakerManager>> mManagers;
            mutable std::mutex mRegistryMutex;
        };

    }  // namespace aas
}  // namespace qifeng

#endif  // QIFENG_FRAMWORK_AAS_SESSION_SPEAKER_MANAGER_H
