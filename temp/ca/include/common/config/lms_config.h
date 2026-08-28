//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_LMS_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_LMS_CONFIG_H

#include <string>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class LmsConfig {
    public:
        static LmsConfig &GetInstance() {
            static LmsConfig Instance;
            return Instance;
        }

        std::string GetTokenizerPath() const { return CONFIG_MANAGER.GetString("lms.model", "tokenizer_path", ""); }

        std::string GetConfigPath() const { return CONFIG_MANAGER.GetString("lms.model", "config_path", ""); }

        std::string GetModelPath() const { return CONFIG_MANAGER.GetString("lms.model", "model_path", ""); }

        int GetChunkSize() const { return CONFIG_MANAGER.GetInt("lms.summarizer", "chunk_size", 2000); }

        int GetChunkOverlap() const { return CONFIG_MANAGER.GetInt("lms.summarizer", "chunk_overlap", 200); }

        float GetMaxExecutionTime() const {
            return static_cast<float>(CONFIG_MANAGER.GetDouble("lms.summarizer", "max_execution_time", 1800.0));
        }

        // m50b 模型
        std::string GetEmbeddingBinPath() const { return CONFIG_MANAGER.GetString("lms.model", "embedding_bin_path", ""); }

        std::string GetPrefillModelPath() const { return CONFIG_MANAGER.GetString("lms.model", "prefill_model_path", ""); }

        std::string GetDecodeModelPath() const { return CONFIG_MANAGER.GetString("lms.model", "decode_model_path", ""); }

        std::string GetTokenizerJsonPath() const { return CONFIG_MANAGER.GetString("lms.model", "tokenizer_json_path", ""); }

        int GetDeviceId() const { return CONFIG_MANAGER.GetInt("lms.model", "device_id", 0); }

    private:
        LmsConfig() = default;
        ~LmsConfig() = default;
        LmsConfig(const LmsConfig &) = delete;
        LmsConfig &operator=(const LmsConfig &) = delete;
        LmsConfig(LmsConfig &&) = delete;
        LmsConfig &operator=(LmsConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_LMS_CONFIG_H
