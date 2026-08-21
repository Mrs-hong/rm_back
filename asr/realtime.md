# 实时转写三模式流程与代码阅读顺序

> 本文档面向首次阅读 AAS（Audio Audio transcription Service）代码的工程师，梳理
> PARAFORMER / QWEN / HYBRID 三种实时转写模式的执行逻辑、关键数据结构、DAG 节点
> 与收尾流程，并给出推荐的代码阅读顺序（含文件 + 行号，均相对 `asr/` 项目根）。

---

## 一、整体数据流

```
                    ┌─────────────────────────────────────────────────────────┐
                    │  test/flow_transcribe_test.cpp  RunTranscribe            │
                    │    FileAudioSource(继承 GetAudioBase) 按 1s 块流式推送 PCM │
                    └──────────────────────────┬──────────────────────────────┘
                                               │ AasParaformerJobStart / AasQwenJobStart / AasHybridJobStart
                                               ▼
                    ┌─────────────────────────────────────────────────────────┐
                    │  AasJobManager::Start  (aas_api.cpp:1061)                │
                    │    CreateContext() 创建模型上下文                          │
                    │    创建 AasMeeting（会议级缓存/累积缓冲）                 │
                    │    BatchExecutor 提交 numThreads 个 AasThreadFunc        │
                    └──────────────────────────┬──────────────────────────────┘
                                               ▼
              ┌──────────────────────────────────────────────────────────────────┐
              │  AasJobManager::AasThreadFunc  (aas_api.cpp:925)  主循环 while(true)│
              │    1. getAudioBase->GetAudio(true) 取 1s 音频块                    │
              │    2. 停止且无数据 → OfflineLastRun + FlushHybridQwenBuffer → break│
              │    3. AudioLoad::GetPCMData 重采样到 16k/1ch/32bit                │
              │    4. 模式分发:                                                   │
              └────────┬───────────────────────────────────┬─────────────────────┘
                       │ HYBRID                            │ PARAFORMER / QWEN
                       ▼                                   ▼
     ┌─────────────────────────────────┐   ┌────────────────────────────────────┐
     │ HybridProcess (aas_api.cpp:1136)│   │ PrepareTaskData (aas_api.cpp:874)   │
     │                                 │   │ RunAasSynchronous (aas_api.cpp:588) │
     │ ┌─REALTIME 线(同步)────────────┐│   │   BuildAasDAG → ExecuteAasDAG       │
     │ │ PrepareTaskData(PARAFORMER)   ││   │   ExtractResult → callback         │
     │ │ RunAasSynchronous → callback  ││   │ steady_clock 计 mProcessLatencyMs │
     │ │ seg.resultSource = REALTIME   ││   └────────────────────────────────────┘
     │ └───────────────────────────────┘│
     │ ┌─PRECISE 线(异步累积)──────────┐│
     │ │ audio 累积到 mQwenAccumBuffer  ││
     │ │ 满 30s → 取 chunk(保留 3s tail)││
     │ │ BatchExecutor.AddTask:         ││
     │ │   HybridQwenTask(异步,QWEN)    ││
     │ │     MergePreciseOverlap 去重    ││
     │ │     seg.resultSource = PRECISE  ││
     │ │     → callback                  ││
     │ └───────────────────────────────┘│
     └─────────────────────────────────┘
                       │
                       ▼ 停止
     ┌─────────────────────────────────────────────────────────┐
     │ AasJobStop → AasJobManager::Stop (aas_api.cpp:1081)     │
     │   等待在途 qwen 异步任务完成 → ResetModelContext         │
     └─────────────────────────────────────────────────────────┘
```

**关键区别**：
- PARAFORMER/QWEN：每 1s 块同步走一次完整 DAG（VAD→ASR→SV→AGG），结果即时回调。
- HYBRID：实时线每 1s 同步走 paraformer DAG（低延迟）；精确线累积 30s 触发一次异步 qwen DAG（带词级时间戳），双线结果通过同一回调但 `resultSource` 标签区分。

---

## 二、关键数据结构

定义位置：`include/aas/aas_callback.h`

