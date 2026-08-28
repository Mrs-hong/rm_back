//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_QWEN_TRANS_MERGER_H
#define QIFENG_CA_INCLUDE_INTERNAL_QWEN_TRANS_MERGER_H

#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "dao/models/bms_trans.h"
#include "qifeng_framework/aas/aas_callback.h"

namespace qifeng_ca {

    // Qwen 与数据库转写记录的整合合并器(参照 TranscribeHandler 独立模块模式):
    //
    // 职责: 将 Qwen 模型的分段结果与数据库中的 Paraformer 转写记录进行匹配,
    // 通过时间重叠/说话人分组/字符匹配生成替换结果, 并完成DB更新与前端推送。
    // 匹配基准为数据库中稳定去重的转写记录(每个时间段仅一条), 而非Paraformer流式原始输出。
    //
    // 支持的能力:
    //  1) 时间戳完全吻合/覆盖 → 直接替换文本(保留DB记录的name/时间/ID)
    //  2) Qwen覆盖多条DB记录 → 按字符匹配切分Qwen连续文本后逐条替换
    //  3) 同一说话人多条记录 → 融合保留第一条: 文本用QWen结果, 时间保留Paraformer边界
    //     (start=首段start, end=最后合并段end), 其余记录删除(前端返回空文本)
    //  4) 未匹配DB记录缓存(D结果): 跨Qwen窗口复用, 防止Paraformer滚动更新导致记录遗漏
    //  5) 窗口截断残句缓存: 拼接至下一窗口开头续接句子
    //  6) 未匹配QWen段缓存(Q结果): Paraformer落库滞后于QWen窗口时暂存未匹配段,
    //     待缺失记录入库后重放融合; 尝试次数耗尽或停止冲刷时兜底推送, 避免文本永久丢失
    //
    // 与调用方(AudioReadTask)解耦: 构造函数注入audioId/accountId, 内部自管理状态与DB/WS依赖,
    // 调用方只需将Qwen结果交给 OnQwenResult() 处理。
    class QwenTransMerger {
    public:
        QwenTransMerger(const std::string &audioId, uint64_t accountId);

        QwenTransMerger(const QwenTransMerger &) = delete;
        QwenTransMerger(QwenTransMerger &&) = delete;
        QwenTransMerger &operator=(const QwenTransMerger &) = delete;
        QwenTransMerger &operator=(QwenTransMerger &&) = delete;

        // 处理一次QWen结果: 读DB记录 → 合并D缓存 → 重放Q缓存 → 残句拼接 → 匹配替换 → 落库+推送前端
        // 内部加锁, 线程安全; 返回本次产生的替换分段数
        // Flush后到达的结果(stop后返回的最后窗口): 正常匹配替换后, 未匹配段直接兜底插入DB,
        // 不再缓存等待后续窗口, 保证stop后QWen数据一定写入数据库
        size_t OnQwenResult(const qifeng::aas::AasResult &result);

        // 任务停止/最终冲刷: 先重放缓存段完成最后一次融合, 未匹配段兜底插入DB新记录,
        // 保证QWen数据一定写入数据库; 同时置位mStopped, 此后到达的QWen结果走兜底落库; 内部加锁, 线程安全
        void Flush();

    private:
        // 单个Qwen段替换后产生的分段: 保留DB记录元数据(时间戳/说话人/ID), 文本为Qwen文本
        // 同时携带Qwen原始段时间范围与替换前内容, 供日志核对替换时间是否正确
        struct ReplacedSegment {
            qifeng::aas::AasSegment seg;  // 替换后的分段(时间戳为DB记录段)
            int64_t qwenStartTime = 0;    // QWen原始段开始时间(ms)
            int64_t qwenEndTime = 0;      // QWen原始段结束时间(ms)
            uint64_t dbId = 0;            // 匹配到的DB记录ID(0表示未匹配到DB记录)
            bool segFlag = false;         // DB记录段标志
            std::string oldContent;       // 替换前DB记录内容(日志用)
            bool deleted = false;         // true=该DB记录被融合删除(文本为空, 仅推送前端移除)
        };

