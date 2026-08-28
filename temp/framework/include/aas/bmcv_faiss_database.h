/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#ifndef QIFENG_FRAMWORK_AAS_BMCV_FAISS_DATABASE_H
#define QIFENG_FRAMWORK_AAS_BMCV_FAISS_DATABASE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/logger.h"

namespace qifeng {
    namespace aas {

        /**
         * @brief 基于 CPU 暴力 L2 的声纹向量数据库封装（硬件无关）
         *
         * 语义等价于 FAISS IndexFlatL2：全量计算 L2 距离后排序取 top-k。
         * 不再依赖算能 BM 芯片的 bmcv/bmlib 硬件算子（bmcv_faiss_indexflatL2），
         * 可在后摩 M50 等任意平台上运行。声纹库规模通常较小（几十~几百条），
         * CPU 暴力搜索足以满足实时性。
         */
        class BmcvFaissDatabase {
        public:
            BmcvFaissDatabase() : mDim(0), mNtotal(0) {
            }
            BmcvFaissDatabase(const BmcvFaissDatabase&) = delete;
            BmcvFaissDatabase& operator=(const BmcvFaissDatabase&) = delete;
            BmcvFaissDatabase(BmcvFaissDatabase&&) = delete;
            BmcvFaissDatabase& operator=(BmcvFaissDatabase&&) = delete;

            ~BmcvFaissDatabase() {
                Reset();
            }

            /**
             * @brief 兼容旧接口：硬件无关实现无需初始化设备
             * @param devId 保留参数（设备ID，无实际作用）
             * @return 0 成功
             */
            int InitDevice(int devId = 0) {
                std::lock_guard<std::mutex> lock(mMutex);
                (void)devId;
                return 0;
            }

            /**
             * @brief 兼容旧接口：无硬件资源需要释放
             */
            void DestroyDevice() {
                std::lock_guard<std::mutex> lock(mMutex);
                // 无硬件资源，无需释放
            }

            /**
             * @brief 重置数据库，清除所有数据
             */
            void Reset() {
                std::lock_guard<std::mutex> lock(mMutex);
                mFeatures.clear();
                mVpIds.clear();
                mIndexToVpId.clear();
                mDbNorms2.clear();
                mDim = 0;
                mNtotal = 0;
            }

            /**
             * @brief 添加带 ID 的特征向量
             */
            void AddWithId(const std::vector<float>& feature, const std::string& vpId) {
                std::lock_guard<std::mutex> lock(mMutex);
                if (feature.empty()) {
                    SLOG_WARN << "BmcvFaissDatabase: ignore empty feature for vpId=" << vpId;
                    return;
                }
                if (mNtotal == 0) {
                    mDim = feature.size();
                } else if (feature.size() != mDim) {
                    SLOG_ERROR << "BmcvFaissDatabase: feature dim mismatch, expected=" << mDim
                               << ", got=" << std::to_string(feature.size()) << ", vpId=" + vpId;
                    return;
                }
                mFeatures.push_back(feature);
                mVpIds.push_back(vpId);
                mIndexToVpId[static_cast<int64_t>(mNtotal)] = vpId;
                ++mNtotal;

                // 增量缓存 L2 范数平方，供 Search 复用
                float norm2 = 0.0F;
                for (float v : feature) {
                    norm2 += v * v;
                }
                mDbNorms2.push_back(norm2);

                float norm = std::sqrt(norm2);
                SLOG_DEBUG << "BmcvFaissDatabase::addWithId vpId=" << vpId
                           << ", idx=" << static_cast<int64_t>(mNtotal - 1) << ", dim=" << feature.size()
                           << ", norm=" << norm << ", head=[" << feature[0] << "," << feature[1] << "," << feature[2]
                           << "]";
            }

