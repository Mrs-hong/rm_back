/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <algorithm>
#include <limits>

#include "aas/session_speaker_manager.h"
#include "common/config_manager.h"

namespace qifeng {
    namespace aas {

        SessionSpeakerManager::SessionSpeakerManager(const std::string& sessionId, const Config& config)
            : mSessionId(sessionId), mConfig(config), mMaxProcessedEndMs(0), mStrangerCounter(0), mNextGlobalId(0) {
            mConfig.knownThreshold = static_cast<float>(
                ConfigManager::GetInstance().GetDouble("sv", "known_threshold", mConfig.knownThreshold));
            mConfig.tauActive =
                static_cast<float>(ConfigManager::GetInstance().GetDouble("sv", "tau_active", mConfig.tauActive));
            mConfig.rhoUpdate =
                static_cast<float>(ConfigManager::GetInstance().GetDouble("sv", "rho_update", mConfig.rhoUpdate));
            mConfig.deltaNew =
                static_cast<float>(ConfigManager::GetInstance().GetDouble("sv", "delta_new", mConfig.deltaNew));
            mConfig.maxTempSpeakers =
                ConfigManager::GetInstance().GetInt("sv", "max_temp_speakers", mConfig.maxTempSpeakers);
            mConfig.orphanAgeThresholdMs =
                ConfigManager::GetInstance().GetInt("sv", "orphan_age_threshold_ms", mConfig.orphanAgeThresholdMs);
            mConfig.minHitCount = ConfigManager::GetInstance().GetInt("sv", "min_hit_count", mConfig.minHitCount);
            mConfig.promotionThreshold =
                ConfigManager::GetInstance().GetInt("sv", "promotion_threshold", mConfig.promotionThreshold);
            mConfig.fixedUpdateRatio = static_cast<float>(
                ConfigManager::GetInstance().GetDouble("sv", "fixed_update_ratio", mConfig.fixedUpdateRatio));
            mConfig.maxUnknownSpeakers =
                ConfigManager::GetInstance().GetInt("sv", "max_unknown_speakers", mConfig.maxUnknownSpeakers);

            SLOG_DEBUG << "SessionSpeakerManager created: sessionId=" << sessionId
                       << ", knownThreshold=" << mConfig.knownThreshold << ", tauActive=" << mConfig.tauActive
                       << ", rhoUpdate=" << mConfig.rhoUpdate << ", deltaNew=" << mConfig.deltaNew
                       << ", maxTempSpeakers=" << mConfig.maxTempSpeakers
                       << ", orphanAgeMs=" << mConfig.orphanAgeThresholdMs << ", minHitCount=" << mConfig.minHitCount
                       << ", promotionThreshold=" << mConfig.promotionThreshold
                       << ", fixedUpdateRatio=" << mConfig.fixedUpdateRatio
                       << ", maxUnknownSpeakers=" << mConfig.maxUnknownSpeakers;
        }

        float SessionSpeakerManager::ComputeCosineSimilarity(const std::vector<float>& a,
                                                             const std::vector<float>& b) const {
            if (a.size() != b.size() || a.empty()) {
                return 0.0F;
            }
            float dot = 0.0F;
            float normA = 0.0F;
            float normB = 0.0F;
            for (size_t i = 0; i < a.size(); ++i) {
                dot += a[i] * b[i];
                normA += a[i] * a[i];
                normB += b[i] * b[i];
            }
            float denom = std::sqrt(normA) * std::sqrt(normB);
            return (denom > 1e-8F) ? dot / denom : 0.0F;
        }

        float SessionSpeakerManager::ComputeCosineDistance(const std::vector<float>& a,
                                                           const std::vector<float>& b) const {
            return 1.0F - ComputeCosineSimilarity(a, b);
        }