| 结构 | 行号 | 作用 |
|---|---|---|
| `AasArchType` | :21 | 枚举：PARAFORMER / QWEN / HYBRID，决定 DAG 构建与分发路径 |
| `AasResultSource` | :28 | 枚举：REALTIME（paraformer 实时线）/ PRECISE（qwen 精确线），仅 HYBRID 区分 |
| `AasTimestampItem` | :36 | 词级时间戳项：text / startMs / endMs（绝对时间，含 BMS 起始偏移） |
| `StreamInferContent` | :45 | 流式推理单次返回：fixText / unfixText / sequence / isDecoded |
| `AasSegment` | :55 | 单段结果：时间区间 / 文本 / 说话人 / 声纹 / 时间戳 / resultSource |
| `AasResult` | :75 | 完整结果：code / text / segments / svEmbedding / ready 标志 |
| `FormatConfig` | :90 | 音频目标格式：sampleRate(16000) / channels(1) / bitDepth(32) |
| `BmsInfo` | :104 | 推送元信息：mStartTime / mEndTime / mPushIntervalMs / **mProcessLatencyMs**（纯处理耗时） |

**`BmsInfo.mProcessLatencyMs`** 是当前版本的计时核心：框架在 `RunAasSynchronous`（或 `HybridQwenTask`）前后用 `steady_clock` 计量纯处理耗时（VAD→文字），写入此字段，回调端直接读取，无需回查推送时刻。

---

## 三、代码阅读顺序（推荐）

### 3.1 入口层

| 顺序 | 文件:行号 | 函数 | 阅读要点 |
|---|---|---|---|
| 1 | `test/flow_transcribe_test.cpp:54` | 路径常量 + CHUNK_MS | DATA_DIR/OUTPUT_DIR/CONFIG_PATH，1s 块推送 |
| 2 | `test/flow_transcribe_test.cpp` RunTranscribe | 测试主逻辑 | FileAudioSource 继承 GetAudioBase，三模式各跑一遍 |
| 3 | `src/aas/aas_api.cpp:73` | AasParaformerJobStart | 三个 JobStart 都转调 `AasJobManager::Start` |
| 4 | `src/aas/aas_api.cpp:97` | InitializeAasResources | 模型预加载入口 |

### 3.2 主循环层

| 顺序 | 文件:行号 | 函数 | 阅读要点 |
|---|---|---|---|
| 5 | `src/aas/aas_api.cpp:1061` | AasJobManager::Start | CreateContext + 建 meeting + BatchExecutor 提交 AasThreadFunc |
| 6 | `src/aas/aas_api.cpp:925` | AasThreadFunc | **核心循环**：取音频→重采样→模式分发→计时→回调 |
| 7 | `src/aas/aas_api.cpp:892` | OfflineLastRun | 停止时冲刷滑动窗口缓存的尾部段 |
| 8 | `src/aas/aas_api.cpp:1081` | AasJobManager::Stop | 置 mIsStop + 等待在途 qwen future + ResetModelContext |
| 9 | `src/aas/aas_api.cpp:1105` | AasJobManager::Reset | Stop + 清除会议缓存/上下文 |

### 3.3 同步推理路径（PARAFORMER / QWEN 共用）

| 顺序 | 文件:行号 | 函数 | 阅读要点 |
|---|---|---|---|
| 10 | `src/aas/aas_api.cpp:874` | PrepareTaskData | 将音频/格式/meeting/archType 填入 CommonTaskData |
| 11 | `src/aas/aas_api.cpp:588` | RunAasSynchronous | 创建 taskId → ExecuteAasDAG → 返回 AasResult |
| 12 | `src/aas/aas_api.cpp:537` | ExecuteAasDAG | BuildAasDAG + WorkflowDAGExecutor.Execute + ExtractResult |
| 13 | `src/aas/aas_api.cpp:498` | BuildAasDAG | 按 archType 分发到 BuildAasParaformerDAG / BuildAasQwenDAG |
| 14 | `src/aas/aas_api.cpp:285` | BuildAasParaformerDAG | VAD→ASR_PUNC→AGG；VAD→SV→AGG |
| 15 | `src/aas/aas_api.cpp:313` | BuildAasQwenDAG | VAD→QwenASR→AGG；VAD→SV→AGG |

### 3.4 DAG 任务节点（`src/aas/aas_tasks.cpp`）

| 顺序 | 文件:行号 | 节点 | 职责 |
|---|---|---|---|
| 16 | `src/aas/aas_tasks.cpp:61` | VADTask::Execute | silero VAD 切段 + 滑动窗口尾部缓存 + 特征提取 |
| 17 | `src/aas/aas_tasks.cpp:173` | ASRPuncTask::Execute | paraformer 推理 + MergeOverlapText 段间去重 + PUNC 加标点 |
| 18 | `src/aas/aas_tasks.cpp:307` | QwenASRTask::Execute | qwen3-asr tcim 推理 + 词级时间戳（ForcedAligner 降级模式） |
| 19 | `src/aas/aas_tasks.cpp:448` | ExtractSpeakerEmbeddingTask::Execute | campplus 声纹提取 |
| 20 | `src/aas/aas_tasks.cpp:625` | AggregationTask::Execute | 说话人聚类(hdbscan/谱聚类) + 库匹配 + 标签分配 + 段生成 |