        // 未匹配到完整DB记录的QWen段暂存项(Q缓存)
        struct PendingQwenSeg {
            qifeng::aas::AasSegment seg;  // 暂存的QWen段(时间戳/文本/说话人)
            int retryLeft = 0;            // 剩余尝试次数(每次重放未匹配则递减)
        };

        // QWen结果与DB记录整合入口(匹配基准为数据库中稳定去重的转写记录):
        // 1) 时间戳范围完全吻合 → 直接替换
        // 2) QWen完全覆盖单条DB记录 → 直接替换(记录尾未达窗口尾=缺失记录未落库 → 暂存等待)
        // 3) 单条记录未被QWen完全覆盖(记录尾超出窗口) → 保留DB原文, 文本暂存待下一窗口续接
        // 4) QWen覆盖多条DB记录 → 按DB记录分句分割QWen连续文本, 仅替换被完全覆盖的记录
        // 5) 未匹配到任何记录 → 暂存缓存, 待缺失记录入库后重放融合
        // 返回true=匹配到至少一条DB记录(完成替换或尾部暂存续接), false=未匹配(已缓存等待)
        bool SplitAndReplaceText(const qifeng::aas::AasSegment &qwenSeg, const std::vector<models::Trans> &dbSegs,
                                 std::vector<ReplacedSegment> &replaced);

        // 基于说话人分组的整合替换
        // QWen段带说话人信息时, 按说话人匹配DB记录:
        // - 同一说话人单条记录 → 直接替换(记录尾未达窗口尾=缺失记录未落库 → 暂存等待)
        // - 同一说话人多条记录(QWen判定该时间段均为该说话人) → 融合保留第一条, 删除其余
        // - 无说话人信息或该时间段混有其他说话人 → 回退现有时间匹配逻辑(SplitAndReplaceText)
        // 返回值语义同SplitAndReplaceText
        bool SplitAndReplaceTextBySpeaker(const qifeng::aas::AasSegment &qwenSeg,
                                          const std::vector<models::Trans> &dbSegs,
                                          std::vector<ReplacedSegment> &replaced);

        // 将QWen单条文本融合进同一说话人的多条DB记录:
        // 保留ID优先取带分句标志(seg_flag=1)的记录(多个分句时取第一次出现, 无分句时取首条);
        // 时间保留Paraformer边界: start=首段start, end=最后合并段end;
        // 其余记录标记删除(前端返回空文本)
        void ConsolidateSameSpeakerSegs(const qifeng::aas::AasSegment &qwenSeg,
                                        const std::vector<models::Trans> &sameSpeaker,
                                        const std::string &consolidatedText,
                                        std::vector<ReplacedSegment> &replaced);

        // 将QWen连续文本按多条DB记录分割并逐条替换(仅替换文本, 保留时间戳/说话人/ID)
        void SplitQwenTextToParaformerSegs(const qifeng::aas::AasSegment &qwenSeg,
                                           const std::vector<models::Trans> &matched,
                                           std::vector<ReplacedSegment> &replaced);

        // 用新文本替换单条DB记录: 标记已替换(防止重复替换)+收集替换结果
        // 携带QWen原始段时间范围, 便于日志核对替换时间是否正确
        void ReplaceDbSeg(const models::Trans &dbSeg, const std::string &newText,
                          const qifeng::aas::AasSegment &qwenSeg, std::vector<ReplacedSegment> &replaced);

        // 将QWen段中因窗口截断而未被替换的文本暂存, 供下一窗口开头拼接续接
        void AppendPendingQwenTail(const std::string &text);