        std::vector<float>
        SessionSpeakerManager::HammingWeightedAverage(const std::vector<std::vector<float>>& embeddings) const {
            if (embeddings.empty()) {
                return {};
            }
            size_t dim = embeddings[0].size();
            size_t n = embeddings.size();
            std::vector<float> weights(n);
            float totalWeight = 0.0F;

            for (size_t i = 0; i < n; ++i) {
                float t = 2.0F * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(n);
                weights[i] = 0.54F - 0.46F * std::cos(t);
                totalWeight += weights[i];
            }

            std::vector<float> result(dim, 0.0F);
            for (size_t i = 0; i < n; ++i) {
                float w = weights[i] / totalWeight;
                for (size_t d = 0; d < dim; ++d) {
                    result[d] += w * embeddings[i][d];
                }
            }
            return result;
        }

        void SessionSpeakerManager::UpdateCenter(TempSpeakerEntry& entry, const std::vector<float>& embedding,
                                                 float activation) {
            if (entry.center.size() != embedding.size()) {
                return;
            }
            float totalWeight = entry.totalActivation;
            for (size_t d = 0; d < entry.center.size(); ++d) {
                entry.center[d] =
                    (entry.center[d] * totalWeight + embedding[d] * activation) / (totalWeight + activation);
            }
            entry.totalActivation += activation;
            entry.hitCount++;
        }

        std::pair<int, int> SessionSpeakerManager::AddCenter(const std::vector<float>& embedding, int64_t timestamp,
                                                             float activation) {
            int id = GenerateNewId();
            int globalId = GenerateNewGlobalId();
            mTempSpeakers.emplace_back(embedding, id, globalId, timestamp, activation);
            mActiveCenters.insert(id);
            return {id, globalId};
        }

        int SessionSpeakerManager::GetNextCenterPosition() const {
            for (int i = 0; i < mConfig.maxTempSpeakers; ++i) {
                if (mActiveCenters.find(i) == mActiveCenters.end() &&
                    mBlockedCenters.find(i) == mBlockedCenters.end()) {
                    return i;
                }
            }
            return -1;
        }

        int SessionSpeakerManager::GetFreeCenterCount() const {
            int count = 0;
            for (int i = 0; i < mConfig.maxTempSpeakers; ++i) {
                if (mActiveCenters.find(i) == mActiveCenters.end() &&
                    mBlockedCenters.find(i) == mBlockedCenters.end()) {
                    ++count;
                }
            }
            return count;
        }

        void SessionSpeakerManager::AddToBuffer(int speakerId, const BufferEntry& bufferEntry) {
            std::lock_guard<std::mutex> lock(mMutex);
            for (auto& entry : mTempSpeakers) {
                if (entry.id == speakerId) {
                    entry.buffer.push_back(bufferEntry);
                    const size_t maxBufferSize = 5;
                    while (entry.buffer.size() > maxBufferSize) {
                        entry.buffer.erase(entry.buffer.begin());
                    }
                    SLOG_DEBUG << "SessionSpeakerManager: AddToBuffer id=" << speakerId
                               << ", bufferSize=" << entry.buffer.size();
                    return;
                }
            }
        }

        std::vector<float> SessionSpeakerManager::AggregateBuffer(int speakerId) const {
            std::lock_guard<std::mutex> lock(mMutex);
            for (const auto& entry : mTempSpeakers) {
                if (entry.id == speakerId && !entry.buffer.empty()) {
                    std::vector<std::vector<float>> embeddings;
                    for (const auto& buff : entry.buffer) {
                        embeddings.push_back(buff.embedding);
                    }
                    SLOG_DEBUG << "SessionSpeakerManager: AggregateBuffer id=" << speakerId
                               << ", bufferSize=" << embeddings.size();
                    return HammingWeightedAverage(embeddings);
                }
            }
            return {};
        }

        int SessionSpeakerManager::GenerateNewGlobalId() {
            return mNextGlobalId++;
        }

        int SessionSpeakerManager::GenerateNewId() {
            return mStrangerCounter++;
        }