### 3.5 HYBRID 双线路径

| 顺序 | 文件:行号 | 函数 | 阅读要点 |
|---|---|---|---|
| 21 | `src/aas/aas_api.cpp:1136` | HybridProcess | **实时线**(同步 paraformer) + **精确线**(累积 30s 异步 qwen) |
| 22 | `src/aas/aas_api.cpp:1221` | HybridQwenTask | 独立 meeting + QWEN DAG + MergePreciseOverlap 跨 chunk 去重 |
| 23 | `src/aas/aas_api.cpp:31` | MergePreciseOverlap | UTF-8 字符级最长公共子串去重（prev 尾 ∩ cur 头） |
| 24 | `src/aas/aas_api.cpp:1280` | FlushHybridQwenBuffer | 停止时冲刷精确线剩余累积缓冲（<1s 丢弃） |

### 3.6 模型推理层（`src/models_hm/`，tcim 版）

| 顺序 | 文件 | 职责 |
|---|---|---|
| 25 | `src/models_hm/manager/models_manager.cpp` | 模型管理单例：CreateContext / ASRProcess / QwenASRProcess / VADProcess / SVProcess |
| 26 | `src/models_hm/worker/vad_worker.cpp` | silero VAD（onnxruntime） |
| 27 | `src/models_hm/worker/asr_worker.cpp` | paraformer encoder/decoder/predictor（tcim hmm + onnxruntime predictor） |
| 28 | `src/models_hm/worker/qwen3_asr_worker.cpp` | qwen3-asr encode/prefill/decode（tcim hmm） |
| 29 | `src/models_hm/worker/punc_worker.cpp` | ct-transformer 标点（tcim hmm） |
| 30 | `src/models_hm/worker/sv_worker.cpp` | campplus 声纹（tcim hmm） |
| 31 | `src/models_hm/worker/audio_feature.cpp` | Fbank/Mel 特征提取（kaldi-native-fbank + NumCpp） |

---

## 四、三模式对照表

| 维度 | PARAFORMER | QWEN | HYBRID |
|---|---|---|---|
| **入口** | AasParaformerJobStart | AasQwenJobStart | AasHybridJobStart |
| **DAG** | VAD→ASR_PUNC→AGG；VAD→SV→AGG | VAD→QwenASR→AGG；VAD→SV→AGG | 实时线=paraformer DAG；精确线=qwen DAG |
| **单段上限** | 15s（paraformer_max_segment_ms） | 30s（qwen_chunk_ms） | 实时线 15s；精确线 30s（hybrid_qwen_chunk_ms） |
| **段间重叠** | 3s（paraformer_overlap_ms） | 3s（qwen_overlap_ms） | 精确线 3s overlap 保留 |
| **词级时间戳** | 无 | 有（qwen ForcedAligner） | 精确线有 |
| **resultSource** | REALTIME | REALTIME | REALTIME + PRECISE |
| **线程模型** | 全同步（AasThreadFunc 内） | 全同步 | 实时线同步 + 精确线异步（BatchExecutor） |
| **计时点** | RunAasSynchronous 前后 steady_clock | 同左 | 实时线同左；精确线 HybridQwenTask 内 steady_clock |
| **收尾** | OfflineLastRun（冲刷滑动窗口尾部） | 同左 | OfflineLastRun + FlushHybridQwenBuffer |
| **典型延迟** | avg ~277ms, p95 ~367ms | avg ~918ms, p95 ~1816ms | 实时线 avg ~320ms；精确线 avg ~5693ms |
| **文本去重** | MergeOverlapText（段间头尾） | 无（整段一次性） | MergePreciseOverlap（跨 chunk overlap） |

---

## 五、HYBRID 完整时序示例

假设音频持续推送，hybrid_qwen_chunk_ms=30000，qwen_overlap_ms=3000：

