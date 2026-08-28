//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_SCHEDULE_TASK_SUMMARY_TASK_H
#define QIFENG_CA_INCLUDE_SCHEDULE_TASK_SUMMARY_TASK_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "common/config/schedule_config.h"
#include "common/status.h"
#include "qifeng_framework/lms/metting.h"
#include "qifeng_framework/lms_hm/hm_qwen_infer.h"
#include "schedule/task/base_task.h"

namespace qifeng_ca {

    // 总结任务结果
    struct SummaryResult {
        Status mStatus;
        std::string mOverviewText;
        std::string mKeywords;
    };

    using SummaryResultCallback = std::function<void(const SummaryResult &)>;

    // 总结任务输入数据(从Audio/Note/Trans表查询后组装)
    struct SummaryInput {
        std::string mText;
        std::string mDate;
        std::string mLocation;
        std::string mHost;
        std::string mAttendees;
        std::string mNote;
        int mMeetingKind {0};
        int mUseNote {0};
        int mReSummury {0};
        int mSumWordCount {0};
    };

    // 总结任务(可抢占): 调用LMS Summarizer生成会议纪要, 完成后通过callback返回结果
    class SummaryTask : public BaseTask {
    public:
        SummaryTask(const std::string &audioId, uint64_t accountId);

        ~SummaryTask() override;

        SummaryTask(const SummaryTask &) = delete;
        SummaryTask(SummaryTask &&) = delete;
        SummaryTask &operator=(const SummaryTask &) = delete;
        SummaryTask &operator=(SummaryTask &&) = delete;

        // BaseTask接口
        void Start() override;
        void Stop() override;
        void Preempt() override;
        void Cancel() override;
        void After() override;
        int Priority() const override { return static_cast<int>(Priority::Summary); }
        bool IsPreemptable() const override { return true; }
        std::string_view GetTaskType() const override { return "Summary"; }

        // 设置输入数据和回调(在Start前调用)
        void SetInput(const SummaryInput &input) { mInput = input; }
        void SetCallback(const SummaryResultCallback &cb) { mCallback = cb; }

        // 设置纪要生成提示信息(议题+范文风格, 在Start前调用)
        // hints.topic 非空时跳过LLM自动提取议题; hints.exampleSummary 非空时学习范文风格
        void SetHints(const qifeng::lms::MettingHints &hints) { mHints = hints; }

    private:
        // 获取自身shared_ptr(用于Workflow回调)
        std::shared_ptr<SummaryTask> Self() { return std::static_pointer_cast<SummaryTask>(shared_from_this()); }

        // 在workflow线程中执行总结
        // generation: 启动时分配的运行代次, 用于区分抢占后重新启动的旧go_task
        void DoSummarize(uint64_t generation);

        // 等待LMS初始化就绪, 返回false表示任务已被取消/抢占或代次已过期
        bool WaitLmsReady(uint64_t generation);

        // 等待旧go_task退出(Preempt中限时等待, 避免旧go_task在新Start后访问已重置成员)
        void WaitGoTaskExit();

        // 从DB构建总结输入(查询Audio/Note/Trans表)
        void BuildInputFromDb();

        // 构建HmQwenInfer::Summarize所需的会议信息输入
        qifeng::lmshm::MettingInfo BuildSummaryData();

        // 处理Summarizer结果(variant: SummaryData/SummaryError)
        void HandleResult(const qifeng::lms::SummaryResult &result);

        void NotifyResult(const SummaryResult &result);

        // 计算总结耗时(毫秒)并写入DB
        void PersistSumDuration();

        // 构建进度回调(更新总结进度和预计完成时间)
        std::function<void(qifeng::lms::ProgressStat)> BuildProgressObserver();

        // 进度推算定时器(引擎仅上报一次预估总耗时, 定时器按该耗时周期推送进度兜底)
        void StartProgressTimer();
        void StopProgressTimer();
        void ScheduleNextProgress();
        void OnProgressTick();
        uint64_t EstimateTotalMsByTextLen(size_t textLen) const;
        int CalcProgress(uint64_t spentMs, uint64_t totalMs);

        // 超时定时器
        void StartTimeoutTimer();
        void StopTimeoutTimer();
        void OnTimeout();

    private:
        SummaryInput mInput;
        SummaryResult mResult;
        SummaryResultCallback mCallback;
        qifeng::lms::MettingHints mHints;  ///< 纪要生成提示(议题+范文风格, 默认空)

        uint64_t mSumStartTime {};  // 总结开始时间点(用于统计耗时)

        // 运行代次: 每次 Start 自增, DoSummarize 中校验, 过期go_task静默退出
        std::atomic<uint64_t> mRunGeneration {0};
        // go_task 是否在运行: Preempt 中等待其退出, 避免旧go_task在新Start后访问已重置成员
        std::atomic<bool> mGoTaskActive {false};

        // 超时定时器名称(唯一,支持cancel_by_name)
        std::string mTimeoutTimerName;

        // 进度推算(引擎预估总耗时 + 定时器兜底推送)
        std::string mProgressTimerName;                  // 10s周期定时器名称
        std::atomic<int> mLastProgress {0};              // 上次推送的进度(保证单调递增)
        std::atomic<uint64_t> mEstimatedTotalMs {0};     // 推算总时长(毫秒)
        std::atomic<bool> mExtraTimeAdded {false};       // 是否已追加额外时长
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_SCHEDULE_TASK_SUMMARY_TASK_H