        int SessionSpeakerManager::GetActiveSpeakerCount() const {
            return static_cast<int>(mActiveCenters.size());
        }

        int SessionSpeakerManager::GetStrangerCount() const {
            int count = 0;
            for (const auto& entry : mTempSpeakers) {
                if (!IsKnownSpeaker(entry.id) && entry.isActive) {
                    ++count;
                }
            }
            return count;
        }

        bool SessionSpeakerManager::CanCreateNewSpeaker() const {
            if (GetFreeCenterCount() <= 0) {
                return false;
            }
            // 陌生人数量达到上限时不再创建新陌生人，强制 fallback 匹配已有说话人
            if (GetStrangerCount() >= mConfig.maxUnknownSpeakers) {
                return false;
            }
            return true;
        }

        int SessionSpeakerManager::FindClosestActiveSpeaker(const std::vector<float>& embedding) const {
            float bestDist = std::numeric_limits<float>::max();
            int bestId = -1;
            for (const auto& entry : mTempSpeakers) {
                if (!entry.isActive || entry.isBlocked) {
                    continue;
                }
                if (entry.center.size() != embedding.size()) {
                    continue;
                }
                float dist = ComputeCosineDistance(embedding, entry.center);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestId = entry.id;
                }
            }
            return bestId;
        }

        bool SessionSpeakerManager::IsKnownSpeaker(int speakerId) const {
            for (const auto& [vpId, internalId] : mKnownVpIdToId) {
                if (internalId == speakerId) {
                    return true;
                }
            }
            return false;
        }

        bool SessionSpeakerManager::IsOrphan(const TempSpeakerEntry& entry, int currentTimestamp) const {
            int64_t age = currentTimestamp - entry.timestamp;
            return age > mConfig.orphanAgeThresholdMs && entry.hitCount < mConfig.minHitCount &&
                   !IsKnownSpeaker(entry.id);
        }

        void SessionSpeakerManager::CleanupOrphans(int currentTimestamp) {
            std::lock_guard<std::mutex> lock(mMutex);

            size_t beforeSize = mTempSpeakers.size();
            auto it = mTempSpeakers.begin();
            while (it != mTempSpeakers.end()) {
                if (IsOrphan(*it, currentTimestamp)) {
                    mActiveCenters.erase(it->id);
                    mBlockedCenters.erase(it->id);
                    SLOG_DEBUG << "SessionSpeakerManager: Cleanup orphan id=" << it->id << ", globalId=" << it->globalId
                               << ", age=" << (currentTimestamp - it->timestamp) << "ms";
                    it = mTempSpeakers.erase(it);
                    continue;
                }
                ++it;
            }

            if (mTempSpeakers.size() != beforeSize) {
                SLOG_DEBUG << "SessionSpeakerManager: Cleaned " << (beforeSize - mTempSpeakers.size())
                           << " orphans, remaining=" << mTempSpeakers.size();
            }
        }

        void SessionSpeakerManager::Finalize() {
            std::lock_guard<std::mutex> lock(mMutex);
            mTempSpeakers.clear();
            mActiveCenters.clear();
            mBlockedCenters.clear();
            mStrangerCounter = 0;
            SLOG_DEBUG << "SessionSpeakerManager: Finalized session " << mSessionId;
        }

        int SessionSpeakerManager::GetTempSpeakerCount() const {
            std::lock_guard<std::mutex> lock(mMutex);
            return static_cast<int>(mTempSpeakers.size());
        }

        int64_t SessionSpeakerManager::GetMaxProcessedEndMs() const {
            std::lock_guard<std::mutex> lock(mMutex);
            return mMaxProcessedEndMs;
        }

        std::string SessionSpeakerManager::GetVpIdForSpeaker(int speakerId) const {
            for (const auto& [vpId, internalId] : mKnownVpIdToId) {
                if (internalId == speakerId) {
                    return vpId;
                }
            }
            return "";
        }