            /**
             * @brief 搜索最近邻 (L2距离)，返回每个查询的 top-k 索引和 L2 距离
             * @param queries 查询特征列表 [numQueries x dim]
             * @param k top-k
             * @return pair<indices, distances>，每个 [numQueries x k]
             */
            std::pair<std::vector<std::vector<int>>, std::vector<std::vector<float>>>
            Search(const std::vector<std::vector<float>>& queries, int k) {
                std::lock_guard<std::mutex> lock(mMutex);

                auto queryNum = queries.size();
                int dbNum = static_cast<int>(mNtotal);
                int sortCnt = std::min(k, dbNum);

                std::vector<std::vector<int>> allIndices(queryNum, std::vector<int>(sortCnt, -1));
                std::vector<std::vector<float>> allDistances(queryNum, std::vector<float>(sortCnt, 0.0F));

                if (!ValidateSearchInput(queries, queryNum, dbNum)) {
                    return {allIndices, allDistances};
                }

                SLOG_DEBUG << "BmcvFaissDatabase::search begin: dim=" << mDim << ", queryNum=" << queryNum
                           << ", dbNum=" << dbNum << ", sortCnt=" << sortCnt;

                int dim = static_cast<int>(mDim);
                std::vector<float> qNorms2(queryNum, 0.0F);
                for (size_t q = 0; q < queryNum; ++q) {
                    for (int d = 0; d < dim; ++d) {
                        qNorms2[q] += queries[q][static_cast<size_t>(d)] * queries[q][static_cast<size_t>(d)];
                    }
                }

                for (size_t q = 0; q < queryNum; ++q) {
                    // 暴力计算 query 与所有 db 向量的平方 L2 距离
                    std::vector<std::pair<float, int>> distIdx;
                    distIdx.reserve(static_cast<size_t>(dbNum));
                    for (int i = 0; i < dbNum; ++i) {
                        float dot = 0.0F;
                        for (int d = 0; d < dim; ++d) {
                            dot += queries[q][static_cast<size_t>(d)] *
                                   mFeatures[static_cast<size_t>(i)][static_cast<size_t>(d)];
                        }
                        float dist2 = qNorms2[q] + mDbNorms2[static_cast<size_t>(i)] - 2.0F * dot;
                        if (dist2 < 0.0F) {
                            dist2 = 0.0F;  // 数值误差保护
                        }
                        distIdx.emplace_back(dist2, i);
                    }
                    // 距离升序排列（最近的在最前）
                    std::partial_sort(distIdx.begin(), distIdx.begin() + sortCnt, distIdx.end(),
                                      [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
                                          return a.first < b.first;
                                      });
                    for (int j = 0; j < sortCnt; ++j) {
                        allDistances[q][static_cast<size_t>(j)] = std::sqrt(distIdx[static_cast<size_t>(j)].first);
                        allIndices[q][static_cast<size_t>(j)] = distIdx[static_cast<size_t>(j)].second;
                    }
                }

                SLOG_DEBUG << "BmcvFaissDatabase::search done: sample_top1_idx="
                           << std::to_string((queryNum > 0 && sortCnt > 0) ? allIndices[0][0] : -9999)
                           << ", sample_top1_dis="
                           << std::to_string((queryNum > 0 && sortCnt > 0) ? allDistances[0][0] : -9999.0F);

                return {allIndices, allDistances};
            }

            bool ValidateSearchInput(const std::vector<std::vector<float>>& queries, size_t queryNum, int dbNum) {
                if (queryNum == 0 || dbNum == 0) {
                    SLOG_WARN << "BmcvFaissDatabase::search early return: queryNum=" << queryNum << ", dbNum=" << dbNum;
                    return false;
                }

                if (mDim == 0) {
                    SLOG_ERROR << "BmcvFaissDatabase::search invalid state: mDim=0 while dbNum=" << dbNum;
                    return false;
                }

                for (size_t i = 0; i < queryNum; ++i) {
                    if (queries[i].size() != mDim) {
                        SLOG_ERROR << "BmcvFaissDatabase::search query dim mismatch at i=" << i << ", expected=" << mDim
                                   << ", got=" << queries[i].size();
                        return false;
                    }
                }

                return true;
            }

            /**
             * @brief 获取指定索引的特征向量
             */
            std::vector<float> Reconstruct(int64_t idx) const {
                std::lock_guard<std::mutex> lock(mMutex);
                if (idx >= 0 && static_cast<size_t>(idx) < mFeatures.size()) {
                    return mFeatures[static_cast<size_t>(idx)];
                }
                return {};
            }

            /**
             * @brief 获取指定索引的 vpId
             */
            std::string GetVpId(int64_t idx) const {
                std::lock_guard<std::mutex> lock(mMutex);
                auto it = mIndexToVpId.find(idx);
                if (it != mIndexToVpId.end()) {
                    return it->second;
                }
                return "";
            }

            size_t GetNtotal() const {
                std::lock_guard<std::mutex> lock(mMutex);
                return mNtotal;
            }

            bool IsEmpty() const {
                std::lock_guard<std::mutex> lock(mMutex);
                return mFeatures.empty();
            }

        private:
            mutable std::mutex mMutex;

            // 特征库数据
            std::vector<std::vector<float>> mFeatures;
            std::vector<std::string> mVpIds;
            std::unordered_map<int64_t, std::string> mIndexToVpId;
            std::vector<float> mDbNorms2;  // 各特征向量的 L2 范数平方缓存，加速搜索
            size_t mDim;
            size_t mNtotal;
        };

    }  // namespace aas
}  // namespace qifeng

#endif  // QIFENG_FRAMWORK_AAS_BMCV_FAISS_DATABASE_H
