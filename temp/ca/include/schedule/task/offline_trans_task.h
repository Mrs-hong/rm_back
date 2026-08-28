//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_SCHEDULE_TASK_OFFLINE_TRANS_TASK_H
#define QIFENG_CA_INCLUDE_SCHEDULE_TASK_OFFLINE_TRANS_TASK_H

#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "common/audio/audio_utils.h"
#include "common/audio_enums.h"
#include "internal/aas/audio_transcribe_manager.h"
#include "internal/aas/bms_audio_provider.h"
#include "schedule/task/base_task.h"

namespace qifeng_ca {

    class TranscribeHandler;

    // 离线转写任务(可抢占): 校验格式 -> 推送到AAS离线转写 -> 完成后提交总结任务
    class OfflineTransTask : public BaseTask {
    public:
        OfflineTransTask(const std::string &audioId, uint64_t accountId, const std::string &srcFilePath,
                         const std::string &taskFilePath, bool restartTrans = false);

        ~OfflineTransTask() override;

        OfflineTransTask(const OfflineTransTask &) = delete;
        OfflineTransTask(OfflineTransTask &&) = delete;
        OfflineTransTask &operator=(const OfflineTransTask &) = delete;
        OfflineTransTask &operator=(OfflineTransTask &&) = delete;

        // BaseTask接口
        void Start() override;
        void Stop() override;
        void Cancel() override;
        void Preempt() override;
        void After() override;
        int Priority() const override { return static_cast<int>(Priority::OfflineTrans); }
        bool IsPreemptable() const override { return true; }
        std::string_view GetTaskType() const override { return "OfflineTrans"; }

    protected:
        // 获取自身shared_ptr(用于回调)
        std::shared_ptr<OfflineTransTask> Self() {
            return std::static_pointer_cast<OfflineTransTask>(shared_from_this());
        }

        // 准备任务文件(校验+重采样)
        bool PrepareTaskFile();
        // 解析WAV头信息
        bool ParseWavHeader();

        // 启动AAS转写
        virtual bool StartAasTranscribe();
        // 在StartAasTranscribe内部注入通用回调(适配新旧两种provider)
        void AttachTransResultCallback(BmsAudioProvider* provider);
        // 结束音频流(信号end+停止AAS job+清理provider)
        virtual void EndAudioStream();
        // 构建DataPreparer回调(从taskFile中流式读取音频数据)
        BmsAudioProvider::DataPreparer BuildDataPreparer();
        // 流式读取下一块音频数据(供DataPreparer回调调用)
        bool ReadNextChunk(const std::shared_ptr<std::ifstream> &sharedFile, std::vector<uint8_t> &outData,
                           uint32_t generation);

        // 转写完成处理
        virtual void OnTransComplete();

        // 转写失败处理: 设置错误状态并终止任务
        void OnTransFailed(std::string_view message);

        // 检查转写是否超时(40分钟), 超时则调用OnTransFailed
        void CheckTransTimeout();

        // 计算转写耗时(毫秒)
        void PersistTransDuration();

        // 预估转写完成时间并写入DB(仅首次)
        virtual void EstimateAndPersistPlanFinishTime();

        // 当实际耗时接近初始预估时, 根据当前进度重新计算并更新预估完成时间
        void RefreshPlanFinishTimeIfNeeded();

        // 更新转写进度(基于已读字节数)
        void UpdateTransProgressByBytes(size_t bytesRead);

        // 转写中状态文案
        virtual std::string_view GetTranscribingMsg() const { return AudioStatusMsg::OfflineTranscribing; }
        // 转写失败状态文案
        virtual std::string_view GetTransFailedMsg() const { return AudioStatusMsg::OfflineTransFailed; }

    protected:
        std::string mSrcFilePath;
        std::string mTaskFilePath;
        bool mTransStarted {false};   // 转写是否真正启动(用于After判断是否提交SummaryTask)
        uint64_t mTransStartTime {};  // 转写开始时间点(用于统计耗时)

        AudioUtilsConfig mAasFormatConfig;

        WavHeaderInfo mWavInfo;
        uint64_t mTotalTime {60};  // 每次推送aas的时长(秒)
        size_t mChunkSize {};      // 每次读取/推送字节数 = mOneSecBytes * mTotalTime

        // AAS provider与handler
        std::shared_ptr<BmsAudioProvider> mProvider;
        std::shared_ptr<TranscribeHandler> mHandler;

        bool mFailed {false};  // 转写是否失败
        std::atomic<bool> mCancelled {false};  // 是否被取消(删除录音触发), After据此跳过提交SummaryTask

        // 运行代次 : 每次 Start() 递增, 回调捕获当前代次,
        // 过期回调(来自上次被抢占的 AAS job)检测到代次不匹配时直接退出, 避免误触发 OnTransComplete
        std::atomic<uint32_t> mRunGeneration {0};

    private:
        bool mRestartTrans {false};
        size_t mTransBytesRead {0};          // 转写已读字节数(用于计算进度)
        int64_t mInitialPlanFinishTime {0};  // 初始预估完成时间(用于判断是否需要刷新)
        // 运行时可切换的provider实现: false=AudioStreamProvider(有冗余), true=OfflineQWStreamProvider(10s无冗余)
        bool mUseQwProvider {false};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_SCHEDULE_TASK_OFFLINE_TRANS_TASK_H