        std::map<int, std::string> SessionSpeakerManager::GetSpeakerLabels() const {
            std::lock_guard<std::mutex> lock(mMutex);
            std::map<int, std::string> labels;
            int strangerCounter = 1;
            for (const auto& entry : mTempSpeakers) {
                if (labels.find(entry.id) != labels.end()) {
                    continue;
                }
                std::string vpId = GetVpIdForSpeaker(entry.id);
                if (!vpId.empty()) {
                    labels[entry.id] = vpId;
                } else {
                    labels[entry.id] = "陌生人_" + std::to_string(strangerCounter++);
                }
            }
            return labels;
        }

        int SessionSpeakerManager::HandleInactiveSpeaker(const std::vector<float>& embedding, float activation) {
            SLOG_DEBUG << "SessionSpeakerManager: SKIP activation=" << activation
                       << " < tauActive=" << mConfig.tauActive;
            if (!mTempSpeakers.empty()) {
                int closestId = FindClosestActiveSpeaker(embedding);
                if (closestId >= 0) {
                    return closestId;
                }
            }
            return 0;
        }

        void SessionSpeakerManager::UpdateKnownCenter(const std::string& vpId, const TempSpeakerEntry& entry,
                                                      const std::vector<float>& embedding, float activation) {
            auto it = mKnownSpeakerCenters.find(vpId);
            if (it == mKnownSpeakerCenters.end()) {
                return;
            }
            auto& knownCenter = it->second;
            float totalW = entry.totalActivation;
            for (size_t d = 0; d < knownCenter.size(); ++d) {
                knownCenter[d] = (knownCenter[d] * totalW + embedding[d] * activation) / (totalW + activation);
            }
        }

        void SessionSpeakerManager::UpdateKnownSpeakerEntry(int knownId, const std::vector<float>& embedding,
                                                            int64_t timestamp, float activation) {
            for (auto& entry : mTempSpeakers) {
                if (entry.id != knownId) {
                    continue;
                }
                if (activation >= mConfig.rhoUpdate) {
                    UpdateCenter(entry, embedding, activation);
                    std::string vpId = GetVpIdForSpeaker(knownId);
                    if (!vpId.empty()) {
                        UpdateKnownCenter(vpId, entry, embedding, activation);
                    }
                }
                entry.timestamp = timestamp;
                break;
            }
        }

        int SessionSpeakerManager::MatchKnownCentroid(const std::vector<float>& embedding, int64_t timestamp,
                                                      float activation, bool isLongEnough) {
            if (mKnownSpeakerCenters.empty()) {
                return -1;
            }

            float bestSim = -1.0F;
            std::string bestVpId;
            for (const auto& [vpId, center] : mKnownSpeakerCenters) {
                if (center.size() != embedding.size()) {
                    continue;
                }
                float sim = ComputeCosineSimilarity(embedding, center);
                if (sim > bestSim) {
                    bestSim = sim;
                    bestVpId = vpId;
                }
            }

            if (bestSim <= mConfig.knownThreshold || bestVpId.empty()) {
                return -1;
            }

            auto it = mKnownVpIdToId.find(bestVpId);
            int knownId = -1;

            if (it != mKnownVpIdToId.end()) {
                knownId = it->second;
                UpdateKnownSpeakerEntry(knownId, embedding, timestamp, activation);
            } else {
                auto [newId, newGlobalId] = AddCenter(embedding, timestamp, activation);
                knownId = newId;
                mKnownVpIdToId[bestVpId] = knownId;
                mKnownVpIdToGlobalId[bestVpId] = newGlobalId;
            }

            SLOG_DEBUG << "SessionSpeakerManager: KNOWN_CENTROID matched vpId=" << bestVpId << ", id=" << knownId
                       << ", cosSim=" << bestSim << ", isLongEnough=" << isLongEnough;
            return knownId;
        }

