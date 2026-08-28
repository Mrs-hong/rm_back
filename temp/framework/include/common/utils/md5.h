/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_MD5_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_MD5_H

#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <openssl/md5.h>

#include "common/utils/batch_executor.h"
#include "common/utils/status.h"

namespace qifeng {

    // MD5 哈希计算器：封装 OpenSSL MD5_CTX，支持流式 Update 与一次性 Final。
    class MD5Hasher {
    public:
        MD5Hasher();

        void Reset();

        void Update(const void* data, size_t len);

        // 计算并返回 32 字符小写十六进制 MD5；调用后 ctx 被终结，需要 Reset 才能复用。
        std::string Final();

        // 便捷方法：计算整个文件的 MD5，失败返回空串。
        static std::string FileMD5(const std::string& filePath);

    private:
        MD5_CTX mCtx;
    };

    // 模型完整性校验器：维护校验状态（是否损坏 + 详情），支持单文件校验与批量异步校验。
    class ModelVerifier {
    public:
        ModelVerifier();

        // 校验单个模型文件：读取同目录下的 <modelPath>.md5 期望值并与实际 MD5 比对。
        // 返回 true 表示通过（或没有 .md5 文件跳过校验）；false 表示校验失败并更新损坏状态。
        bool VerifySingleModel(const std::string& modelPath);

        // 批量异步校验：将每个模型的校验任务投递到全局 BatchExecutor 线程池，立即返回。
        void ModelVerifyWorker(std::vector<std::string> modelPaths);

        bool IsCorrupted() const;

        std::string GetCorruptionInfo() const;

        // 返回当前状态快照（按值返回，线程安全）。
        Status GetStatus() const;

        void Reset();

    private:
        void AppendCorruption(const std::string& errMsg);

        // 用原子 shared_ptr 实现 copy-on-write：读方通过 atomic_load 拿到只读快照，
        // 写方通过 CAS 构造并原子交换新的 Status 副本，Status 本身无需任何锁或 atomic。
        std::shared_ptr<Status> mStatus;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_MD5_H
