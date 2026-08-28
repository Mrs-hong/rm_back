//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_SCHEDULE_TASK_FILE_REALTIME_TRANS_TASK_H
#define QIFENG_CA_INCLUDE_SCHEDULE_TASK_FILE_REALTIME_TRANS_TASK_H

#include <fstream>
#include <memory>
#include <string>

#include "schedule/task/offline_trans_task.h"

namespace qifeng_ca {

    // 文件模拟实时转写任务: 继承离线转写任务, 复用文件校验/WAV解析/生命周期/总结链路,
    // 仅重写AAS投喂方式——以实时provider(AudioQWStreamProvider, isRealtime=true)按2s分片
    // 定时推送, 用离线音频替代麦克风输入测试实时转写链路。
    class FileRealtimeTransTask : public OfflineTransTask {
    public:
        FileRealtimeTransTask(const std::string &audioId, uint64_t accountId, const std::string &srcFilePath,
                              const std::string &taskFilePath, bool restartTrans = false);

        ~FileRealtimeTransTask() override;

        FileRealtimeTransTask(const FileRealtimeTransTask &) = delete;
        FileRealtimeTransTask(FileRealtimeTransTask &&) = delete;
        FileRealtimeTransTask &operator=(const FileRealtimeTransTask &) = delete;
        FileRealtimeTransTask &operator=(FileRealtimeTransTask &&) = delete;

        int Priority() const override { return static_cast<int>(Priority::FileRealtimeTrans); }
        std::string_view GetTaskType() const override { return "FileRealtimeTrans"; }

    protected:
        // 重写AAS投喂: 实时provider + 定时2s推送(替代基类DataPreparer拉取)
        bool StartAasTranscribe() override;
        // 快速停止(Preempt/Stop调用)
        void EndAudioStream() override;
        // EOF
        void OnTransComplete() override;
        // 文件模拟实时转写不做预估完成时间
        void EstimateAndPersistPlanFinishTime() override;
        std::string_view GetTranscribingMsg() const override;
        std::string_view GetTransFailedMsg() const override;

    private:
        std::shared_ptr<FileRealtimeTransTask> Self() {
            return std::static_pointer_cast<FileRealtimeTransTask>(shared_from_this());
        }

        // 读循环: 从文件读取2s数据推送到provider, EOF触发OnTransComplete
        void DoReadCycle();
        // 调度下一次读取(Workflow定时器, 2s间隔, 实时节奏)
        void ScheduleNext();
        // 取消读循环定时器
        void CancelTimer();
        // 自然结束: 等待drain + NotifyFinal + 停止AAS
        void EndAudioStreamWithDrain();

    private:
        std::shared_ptr<std::ifstream> mSharedFile;  // 任务文件读取流(读循环间共享位置)
        std::string mTimerName;                      // 读循环定时器名(按audioId隔离)

        // 每次推送AAS的音频时长(秒)
        static constexpr int ChunkSeconds = 2;
        // 读循环间隔(秒): 与ChunkSeconds一致, 模拟实时输入节奏
        static constexpr int ReadIntervalSec = 2;
        // 自然结束等待drain的最大时长与检查步长(毫秒)
        static constexpr int DrainMaxWaitMs = 2000;
        static constexpr int DrainCheckStepMs = 100;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_SCHEDULE_TASK_FILE_REALTIME_TRANS_TASK_H
