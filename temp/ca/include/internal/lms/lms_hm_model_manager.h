//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_LMS_LMS_HM_MODEL_MANAGER_H
#define QIFENG_CA_INCLUDE_INTERNAL_LMS_LMS_HM_MODEL_MANAGER_H

#include <memory>
#include <mutex>

#include "qifeng_framework/lms/metting.h"
#include "qifeng_framework/lms_hm/hm_qwen_infer.h"

namespace qifeng_ca {

    // LMS(HM runtime)模型管理器: 全局单例, 管理HmQwenInfer实例
    // 在main中初始化, 提供线程安全的模型访问
    class LmsHmModelManager {
    public:
        static LmsHmModelManager &GetInstance();

        // 初始化模型(从配置读取路径, 创建HmQwenInfer)
        bool Initialize();

        // 释放模型资源
        void Shutdown();

        // 执行会议纪要总结(线程安全): 内部调用 HmQwenInfer::Summarize()
        // 支持进度回调; 任务被取消时抛出 qifeng::lmshm::OperationCancelled
        qifeng::lms::SummaryResult Summarize(const qifeng::lmshm::MettingInfo &mettingInfo,
                                             const qifeng::lmshm::MettingHints &hints = {},
                                             const qifeng::lmshm::ProgressObserver &progressObserver = {});

        // 请求取消当前正在执行的总结任务(线程安全、非阻塞)
        void Cancel();

        // 获取HmQwenInfer实例(未初始化返回nullptr)
        std::shared_ptr<qifeng::lmshm::HmQwenInfer> GetInfer();

        bool IsInitialized() const { return mInitialized; }

    private:
        LmsHmModelManager() = default;
        ~LmsHmModelManager() = default;
        LmsHmModelManager(const LmsHmModelManager &) = delete;
        LmsHmModelManager &operator=(const LmsHmModelManager &) = delete;
        LmsHmModelManager(LmsHmModelManager &&) = delete;
        LmsHmModelManager &operator=(LmsHmModelManager &&) = delete;

        bool CreateModelFromConfig();

        std::mutex mMutex;
        std::shared_ptr<qifeng::lmshm::HmQwenInfer> mHmQwenInfer;
        bool mInitialized {false};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_LMS_LMS_HM_MODEL_MANAGER_H