        int SessionSpeakerManager::MatchKnownDatabase(const std::vector<float>& embedding, int64_t timestamp,
                                                      float activation) {
            if (mKnownDb->IsEmpty()) {
                return -1;
            }

            std::vector<std::vector<float>> query = {embedding};
            int64_t vectorId = -1;
            std::vector<float> neighborVec;
            float cosSim = 0.0F;

            {
                std::lock_guard<std::mutex> dbLock(*mDbMutex);
                auto [batchIndices, batchDistances] = mKnownDb->Search(query, 1);

                if (batchIndices.empty() || batchIndices[0].empty() || batchIndices[0][0] < 0) {
                    return -1;
                }

                vectorId = static_cast<int64_t>(batchIndices[0][0]);
                neighborVec = mKnownDb->Reconstruct(vectorId);

                if (neighborVec.size() != embedding.size()) {
                    return -1;
                }

                cosSim = ComputeCosineSimilarity(embedding, neighborVec);
                if (cosSim <= mConfig.knownThreshold) {
                    return -1;
                }
            }

            std::string knownVpId = mKnownDb->GetVpId(vectorId);
            auto it = mKnownVpIdToId.find(knownVpId);
            int knownId = -1;

            if (it != mKnownVpIdToId.end()) {
                knownId = it->second;
                UpdateKnownSpeakerEntry(knownId, embedding, timestamp, activation);
            } else {
                auto [newId, newGlobalId] = AddCenter(neighborVec, timestamp, activation);
                knownId = newId;
                mKnownVpIdToId[knownVpId] = knownId;
                mKnownVpIdToGlobalId[knownVpId] = newGlobalId;
                mKnownSpeakerCenters[knownVpId] = neighborVec;
            }

            bool isLongEnough = (activation >= mConfig.rhoUpdate);
            SLOG_DEBUG << "SessionSpeakerManager: KNOWN_DB matched vpId=" << knownVpId << ", id=" << knownId
                       << ", cosSim=" << cosSim << ", isLongEnough=" << isLongEnough;
            return knownId;
        }

        int SessionSpeakerManager::FindBestMatch(const std::vector<float>& embedding) const {
            struct DistEntry {
                int id;
                float distance;
                int idx;
                bool isFixed;
            };

            std::vector<DistEntry> distMap;
            for (int i = 0; i < static_cast<int>(mTempSpeakers.size()); ++i) {
                const auto& entry = mTempSpeakers[static_cast<size_t>(i)];
                if (!entry.isActive || entry.isBlocked) {
                    continue;
                }
                if (entry.center.size() != embedding.size()) {
                    continue;
                }
                float dist = ComputeCosineDistance(embedding, entry.center);
                distMap.push_back({entry.id, dist, i, entry.isFixed});
            }

            // 优先在 deltaNew 范围内寻找匹配，固定说话人优先
            std::vector<DistEntry> validMap;
            for (const auto& de : distMap) {
                if (de.distance < mConfig.deltaNew) {
                    validMap.push_back(de);
                }
            }

            if (!validMap.empty()) {
                std::sort(validMap.begin(), validMap.end(),
                          [](const DistEntry& a, const DistEntry& b) { return a.distance < b.distance; });
                return validMap[0].idx;
            }

            if (!distMap.empty()) {
                std::sort(distMap.begin(), distMap.end(),
                          [](const DistEntry& a, const DistEntry& b) { return a.distance < b.distance; });
                return distMap[0].idx;
            }

            return -1;
        }