```
t=0s     推送第1块 → HybridProcess
         ├─ 实时线：paraformer DAG 同步推理 → callback(REALTIME)
         └─ 精确线：mQwenAccumBuffer += 1s (累积=1s, <30s, 不触发)

t=1s     推送第2块 → HybridProcess
         ├─ 实时线：paraformer → callback(REALTIME)
         └─ 精确线：累积=2s, 不触发
         ...
         (每秒：实时线 callback 一次，精确线静默累积)

t=30s    推送第30块 → HybridProcess
         ├─ 实时线：paraformer → callback(REALTIME)
         └─ 精确线：累积=30s ≥ 30s, 触发：
             ├─ 取 chunk[0,30s), 保留尾部 [27,30s) 作为下段 overlap
             ├─ mQwenAccumStartMs 更新 = 0 + (30-3) = 27s
             └─ BatchExecutor.AddTask(HybridQwenTask) 异步执行

         [异步] HybridQwenTask:
             ├─ 独立 qwenMeeting（避免干扰实时线缓存）
             ├─ QWEN DAG 推理（30s 音频 → 文本 + 词级时间戳）
             ├─ MergePreciseOverlap(prev="", cur=text) → newText=text
             ├─ mLastPreciseText = text
             ├─ seg.resultSource = PRECISE
             └─ callback(PRECISE, 带时间戳)

t=31s..57s  精确线继续累积（每秒+1s，从 3s 累到 30s）
            实时线每秒 callback(REALTIME)

t=57s    精确线累积=30s(27s保留+30s新增-27s已取=30s), 再次触发：
         ├─ chunk[27s,57s), 保留 [54,57s)
         └─ HybridQwenTask 异步:
             ├─ qwen 推理 → text2
             ├─ MergePreciseOverlap(prev=text1, cur=text2, maxChars=10)
             │   去掉 cur 头部与 prev 尾部重叠的重复文字
             ├─ newText = mergedText.substr(text1.size())
             └─ callback(PRECISE)

停止:
t=stop   AasJobStop → Stop
         ├─ mIsStop = true
         ├─ AasThreadFunc 检测停止+无音频:
         │   ├─ OfflineLastRun（实时线滑动窗口尾部段冲刷）
         │   └─ FlushHybridQwenBuffer（精确线剩余缓冲, <1s 丢弃）
         └─ 等待所有在途 HybridQwenTask future 完成
```

---

## 六、计时语义说明

当前版本（独立 asr 项目）的计时设计：

1. **纯处理耗时 `BmsInfo.mProcessLatencyMs`**：从 `PrepareTaskData` 之后、`RunAasSynchronous`（或 `HybridQwenTask`）调用之前记录 `steady_clock` 起始点，推理返回后计算差值写入 `mProcessLatencyMs`。覆盖 VAD→特征→ASR→PUNC→SV→AGG 全链路，但不包含音频重采样（在计时点之前）和回调本身。

2. **HYBRID 双线独立计时**：实时线和精确线各自有独立的 `procStart`/`mProcessLatencyMs`，互不干扰。精确线由于异步执行，其 `mProcessLatencyMs` 反映的是 qwen DAG 的纯处理时间，不含在 BatchExecutor 队列中的等待时间。

3. **已移除 `PushTimeline`**：旧版本通过推送时刻回查计算延迟，现已删除。当前全部依赖 `mProcessLatencyMs`，回调端直接读取即可。

4. **测试端统计**：`flow_transcribe_test.cpp` 按 `resultSource` 分组统计 REALTIME/PRECISE 的 n/avg/p50/p95/max，写入输出文件尾部 `[STAT]` 段。

---

## 七、配置项速查（`config/config.yaml`）

| 段.键 | 默认值 | 作用 |
|---|---|---|
| `aas.sample_rate` | 16000 | 目标采样率 |
| `aas.channels` | 1 | 目标通道数 |
| `aas.bit_depth` | 32 | 目标位深 |
| `aas.paraformer_max_segment_ms` | 15000 | paraformer 单段上限 |
| `aas.paraformer_overlap_ms` | 3000 | paraformer 段间重叠 |
| `aas.paraformer_merge_max_chars` | 10 | 段间文字合并最大字符 |
| `aas.qwen_chunk_ms` | 30000 | qwen 单段上限（纯 QWEN 模式） |
| `aas.qwen_overlap_ms` | 3000 | qwen 段间重叠 |
| `aas.hybrid_qwen_chunk_ms` | 30000 | HYBRID 精确线累积触发时长 |
| `dag.compute_threads` | 10 | DAG 执行线程池大小 |
| `models.asr.tpu_id` | 0 | paraformer TPU 设备 ID |
| `models.qwen3_asr.tpu_id` | 0 | qwen3-asr TPU 设备 ID |
