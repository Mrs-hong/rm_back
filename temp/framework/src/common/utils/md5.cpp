/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/md5.h"

#include <chrono>
#include <iomanip>
#include <sstream>

#include "common/logger.h"

namespace qifeng {

    MD5Hasher::MD5Hasher() {
        Reset();
    }

    void MD5Hasher::Reset() {
        MD5_Init(&mCtx);
    }

    void MD5Hasher::Update(const void* data, size_t len) {
        if (data && len > 0) {
            MD5_Update(&mCtx, data, len);
        }
    }

    std::string MD5Hasher::Final() {
        unsigned char digest[MD5_DIGEST_LENGTH];
        MD5_Final(digest, &mCtx);
        std::stringstream ss;
        for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
        }
        return ss.str();
    }

    std::string MD5Hasher::FileMD5(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            FLOG_ERROR("MD5Hasher: Cannot open file: " + filePath);
            return {};
        }
        MD5Hasher hasher;
        char buffer[1024 * 1024];
        while (file.good()) {
            file.read(buffer, sizeof(buffer));
            std::streamsize bytesRead = file.gcount();
            if (bytesRead > 0) {
                hasher.Update(buffer, static_cast<size_t>(bytesRead));
            }
        }
        return hasher.Final();
    }

    ModelVerifier::ModelVerifier() : mStatus(std::make_shared<Status>()) {
    }

    bool ModelVerifier::VerifySingleModel(const std::string& modelPath) {
        std::string checksumPath = modelPath + ".md5";
        std::ifstream checksumFile(checksumPath);
        if (!checksumFile.is_open()) {
            FLOG_WARN("ModelVerifier: Checksum file not found, skipping: " + checksumPath);
            return true;
        }

        std::string line;
        std::getline(checksumFile, line);
        checksumFile.close();

        size_t start = line.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) {
            FLOG_WARN("ModelVerifier: Checksum file empty, skipping: " + checksumPath);
            return true;
        }
        size_t end = line.find_last_not_of(" \t\n\r");
        std::string content = line.substr(start, end - start + 1);

        size_t spacePos = content.find_first_of(" \t");
        std::string expectedMD5 = (spacePos != std::string::npos) ? content.substr(0, spacePos) : content;

        if (expectedMD5.length() != 32) {
            FLOG_WARN("ModelVerifier: Invalid MD5 format (len=" + std::to_string(expectedMD5.length()) +
                      "), skipping: " + checksumPath);
            return true;
        }

        FLOG_INFO("ModelVerifier: Verifying " + modelPath + " (expected MD5: " + expectedMD5 + ")");

        std::string actualMD5 = MD5Hasher::FileMD5(modelPath);
        if (actualMD5.empty()) {
            AppendCorruption("Failed to calculate MD5 for model: " + modelPath);
            return false;
        }
        if (actualMD5 != expectedMD5) {
            AppendCorruption("Model corruption detected! File: " + modelPath + " | Expected MD5: " + expectedMD5 +
                             " | Actual MD5: " + actualMD5);
            return false;
        }

        FLOG_INFO("ModelVerifier: Verification passed: " + modelPath);
        return true;
    }

    void ModelVerifier::ModelVerifyWorker(std::vector<std::string> modelPaths) {
        FLOG_INFO("ModelVerifier: Verification started, " + std::to_string(modelPaths.size()) + " models");

        BatchExecutor& batchExecutor = BatchExecutor::GetInstance();

        struct VerifyStats {
            std::atomic<size_t> completed {0};
            std::chrono::high_resolution_clock::time_point startTime;
            size_t total;
        };
        auto stats = std::make_shared<VerifyStats>();
        stats->total = modelPaths.size();
        stats->startTime = std::chrono::high_resolution_clock::now();

        for (const auto& modelPath : modelPaths) {
            batchExecutor.AddTask([this, modelPath, stats]() {
                VerifySingleModel(modelPath);
                size_t done = stats->completed.fetch_add(1) + 1;
                if (done == stats->total) {
                    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::high_resolution_clock::now() - stats->startTime)
                                          .count();
                    FLOG_INFO("ModelVerifier: All " + std::to_string(done) +
                              " verifications finished, total time: " + std::to_string(durationMs) + "ms");
                }
            });
        }

        FLOG_INFO("ModelVerifier: All verification tasks dispatched");
    }

    bool ModelVerifier::IsCorrupted() const {
        return !std::atomic_load(&mStatus)->IsSuccess();
    }

    std::string ModelVerifier::GetCorruptionInfo() const {
        return std::atomic_load(&mStatus)->GetMsg();
    }

    Status ModelVerifier::GetStatus() const {
        return *std::atomic_load(&mStatus);
    }

    void ModelVerifier::Reset() {
        std::atomic_store(&mStatus, std::make_shared<Status>());
    }

    void ModelVerifier::AppendCorruption(const std::string& errMsg) {
        FLOG_ERROR("ModelVerifier: " + errMsg);
        // CAS 循环：先原子 load 当前快照，基于它构造新 Status，再原子 compare_exchange。
        // 若期间被其他线程抢先更新（expected 被更新为最新值），则基于最新值重试。
        std::shared_ptr<Status> expected = std::atomic_load(&mStatus);
        while (true) {
            std::string merged = expected->GetMsg();
            if (!merged.empty()) {
                merged += "; ";
            }
            merged += errMsg;
            auto desired = std::make_shared<Status>(-1, std::move(merged));
            if (std::atomic_compare_exchange_strong(&mStatus, &expected, desired)) {
                break;
            }
            // compare_exchange 失败时 expected 已被更新为当前最新值，直接复用
        }
    }

}  // namespace qifeng