        int SessionSpeakerManager::HandleNoMatch(const std::vector<float>& embedding, int64_t timestamp,
                                                 float activation, bool isLongEnough) {
            if (CanCreateNewSpeaker() && isLongEnough) {
                auto [newId, newGlobalId] = AddCenter(embedding, timestamp, activation);
                SLOG_DEBUG << "SessionSpeakerManager: NEW_CENTER id=" << newId << ", globalId=" << newGlobalId
                           << ", activation=" << activation << ", isLongEnough=true"
                           << ", freeCenters=" << GetFreeCenterCount() << ", totalTemp=" << mTempSpeakers.size();
                return newId;
            }

            int closestIdx = FindBestMatch(embedding);
            if (closestIdx >= 0) {
                auto& fallbackEntry = mTempSpeakers[static_cast<size_t>(closestIdx)];
                if (fallbackEntry.isFixed) {
                    // 固定说话人 fallback：使用极低权重慢更新
                    if (isLongEnough) {
                        float slowActivation = activation * mConfig.fixedUpdateRatio * 0.5F;
                        UpdateCenter(fallbackEntry, embedding, slowActivation);
                    }
                } else if (isLongEnough) {
                    UpdateCenter(fallbackEntry, embedding, activation);
                }
                fallbackEntry.timestamp = timestamp;
                int fallbackId = fallbackEntry.id;
                SLOG_DEBUG << "SessionSpeakerManager: FALLBACK id=" << fallbackId << ", isLongEnough=" << isLongEnough
                           << ", isFixed=" << fallbackEntry.isFixed << ", hits=" << fallbackEntry.hitCount
                           << ", reason=" << (CanCreateNewSpeaker() ? "not_long" : "no_space");
                return fallbackId;
            }

            auto [newId, newGlobalId] = AddCenter(embedding, timestamp, activation);
            SLOG_DEBUG << "SessionSpeakerManager: FIRST_CENTER id=" << newId << ", globalId=" << newGlobalId;
            return newId;
        }

        int SessionSpeakerManager::IdentifyInt(const std::vector<float>& embedding, int64_t timestamp,
                                               float activation) {
            std::lock_guard<std::mutex> lock(mMutex);

            if (embedding.empty()) {
                SLOG_WARN << "SessionSpeakerManager::IdentifyInt empty embedding";
                return 0;
            }

            if (activation < mConfig.tauActive) {
                return HandleInactiveSpeaker(embedding, activation);
            }

            bool isLongEnough = (activation >= mConfig.rhoUpdate);

            int knownId = MatchKnownCentroid(embedding, timestamp, activation, isLongEnough);
            if (knownId >= 0) {
                return knownId;
            }

            knownId = MatchKnownDatabase(embedding, timestamp, activation);
            if (knownId >= 0) {
                return knownId;
            }

            int matchedIdx = FindBestMatch(embedding);
            if (matchedIdx < 0) {
                return HandleNoMatch(embedding, timestamp, activation, isLongEnough);
            }

            auto& matchedEntry = mTempSpeakers[static_cast<size_t>(matchedIdx)];
            float matchedDist = ComputeCosineDistance(embedding, matchedEntry.center);
            if (matchedDist >= mConfig.deltaNew) {
                return HandleNoMatch(embedding, timestamp, activation, isLongEnough);
            }

            // 固定说话人：使用慢更新系数调整聚类中心，防止漂移但保持适应性
            if (matchedEntry.isFixed) {
                matchedEntry.timestamp = timestamp;
                matchedEntry.hitCount++;
                if (isLongEnough) {
                    // 慢更新：使用 fixedUpdateRatio 衰减激活权重
                    float slowActivation = activation * mConfig.fixedUpdateRatio;
                    UpdateCenter(matchedEntry, embedding, slowActivation);
                } else {
                    // 即使 isLongEnough=false，也以极低权重微调
                    float microActivation = activation * mConfig.fixedUpdateRatio * 0.3F;
                    UpdateCenter(matchedEntry, embedding, microActivation);
                }
                int matchedId = matchedEntry.id;
                SLOG_DEBUG << "SessionSpeakerManager: MATCHED_FIXED id=" << matchedId << ", dist=" << matchedDist
                           << ", sim=" << (1.0F - matchedDist) << ", hits=" << matchedEntry.hitCount
                           << ", updated=" << isLongEnough;
                return matchedId;
            }

            // 临时说话人：更新聚类中心，检查是否达到提升阈值
            if (isLongEnough) {
                UpdateCenter(matchedEntry, embedding, activation);
            }
            matchedEntry.timestamp = timestamp;

            // 检查是否达到提升阈值
            bool promoted = false;
            if (matchedEntry.hitCount >= mConfig.promotionThreshold) {
                matchedEntry.isFixed = true;
                promoted = true;
                SLOG_DEBUG << "SessionSpeakerManager: PROMOTED id=" << matchedEntry.id
                           << " to FIXED, hitCount=" << matchedEntry.hitCount << ", sim=" << (1.0F - matchedDist);
            }

            int matchedId = matchedEntry.id;
            SLOG_DEBUG << "SessionSpeakerManager: MATCHED id=" << matchedId << ", dist=" << matchedDist
                       << ", sim=" << (1.0F - matchedDist) << ", hits=" << matchedEntry.hitCount
                       << ", isFixed=" << matchedEntry.isFixed << ", isLongEnough=" << isLongEnough
                       << ", centroidUpdated=" << (isLongEnough && !matchedEntry.isFixed) << ", promoted=" << promoted;
            return matchedId;
        }

