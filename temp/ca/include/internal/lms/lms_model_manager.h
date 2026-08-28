//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_LMS_LMS_MODEL_MANAGER_H
#define QIFENG_CA_INCLUDE_INTERNAL_LMS_LMS_MODEL_MANAGER_H

#include <memory>
#include <mutex>

#include "qifeng_framework/lms/metting.h"

namespace qifeng_ca {

    // LMS模型管理器: 全局单例, 管理LLM模型实例和MettingSummarizer实例
    // 在main中初始化, 提供线程安全的模型访问
    class LmsModelManager {
    public:
        static LmsModelManager &GetInstance();

        // 初始化模型(从配置读取路径, 创建Model)
        bool Initialize();

        // 释放模型资源
        void Shutdown();

        // 创建MettingSummarizer(线程安全, 根据mettingInfo选择Brief/Normal/Detailed)
        std::shared_ptr<qifeng::lms::MettingSummarizer> CreateSummarizer(const qifeng::lms::MettingInfo &mettingInfo);

        // 创建MettingSummarizer(带议题和自定义模板, 线程安全)
        std::shared_ptr<qifeng::lms::MettingSummarizer> CreateSummarizer(const qifeng::lms::MettingInfo &mettingInfo,
                                                                         const qifeng::lms::MettingHints &hints);

        // 获取Model实例
        std::shared_ptr<qifeng::lms::Model> GetModel();

        bool IsInitialized() const { return mInitialized; }

    private:
        LmsModelManager() = default;
        ~LmsModelManager() = default;
        LmsModelManager(const LmsModelManager &) = delete;
        LmsModelManager &operator=(const LmsModelManager &) = delete;
        LmsModelManager(LmsModelManager &&) = delete;
        LmsModelManager &operator=(LmsModelManager &&) = delete;

        bool CreateModelFromConfig();

        std::mutex mMutex;
        std::shared_ptr<qifeng::lms::Model> mModel;
        bool mInitialized {false};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_LMS_LMS_MODEL_MANAGER_H