        // [Q缓存] 将未匹配到完整DB记录的QWen段暂存(带时间戳/说话人/剩余尝试次数):
        // Paraformer落库滞后于QWen窗口时(缺失记录尚未写入DB)先缓存等待; 同范围段去重,
        // 缓存超限时淘汰时间最早的段: 先按核心原则(有重叠用Qwen覆盖paraformer记录,
        // 无重叠作为Qwen额外数据插入)处理被淘汰段, 防止内存增长且不丢失Qwen文本
        void CachePendingQwenSeg(const qifeng::aas::AasSegment &qwenSeg,
                                 const std::vector<models::Trans> &dbSegs,
                                 std::vector<ReplacedSegment> &replaced);

        // [Q缓存] 重放暂存的QWen段: 优先匹配当前DB中时间戳明确且已入库的记录
        // (记录创建早于本次处理时间, 即缺失记录已完成入库); 匹配到记录则按现有逻辑融合,
        // 未匹配则扣减剩余尝试次数, 尝试耗尽后由PushReplacedSegments插入DB落库
        void ReplayPendingQwenSegs(const std::vector<models::Trans> &dbSegs, std::vector<ReplacedSegment> &replaced);

        // [Q缓存] 冲刷全部暂存QWen段(任务停止/最终冲刷): 兜底段交由PushReplacedSegments
        // 插入DB新记录, 保证QWen数据一定写入数据库
        void FlushPendingQwenSegs(std::vector<ReplacedSegment> &replaced);

        // 更新D结果缓存: 收集本次Qwen窗口内未被替换的DB记录, 供下次窗口合并匹配;
        // 同时淘汰已替换/过期的缓存记录, 防止内存增长
        void UpdatePendingDbSegs(const std::vector<models::Trans> &dbSegs, int64_t windowStart, int64_t windowEnd);

        // 推送替换后的分段到前端, 并同步更新DB文本内容(保持DB ID不变)
        // dbId==0 的兜底段(Flush/重试耗尽/缓存溢出)直接插入DB新记录, 保证QWen数据一定落库
        void PushReplacedSegments(std::vector<ReplacedSegment> &segments);

    private:
        std::string mAudioId;
        uint64_t mAccountId {0};

        // 已被QWen替换的DB记录ID集合(数据库记录稳定去重, 直接按ID防重复替换)
        std::set<uint64_t> mReplacedDbIds;
        // 上一窗口因截断而未被替换的QWen文本片段, 拼接至下一窗口开头续接句子
        std::string mPendingQwenTail;
        // D结果缓存: 上次Qwen处理时时间与窗口重叠但未被替换的DB记录(如记录尾超出Qwen窗口)。
        // 下次Qwen调用时合并参与匹配, 防止Paraformer滚动更新(删旧插新)导致记录遗漏。
        std::vector<models::Trans> mPendingDbSegs;
        // Q结果缓存: 未匹配到完整DB记录的QWen段(Paraformer落库滞后于QWen窗口时暂存),
        // 缺失记录入库后由ReplayPendingQwenSegs重放完成融合
        std::vector<PendingQwenSeg> mPendingQwenSegs;
        // [部分替换累积] 本窗口内被部分替换(保留DB头/尾切片)记录的最新全文(dbId→text):
        // 同窗口多段QWen先后部分替换同一记录时, 后一段基于累积文本继续切片,
        // 避免前一段QWen结果被旧DB原文覆盖; 每次OnQwenResult/Flush落库推送后清空
        std::unordered_map<uint64_t, std::string> mPartialTexts;
        // Flush后置位(mMutex保护): 任务已停止, 此后到达的QWen结果(晚于stop返回的最后窗口)
        // 不再依赖后续窗口, 未匹配段由FlushPendingQwenSegs兜底插入DB, 保证文本不丢失
        bool mStopped {false};
        // 串行化OnQwenResult调用(实时回调线程与结束冲刷线程可能并发进入)
        std::mutex mMutex;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_QWEN_TRANS_MERGER_H