        int SessionSpeakerManager::MatchExisting(const std::vector<float>& embedding) const {
            std::lock_guard<std::mutex> lock(mMutex);

            if (embedding.empty() || mTempSpeakers.empty()) {
                return -1;
            }

            float bestDist = std::numeric_limits<float>::max();
            int bestId = -1;

            for (const auto& entry : mTempSpeakers) {
                if (!entry.isActive || entry.isBlocked) {
                    continue;
                }
                if (entry.center.size() != embedding.size()) {
                    continue;
                }
                float dist = ComputeCosineDistance(embedding, entry.center);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestId = entry.id;
                }
            }

            if (bestDist < mConfig.deltaNew) {
                SLOG_DEBUG << "SessionSpeakerManager: MatchExisting id=" << bestId << ", dist=" << bestDist
                           << ", sim=" << (1.0F - bestDist);
                return bestId;
            }

            return -1;
        }

        void SessionSpeakerManager::CacheWindowLabel(int64_t start, int64_t end, int speakerId) {
            std::lock_guard<std::mutex> lock(mMutex);
            mProcessedWindows[{start, end}] = speakerId;
            if (end > mMaxProcessedEndMs) {
                mMaxProcessedEndMs = end;
            }
            // 滑动清理：仅保留最近 maxCachedWindows 条记录，防止长会话 OOM
            constexpr size_t maxCachedWindows = 256;
            if (mProcessedWindows.size() > maxCachedWindows) {
                // mProcessedWindows 按 pair<start,end> 排序，旧的在头部
                size_t toRemove = mProcessedWindows.size() - maxCachedWindows;
                auto it = mProcessedWindows.begin();
                for (size_t i = 0; i < toRemove; ++i) {
                    it = mProcessedWindows.erase(it);
                }
                SLOG_DEBUG << "SessionSpeakerManager: pruned " << toRemove
                           << " old cached windows, remaining=" << mProcessedWindows.size();
            }
            SLOG_DEBUG << "SessionSpeakerManager: cached window [" << start << "," << end
                       << "] -> speakerId=" << speakerId << ", totalCached=" << mProcessedWindows.size();
        }

        int SessionSpeakerManager::GetCachedWindowLabel(int64_t start, int64_t end) const {
            std::lock_guard<std::mutex> lock(mMutex);
            auto it = mProcessedWindows.find({start, end});
            if (it != mProcessedWindows.end()) {
                return it->second;
            }
            return -1;
        }

        void SessionSpeakerManager::ClearCachedWindows() {
            std::lock_guard<std::mutex> lock(mMutex);
            mProcessedWindows.clear();
        }

    }  // namespace aas
}  // namespace qifeng
