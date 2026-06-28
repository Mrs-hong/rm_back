# LevelDB 项目总体架构与模块设计深度分析报告

> 本文档基于 `doc/` 目录下的官方设计文档（`index.md`、`impl.md`、`table_format.md`、`log_format.md`）以及项目全部源码进行系统化分析，覆盖总体架构、模块设计、关键类接口与实现、值得学习的设计思想。

---

## 第 0 章 文档概览与阅读路径

### 0.1 LevelDB 项目定位

LevelDB 是 Google（作者 Jeff Dean、Sanjay Ghemawat）开源的**嵌入式持久化键值（KV）存储库**，具有以下核心特征：

- **存储模型**：基于 LSM-Tree（Log-Structured Merge-Tree）架构，写优化、顺序写磁盘
- **数据模型**：Key/Value 都是任意字节数组（`Slice`），按用户可定制的 `Comparator` 排序
- **嵌入式**：以库形式嵌入应用进程，非独立服务，无网络层
- **单进程**：同一数据库同时只能被一个进程打开（文件锁保证）
- **线程安全**：同一 `DB` 对象可被多线程并发使用，内部自动同步

### 0.2 推荐阅读路径

```
第 1 章 总体结构 → 第 2 章 公共 API → 第 3 章 数据库核心
                                  ↓
第 5 章 工具模块 ← 第 4 章 Table 模块 ← 第 6 章 平台抽象
                                  ↓
                第 7 章 环境实现 → 第 8 章 设计模式总结
                                  ↓
                第 9 章 依赖关系图 → 第 10 章 总结
```

---

## 第 1 章 项目总体结构

### 1.1 顶层目录布局

```
leveldb/
├── include/leveldb/      公共 API 契约层（15 个头文件，对外暴露）
├── db/                   数据库核心引擎（DBImpl、VersionSet、MemTable、WAL 等）
├── table/                SSTable 文件格式实现（Block、Table、迭代器组合）
├── util/                 通用工具与基础设施（Arena、Cache、CRC、编码等）
├── port/                 平台抽象层（Mutex、CondVar、条件编译）
├── helpers/memenv/       内存 Env 实现（用于单元测试）
├── benchmarks/           性能基准测试
├── doc/                  设计文档
├── issues/               已知问题记录
├── cmake/                CMake 辅助脚本
├── third_party/          第三方依赖（如 googletest）
├── CMakeLists.txt        主构建文件
├── README.md             项目说明
├── NEWS                  版本更新
└── TODO                  待办事项
```

### 1.2 构建系统要点（CMakeLists.txt）

| 特性 | 说明 |
|------|------|
| C++ 标准 | C++17 |
| 异常 | 禁用（`-fno-exceptions`），所有错误通过 `Status` 返回 |
| RTTI | 禁用（`-fno-rtti`），不依赖 `dynamic_cast` |
| 可选压缩 | Snappy、Zstd 通过 `HAVE_SNAPPY`/`HAVE_ZSTD` 条件编译 |
| 硬件加速 CRC | 可选 `port::AcceleratedCRC32C`（如 SSE4.2） |
| 线程安全静态分析 | Clang `-Wthread-safety` 配合 `port/thread_annotations.h` |
| 平台配置 | `port/port_config.h.in` 由 CMake 模板生成 |

**值得学习**：通过模板文件 `port_config.h.in` 在编译期决定平台能力，避免运行时探测；通过 `__has_include` 实现头文件级自动检测。

### 1.3 核心数据流图

#### 1.3.1 写入路径

```
        用户 API
            │
            ▼
       DB::Put / DB::Write
            │
            ▼
       WriteBatch 封装
            │
            ├──────────────────────────────┐
            ▼                              ▼
     log::Writer.AddRecord          MemTable.Add
     （顺序写 WAL 日志文件）         （写入 SkipList + Arena）
            │                              │
            ▼                              ▼
      磁盘 .log 文件              内存中的活跃 MemTable
                                         │
                                  (memtable 满时切换)
                                         │
                                         ▼
                                Immutable MemTable (imm_)
                                         │
                                  (后台线程压缩)
                                         │
                                         ▼
                              SSTable 文件（Level 0）
                                         │
                                  (后续 Compaction)
                                         │
                                         ▼
                          SSTable 文件（Level 1...N）
```

#### 1.3.2 读取路径

```
       DB::Get(key)
            │
            ▼
       MemTable.Get ─── 命中 ──→ 返回
            │ (未命中)
            ▼
       Immutable MemTable.Get ─── 命中 ──→ 返回
            │ (未命中)
            ▼
       Version.Get（current version）
            │
            ▼
       ForEachOverlapping (按层遍历)
            │
            ▼
       TableCache.FindTable
            │
            ▼
       Table::Get ─── Bloom Filter 过滤 ──→ Index Block ──→ Data Block
            │
            ▼
        Block::Iter::Seek
            │
            ▼
        返回结果（或 NotFound）
```

#### 1.3.3 后台 Compaction 路径

```
   MaybeScheduleCompaction
            │
            ▼
   BackgroundCall (后台线程)
            │
            ├─────── imm_ 非空？ ────→ CompactMemTable
            │                              │
            │                              ▼
            │                       WriteLevel0Table
            │                       （BuildTable：MemTable → SSTable）
            │
            └─────── versions_->NeedsCompaction()
                    │
                    ▼
              DoCompactionWork
                    │
                    ▼
              合并 Level L 与 Level L+1 的重叠文件
                    │
                    ▼
              生成新的 Level L+1 文件
                    │
                    ▼
              InstallCompactionResults (LogAndApply)
                    │
                    ▼
              RemoveObsoleteFiles (GC 旧文件)
```

---

## 第 2 章 公共 API 层（include/leveldb）

公共 API 是 LevelDB 与使用者的契约层，体现"对外稳定、对内封装"的设计哲学。

### 2.1 核心抽象类设计哲学

#### 2.1.1 Pimpl 惯用法（Pointer to Implementation）

多个公共类仅暴露抽象接口或最小骨架，实现细节私有化：

```cpp
// include/leveldb/db.h
class LEVELDB_EXPORT DB {
 public:
  virtual ~DB();
  static Status Open(const Options& options, const std::string& dbname,
                     DB** dbptr);
  virtual Status Put(const WriteOptions&, const Slice& key, const Slice& value) = 0;
  virtual Status Delete(const WriteOptions&, const Slice& key) = 0;
  virtual Status Get(const ReadOptions& options, const Slice& key,
                    std::string* value) = 0;
  // ...其他纯虚函数
 protected:
  DB() = default;  // 阻止直接实例化
 private:
  DB(const DB&) = delete;
  void operator=(const DB&) = delete;
};
```

`DB` 的真实实现是 `db/db_impl.h` 中的 `DBImpl`，对外不可见。同样的模式用于 `Table`、`TableBuilder`、`Cache`、`WriteBatch`（带 `Rep`）等。

**设计理由**：
- ABI 稳定：实现变更不影响头文件
- 编译隔离：用户代码无需重新编译
- 隐藏细节：防止用户依赖内部字段

#### 2.1.2 `Slice` 零拷贝值类型（include/leveldb/slice.h）

```cpp
class LEVELDB_EXPORT Slice {
 public:
  Slice() : data_(""), size_(0) {}
  Slice(const char* d, size_t n) : data_(d), size_(n) {}
  Slice(const std::string& s) : data_(s.data()), size_(s.size()) {}
  Slice(const char* s) : data_(s), size_(strlen(s)) {}
  // ...
 private:
  const char* data_;
  size_t size_;
};
```

**设计要点**：
- 仅 `const char*` + `size_t` 两个成员（16 字节），可值传递
- 不持有内存（`const` 强调不可修改外部数据）
- 支持 `std::string`、`const char*` 自动转换
- 显式 `ToString()` 转换避免意外拷贝

**值得学习**：在 KV 存储中 keys/values 可能很大，零拷贝避免每次返回都拷贝大字符串。

#### 2.1.3 `Status` 紧凑错误表示（include/leveldb/status.h）

```cpp
class LEVELDB_EXPORT Status {
 public:
  Status() noexcept : state_(nullptr) {}  // OK 路径零分配
  // ...
 private:
  const char* state_;  // nullptr 表示 OK；否则堆分配
};
```

`state_` 的内部布局（在 status.cc 中实现）：
```
[ message length (4 bytes, varint) | code (1 byte) | message bytes ]
```

**设计要点**：
- OK 状态用 `nullptr` 表示，最常见路径零内存分配
- 非 OK 时才堆分配，包含长度、code、消息
- 通过 `ok()`/`IsNotFound()`/`IsCorruption()` 等便捷方法判断
- `ToString()` 输出可读形式

**值得学习**：把"快路径"做到极致——成功路径不分配内存。

### 2.2 策略模式接口

#### 2.2.1 `Comparator`（include/leveldb/comparator.h）

```cpp
class LEVELDB_EXPORT Comparator {
 public:
  virtual ~Comparator();
  virtual int Compare(const Slice& a, const Slice& b) const = 0;
  virtual const char* Name() const = 0;
  virtual void FindShortestSeparator(std::string* start,
                                     const Slice& limit) const = 0;
  virtual void FindShortSuccessor(std::string* key) const = 0;
};
```

**设计理由**：
- `Name()` 用于校验打开数据库时的兼容性（名字改变则打开失败）
- `FindShortestSeparator` 用于缩短 SSTable 索引键——给定 `start` 与 `limit`，将 `start` 缩短为不小于原值且小于 `limit` 的最短字符串，减少索引体积
- 默认实现 `BytewiseComparator`（util/comparator.cc）使用 `NoDestructor` 单例

#### 2.2.2 `FilterPolicy`（include/leveldb/filter_policy.h）

```cpp
class LEVELDB_EXPORT FilterPolicy {
 public:
  virtual ~FilterPolicy();
  virtual const char* Name() const = 0;
  virtual void CreateFilter(const Slice* keys, int n, std::string* dst) const = 0;
  virtual bool KeyMayMatch(const Slice& key, const Slice& filter) const = 0;
};
```

**设计理由**：
- 抽象过滤策略，默认实现 `NewBloomFilterPolicy(bits_per_key)`（util/bloom.cc）
- `CreateFilter` 一次处理多个 keys（写 SSTable 时按 block 批量构建）
- `KeyMayMatch` 可能假阳性，但绝不能假阴性——避免漏判

#### 2.2.3 `Env`（include/leveldb/env.h）

`Env` 是 LevelDB 与操作系统交互的统一抽象层：

```cpp
class LEVELDB_EXPORT Env {
 public:
  virtual Status NewSequentialFile(const std::string& fname,
                                  SequentialFile** result) = 0;
  virtual Status NewRandomAccessFile(const std::string& fname,
                                     RandomAccessFile** result) = 0;
  virtual Status NewWritableFile(const std::string& fname,
                                 WritableFile** result) = 0;
  virtual bool FileExists(const std::string& fname) = 0;
  virtual Status GetChildren(const std::string& dir,
                            std::vector<std::string>* result) = 0;
  virtual Status DeleteFile(const std::string& fname) = 0;
  virtual Status CreateDir(const std::string& name) = 0;
  virtual Status DeleteDir(const std::string& name) = 0;
  virtual Status RenameFile(const std::string& src,
                            const std::string& dest) = 0;
  virtual Status LockFile(const std::string& fname, FileLock** lock) = 0;
  virtual Status UnlockFile(FileLock* lock) = 0;
  virtual void Schedule(void (*function)(void* arg), void* arg) = 0;
  virtual void StartThread(void (*function)(void* arg), void* arg) = 0;
  virtual Status GetTestDirectory(std::string* path) = 0;
  virtual Status NewLogger(const std::string& fname, Logger** result) = 0;
  virtual uint64_t NowMicros() = 0;
  virtual void SleepForMicroseconds(int micros) = 0;
  // ...
};
```

**设计理由**：
- 平台无关：`util/env_posix.cc`、`util/env_windows.cc` 各自实现
- 可定制：用户可继承 `EnvWrapper` 装饰器注入行为（如限速、注入错误测试）
- 集中调度：所有文件操作、后台线程、计时统一从这里走，便于追踪

#### 2.2.4 `Cache`（include/leveldb/cache.h）

```cpp
class LEVELDB_EXPORT Cache {
 public:
  virtual ~Cache();
  virtual Handle* Insert(const Slice& key, void* value, size_t charge,
                         void (*deleter)(const Slice& key, void* value)) = 0;
  virtual Handle* Lookup(const Slice& key) = 0;
  virtual void Release(Handle* handle) = 0;
  virtual void* Value(Handle* handle) = 0;
  virtual void Erase(const Slice& key) = 0;
  virtual uint64_t NewId() = 0;
  virtual void Prune() = 0;
  virtual size_t TotalCharge() const = 0;
};
```

**设计理由**：
- 引用计数（`Insert` 返回 `Handle`，`Release` 减引用）
- `deleter` 回调机制，让用户控制 value 的释放
- `NewId()` 用于生成唯一 ID（如分配 Iterator ID）

### 2.3 迭代器抽象（include/leveldb/iterator.h）

```cpp
class LEVELDB_EXPORT Iterator {
 public:
  Iterator() : cleanup_(nullptr) {}
  virtual ~Iterator();
  virtual bool Valid() const = 0;
  virtual void SeekToFirst() = 0;
  virtual void SeekToLast() = 0;
  virtual void Seek(const Slice& target) = 0;
  virtual void Next() = 0;
  virtual void Prev() = 0;
  virtual Slice key() const = 0;
  virtual Slice value() const = 0;
  virtual Status status() const = 0;
  void RegisterCleanup(CleanupFunction function, void* arg1, void* arg2);
  // ...
};
```

**设计要点**：
- 纯虚接口，多种实现：`BlockIter`、`MergingIterator`、`TwoLevelIterator`、`DBIter`、`MemTableIterator`、`ConcatenatingIterator`
- `RegisterCleanup` 提供资源清理钩子链表（在 `~Iterator()` 中遍历调用）——见 `table/iterator.cc` 的单链表 + 内联 head 节点实现，避免每次都堆分配
- 工厂函数 `NewEmptyIterator` / `NewErrorIterator`（util/testutil.cc）便于错误传播

### 2.4 C 绑定（include/leveldb/c.h）

提供 C ABI 兼容层，便于其他语言（Python、Go、Rust 等）通过 FFI 调用：

```c
leveldb_t* leveldb_open(const leveldb_options_t* options,
                        const char* name, char** errptr);
leveldb_writebatch_t* leveldb_writebatch_create(void);
void leveldb_writebatch_put(leveldb_writebatch_t*, const char* key, size_t keylen,
                            const char* val, size_t vallen);
```

**设计要点**：
- 不透明指针（`leveldb_t*`）
- `char** errptr` 错误约定（NULL 表示成功）
- 稳定 ABI，便于 JNI/共享库导出

---

## 第 3 章 数据库核心层（db/）

### 3.1 `DBImpl` —— 系统中枢（db/db_impl.h、db/db_impl.cc）

`DBImpl` 是 `DB` 接口的唯一实现，是整个 LevelDB 的协调中枢。

#### 3.1.1 核心成员（节选）

```cpp
class DBImpl : public DB {
 private:
  Env* const env_;
  const InternalKeyComparator internal_comparator_;
  const InternalFilterPolicy internal_filter_policy_;
  const Options options_;
  const std::string dbname_;
  TableCache* const table_cache_;        // SSTable 文件缓存
  FileLock* db_lock_;                     // 文件锁，防多进程打开

  port::Mutex mutex_;                     // 保护以下所有状态
  std::atomic<bool> shutting_down_;
  port::CondVar background_work_finished_signal_;
  MemTable* mem_;                         // 当前活跃 MemTable
  MemTable* imm_ GUARDED_BY(mutex_);      // 待压缩的 Immutable MemTable
  std::atomic<bool> has_imm_;             // 让后台线程快速检测 imm_ 非空
  WritableFile* logfile_;
  log::Writer* log_;                      // WAL 日志 writer
  std::deque<Writer*> writers_;           // 等待 group commit 的 Writer 队列
  SnapshotList snapshots_;                // MVCC 快照链表
  VersionSet* const versions_;
  // ...
};
```

#### 3.1.2 `Recover()` —— 启动恢复

`DBImpl::Recover` 三阶段流程：

1. **新建 DB**（若不存在）：写初始 MANIFEST
2. **恢复 VersionSet**：`versions_->Recover()` 读 CURRENT → MANIFEST
3. **回放 WAL 日志**：`RecoverLogFile` 读取所有 .log 文件，将记录重放到新 MemTable

#### 3.1.3 `Write()` —— Group Commit

```cpp
Status DBImpl::Write(const WriteOptions& options, WriteBatch* updates) {
  Writer w(&mutex_, options, updates, sync_options);
  writers_.push_back(&w);
  while (writers_.front() != &w) {
    w.cv.Wait();                          // 非队首等待
  }
  // 队首执行 group commit
  Status status = MakeRoomForWrite(updates == nullptr);  // 触发 MemTable 切换
  WriteBatch* updates_to_write = nullptr;
  if (status.ok()) {
    updates_to_write = BuildBatchGroup(&last_writer);   // 合并多个 batch
    // 写 WAL
    status = log_->AddRecord(WriteBatchInternal::Contents(updates_to_write));
    // 写 MemTable
    WriteBatchInternal::Insert(updates_to_write, mem_);
  }
  // 唤醒下一个 Writer
}
```

**设计要点**：
- **Group commit**：通过 `BuildBatchGroup` 合并队列中多个 Writer 的 batch，一次 WAL 写入 + 一次 MemTable 写入，摊薄 I/O 成本
- **L0 流控**：`MakeRoomForWrite` 根据 L0 文件数控制写入速度
  - `kL0_SlowdownWritesTrigger = 8`：sleep 1ms
  - `kL0_StopWritesTrigger = 12`：等待 Compaction
  - imm_ 未压缩完则等待

#### 3.1.4 `Get()` —— 读路径

```cpp
Status DBImpl::Get(const ReadOptions& options, const Slice& key, std::string* value) {
  // 1. 创建 LookupKey（含 sequence）
  // 2. 检查 mem_（带锁引用计数）
  // 3. 检查 imm_（带锁引用计数）
  // 4. 调用 current_->Get（释放锁后查 SSTable）
  //    - 内部走 Version::ForEachOverlapping → TableCache → Table::Get → Bloom + Index + Data Block
}
```

**值得学习**：读取时通过引用计数短暂持有 `MemTable` 指针，避免被并发 Compaction 释放。

### 3.2 `VersionSet` / `Version` / `Compaction` —— 多版本元数据

#### 3.2.1 `Version` —— 一个一致性视图（db/version_set.h）

```cpp
class Version {
 private:
  VersionSet* vset_;
  Version* next_, *prev_;                 // 双向链表
  int refs_;                              // 引用计数
  std::vector<FileMetaData*> files_[config::kNumLevels];  // 7 层文件列表
  FileMetaData* file_to_compact_;         // 基于 seek 统计的待压缩文件
  int file_to_compact_level_;
  double compaction_score_;               // 基于大小的压缩评分
  int compaction_level_;
};
```

**设计要点**：
- 引用计数（`Ref`/`Unref`），活跃 Iterator 持有引用，不被并发 Compaction 删除
- 双向链表挂在 `VersionSet::dummy_versions_` 上，便于遍历所有版本
- `files_[7]`：L0 可能重叠，L1+ 各文件 key 范围不重叠且有序

#### 3.2.2 `VersionSet` —— 版本集合（db/version_set.h）

```cpp
class VersionSet {
 private:
  Env* const env_;
  const std::string dbname_;
  TableCache* const table_cache_;
  const InternalKeyComparator icmp_;
  uint64_t next_file_number_;
  uint64_t manifest_file_number_;
  uint64_t last_sequence_;
  uint64_t log_number_;
  WritableFile* descriptor_file_;        // MANIFEST 文件
  log::Writer* descriptor_log_;
  Version dummy_versions_;                // 循环链表头
  Version* current_;                       // 当前版本
  std::string compact_pointer_[config::kNumLevels];  // 每层上次压缩结束 key
};
```

**关键方法**：
- `LogAndApply(VersionEdit* edit, port::Mutex* mu)`：原子写 MANIFEST 增量 + 切 CURRENT 文件
- `Recover()`：从 MANIFEST 增量恢复 Version
- `Finalize(Version* v)`：计算每层 compaction 评分
  - L0：`files / kL0_CompactionTrigger(4)`
  - L1+：`bytes / 10^level MB`
- `PickCompaction()`：选层 + inputs
- `AddBoundaryInputs`：处理 user_key 边界（防止数据丢失）

#### 3.2.3 `Compaction` —— 压缩任务（db/version_set.h）

```cpp
class Compaction {
 private:
  int level_;
  uint64_t max_output_file_size_;
  Version* input_version_;
  VersionEdit edit_;
  std::vector<FileMetaData*> inputs_[2];    // L 和 L+1 的输入
  std::vector<FileMetaData*> grandparents_;  // L+2 的文件（用于切分控制）
  size_t grandparent_index_;
  int64_t overlapped_bytes_;
  size_t level_ptrs_[config::kNumLevels];
};
```

**关键方法**：
- `IsTrivialMove()`：若 L+1 无重叠 + grandparent 重叠 < 10×max_file_size，直接移动文件，不重写
- `ShouldStopBefore(internal_key)`：若与 grandparent 重叠超过 `MaxGrandParentOverlapBytes`（10×max_file_size），切新输出文件——避免下次 compaction 工作量过大
- `IsBaseLevelForKey(user_key)`：判断该 key 在更高级别是否有数据，用于决定是否可丢弃 deletion marker

### 3.3 Internal Key 格式（db/dbformat.h）

LevelDB 内部用 `InternalKey` 区分同一 user_key 的多个版本：

```
InternalKey = user_key (变长) + tag (8 字节)
tag = (sequence << 8) | type
type ∈ { kTypeValue=1, kTypeDeletion=2 }
```

```cpp
struct ParsedInternalKey {
  Slice user_key;
  SequenceNumber sequence;
  ValueType type;
};
```

**`InternalKeyComparator`** 排序规则：
- 先按 user_key 升序
- 同 user_key 按 sequence 降序（新版本在前）

**`LookupKey`** 优化：内部 200 字节内联缓冲，小 key 避免堆分配。

**设计理由**：MVCC 多版本共存，读时按 snapshot sequence 取最新可见版本。

### 3.4 `MemTable` + `SkipList` —— 内存写入引擎

#### 3.4.1 `MemTable`（db/memtable.h、db/memtable.cc）

```cpp
class MemTable {
 private:
  SkipList<const char*, KeyComparator> table_;   // 底层数据结构
  Arena arena_;                                   // 内存分配器
  std::atomic<int> refs_;
};
```

`Add` 编码格式：
```
[key_size varint32 | user_key bytes | tag uint64 | value_size varint32 | value bytes]
```

**设计要点**：
- 用 `Arena` 分配内存，避免每条记录单独 `new`
- 引用计数防止被并发释放
- `Get` 用 `LookupKey.memtable_key()` 进行 `SkipList::Seek`

#### 3.4.2 `SkipList`（db/skiplist.h）

```cpp
template <typename Key, class Comparator>
class SkipList {
 private:
  static const int kMaxHeight = 12;
  static const int kBranching = 4;       // 1/4 概率提升高度
  Node* head_;
  std::atomic<int> max_height_;
  Arena* const arena_;
};
```

**值得学习的并发设计**：
- **无锁读**：读操作不加锁，靠 `std::atomic` 的 acquire/release 语义发布节点
- **单写多读**：写需要外部 `mutex_` 互斥，但读无锁并发
- `max_height_` 用 `relaxed` 读：即使读到陈旧值也安全（只是少用几层索引）
- `Prev` 通过 `FindLessThan` 实现（节点无 prev 指针）

### 3.5 WAL 日志（db/log_format.h、db/log_writer.h、db/log_reader.h）

#### 3.5.1 文件格式

```
每个 .log 文件 = 一系列 32KB block
每个 block = 一系列 record + 可能的 trailer（≤6 字节，全 0）

record 格式（7 字节 header + 数据）：
  checksum (4 bytes, crc32c, little-endian)
  length (2 bytes, little-endian)
  type (1 byte)  // FULL=1, FIRST=2, MIDDLE=3, LAST=4
  data (length bytes)
```

#### 3.5.2 `log::Writer`（db/log_writer.cc）

`AddRecord` 自动处理跨 block 分片：
- 单 record 完整放入 → FULL
- 跨 block：FIRST（首片）→ MIDDLE（中片，0 个或多个）→ LAST（尾片）

#### 3.5.3 `log::Reader`（db/log_reader.cc）

`ReadRecord` 重组分片，遇错跳到下一 block 边界恢复。

**设计理由**：
- 固定 32KB block 便于对齐和恢复
- 分片类型让大 record 也能写入，无需大缓冲
- CRC32C 校验每条 record，保证完整性

### 3.6 文件命名（db/filename.h、db/filename.cc）

```
000123.log             — WAL 日志
MANIFEST-000001        — 描述符（MANIFEST）
CURRENT                — 文本，指向当前 MANIFEST 文件名
LOCK                   — 文件锁
000123.ldb             — SSTable 文件
LOG / LOG.old          — 信息日志
```

**`FileType` 枚举**区分类型，`ParseFileName` 解析文件名。

**值得学习**：用文件名编码版本号，便于人眼检查和恢复。

### 3.7 `Snapshot` —— MVCC（db/snapshot.h）

```cpp
class SnapshotImpl : public Snapshot {
 public:
  SequenceNumber sequence_number_;
 private:
  SnapshotImpl* prev_;
  SnapshotImpl* next_;
  // 双向循环链表
};
```

**设计要点**：
- `SnapshotList` 由 `mutex_` 保护
- `GetSnapshot()` 当前 `last_sequence_` 加入链表
- `ReleaseSnapshot()` 移除并允许 Compaction 回收旧版本
- 读时用 snapshot 的 sequence 过滤掉更新的版本

### 3.8 `WriteBatch`（include/leveldb/write_batch.h、db/write_batch.cc）

格式：
```
[sequence fixed64 | count fixed32 | records...]
record = [type | key varint | (value varint if type=kTypeValue)]
```

**`Handler` 模式**：
```cpp
class WriteBatch::Handler {
 public:
  virtual void Put(const Slice& key, const Slice& value) = 0;
  virtual void Delete(const Slice& key) = 0;
};
virtual Status WriteBatch::Iterate(Handler* handler) const;
```

**`MemTableInserter`**（db/write_batch.cc）实现 `Handler`，将 Put/Delete 转为 `MemTable::Add`。

**`WriteBatchInternal`**（db/write_batch_internal.h）暴露内部访问（设 sequence/count），仅供 db/ 内部使用。

### 3.9 `TableCache` / `DBIter` / `Builder`

#### 3.9.1 `TableCache`（db/table_cache.h、db/table_cache.cc）

```cpp
class TableCache {
 private:
  Env* const env_;
  const Options& options_;
  Cache* const cache_;        // 复用 Options.block_cache 或自建
};
```

- `FindTable(file_number, file_size, TableAndFile**)`：从 LRU cache 取或打开
- `NewIterator` / `Get`：返回 Table 的迭代器或查 Get
- `Evict(file_number)`：在 SSTable 被删除后从 cache 移除
- `UnrefEntry` 作为 Iterator cleanup 回调

#### 3.9.2 `DBIter`（db/db_iter.h、db/db_iter.cc）

将 internal key 迭代器转换为 user key 迭代器：
- `FindNextUserEntry` 跳过 deletion marker 和旧版本（sequence > snapshot 的不可见版本）
- `bytes_until_read_sampling_` 每隔约 1MB 采样一次，触发 seek-based compaction

#### 3.9.3 `builder.cc::BuildTable`（db/builder.h、db/builder.cc）

将 MemTable 内容构建为 SSTable 文件：
1. 创建 `TableBuilder`
2. 遍历 MemTable 迭代器，逐条 `table_builder->Add`
3. `table_builder->Finish()`，记录文件元数据到 `VersionEdit`

---

## 第 4 章 Table 模块（table/）

### 4.1 SSTable 文件格式（table/format.h）

```
<beginning_of_file>
[data block 1]
[data block 2]
...
[data block N]
[meta block 1]    // 如 filter block
...
[meta block K]
[metaindex block]  // 每个 meta block 一个 entry
[index block]      // 每个 data block 一个 entry
[Footer]           // 固定大小（footer_size = 2*BlockHandle::kMaxEncodedLength + 8）
<end_of_file>
```

**Footer 结构**：
```
metaindex_handle: BlockHandle (varint offset + varint size)
index_handle:     BlockHandle
padding:          zeroed bytes
magic:            fixed64 = 0xdb4775248b80fb57 (little-endian)
```

**BlockHandle**：`offset + size`，varint64 编码。

**Block trailer**（5 字节）：`type(1 byte, kNoCompression/kSnappyCompression/kZstdCompression) + crc(4 bytes, crc32c)`。

### 4.2 Block 编码（table/block.h、table/block_builder.cc）

#### 4.2.1 前缀压缩 + Restart Array

每个 entry：
```
<shared bytes len varint> <non-shared bytes len varint> <value size varint>
<non-shared key bytes> <value bytes>
```

- 相邻 entry 的公共前缀只存一次
- 每 `block_restart_interval`（默认 16）个 entry 写一个 restart point（完整 key）
- restart point 数组放在 block 末尾，便于二分查找

#### 4.2.2 `Block::Iter`（table/block.cc）

```cpp
class Block::Iter : public Iterator {
 private:
  const Block* block_;
  uint32_t restart_index_;
  // ...
  bool DecodeEntry(Slice* entry, uint32_t* shared, uint32_t* non_shared, uint32_t* value_length);
};
```

**`DecodeEntry` 快速路径**：3 个 varint 都 < 128 时直接读 3 字节，跳过 varint 循环。

**`Seek` 实现**：先在 restart array 二分定位大致区间，再线性扫描。

### 4.3 `Table` 与 `TableBuilder`

#### 4.3.1 `Table::Open`（table/table.cc）

读 footer → 读 index block → 暴露 `Get` / `NewIterator` 接口。

#### 4.3.2 `TableBuilder`（table/table_builder.cc）

```cpp
class TableBuilder {
 private:
  BlockBuilder data_block_;
  BlockBuilder index_block_;
  FilterBlockBuilder* filter_block_;
  bool pending_index_entry_;       // 关键优化
  BlockHandle pending_handle_;
  // ...
};
```

**`pending_index_entry` 优化**：当当前 data block 结束时，不立即写 index entry——而是等下一个 `Add(key, value)` 拿到下一个 block 的首 key 后，用 `Comparator::FindShortestSeparator` 缩短 index key：

```cpp
void TableBuilder::Add(const Slice& key, const Slice& value) {
  if (pending_index_entry_) {
    options_.comparator->FindShortestSeparator(&pending_index_entry_key, key);
    index_block_.Add(pending_index_entry_key_, pending_handle_);
    pending_index_entry_ = false;
  }
  // ...
}
```

**值得学习**：用下一个 key 缩短当前 index key，减少索引体积且不影响查询正确性。

### 4.4 迭代器组合体系

#### 4.4.1 `MergingIterator`（table/merger.cc）

多路归并：内部维护多个子迭代器 + `current_` 指针（指向当前最小/最大 key 的迭代器）。

#### 4.4.2 `TwoLevelIterator`（table/two_level_iterator.cc）

两层结构：`index_iter` 遍历 index block，每个 entry 通过 `block_function` 打开对应的 data block 迭代器 `data_iter`。

`SkipEmptyDataBlocks` 在 data_iter 失效时自动 Seek 到下一个 index entry。

**用途**：Table 的 index→data block，Version 的 level→file 都用此结构。

#### 4.4.3 `IteratorWrapper`（table/iterator_wrapper.h）

```cpp
class IteratorWrapper : public Iterator {
 private:
  Iterator* iter_;
  bool valid_;
  Slice key_;
};
```

缓存 `valid_` 和 `key_`，避免每次调用虚函数。**值得学习**：在 MergingIterator 这种频繁比较的场景，缓存关键状态能显著降低开销。

#### 4.4.4 `iterator.cc::RegisterCleanup`

清理钩子用单链表 + 内联 head 节点（`cleanup_` 头直接存在 Iterator 对象内）：

```cpp
struct Cleanup {
  CleanupFunction function;
  void* arg1;
  void* arg2;
  Cleanup* next;
};
Cleanup cleanup_;        // 头节点，避免每次都堆分配
bool cleanup_head_overridden_;
```

**值得学习**：默认内联头节点，仅在用户调用 `RegisterCleanup` 时分配堆节点——快路径零分配。

### 4.5 Filter Block（table/filter_block.h、table/filter_block.cc）

```cpp
static const size_t kFilterBaseLg = 11;        // 2KB
static const size_t kFilterBase = 1 << kFilterBaseLg;
```

- 每个 data block 按其 offset 归属到 `offset >> 11` 对应的 filter
- filter block 末尾存 offset 数组 + lg(base)
- `KeyMayMatch(key, filter_offset)` 时根据 data block 的 offset 找到对应 filter，调用 `FilterPolicy::KeyMayMatch`

---

## 第 5 章 工具模块（util/）

### 5.1 `Arena` 内存池（util/arena.h、util/arena.cc）

```cpp
class Arena {
 private:
  std::vector<char*> blocks_;
  char* alloc_ptr_;
  size_t alloc_bytes_remaining_;
  std::atomic<size_t> memory_usage_;
};
```

**设计要点**：
- 默认按 4KB block 分配，剩余空间不足时新申请 block
- `AllocateAligned` 处理对齐需求（SkipList 节点需对齐）
- `memory_usage_` 原子计数，便于监控
- `AllocateFallback` 处理大块（>= 1KB 时直接单独分配）

**值得学习**：arena 模式大幅减少 `malloc` 调用，且内存碎片低，析构时一次释放所有 block。

### 5.2 `cache.cc` —— LRU 缓存

#### 5.2.1 `LRUHandle`

```cpp
struct LRUHandle {
  void* value;
  void (*deleter)(const Slice&, void*);
  LRUHandle* next_hash;     // HandleTable 链表
  LRUHandle* next;          // LRU 双向链表
  LRUHandle* prev;
  size_t charge;
  size_t key_length;
  bool in_cache_entry;      // 是否在 cache 中
  uint32_t refs;
  uint32_t hash;
  char key_data[1];         // 柔性数组
  Slice key() const { return Slice(key_data, key_length); }
};
```

**柔性数组 `key_data[1]`**：让 key 紧跟 handle 之后，一次分配两块内存，提高缓存局部性。

#### 5.2.2 `HandleTable`（自定义哈希表）

```cpp
class HandleTable {
 private:
  LRUHandle** list_;        // bucket 数组
  uint32_t length_;
  uint32_t elems_;
  LRUHandle* FindPointer(const Slice& key, uint32_t hash);
};
```

**值得学习**：
- 自实现而非 `std::unordered_map`，避免迭代器失效和额外间接
- `elems_ > length_` 时 resize 到 2 倍并 rehash，保持平均链长 ≤ 1

#### 5.2.3 `LRUCache`

维护两条双向链表：
- `in_use_`：当前被引用（refs > 1）
- `lru_`：未被引用（refs == 1），可驱逐

#### 5.2.4 `ShardedLRUCache`

```cpp
static const int kNumShards = 16;        // 16 个分片
class ShardedLRUCache {
 private:
  LRUCache* shard_[kNumShards];
};
```

**值得学习**：16 个分片将锁竞争降为 1/16，多线程下吞吐显著提升。`Shard(hash)` 用高位 bit 分片，避免与 `HandleTable` 低位 hash 冲突。

### 5.3 `coding.h` —— Varint 编码

```cpp
char* EncodeVarint32(char* dst, uint32_t v);
const char* GetVarint32Ptr(const char* p, const char* limit, uint32_t* v);
```

**`GetVarint32Ptr` 内联快速路径**：
```cpp
if ((v & 128) == 0) { *v = v; return p + 1; }      // 单字节
if (p + 1 < limit && (p[1] & 128) == 0) { ... }     // 双字节
// ...
```

**值得学习**：变长编码节省空间（小整数 1 字节）；快速路径优化常见情况。

### 5.4 `crc32c` —— 循环冗余校验（util/crc32c.h、util/crc32c.cc）

#### 5.4.1 `Mask` / `Unmask`

```cpp
uint32_t Mask(uint32_t crc) {
  return ((crc >> 15) | (crc << 17)) + 0xa282ead8;
}
uint32_t Unmask(uint32_t masked_crc) {
  // 逆运算
}
```

**设计理由**：避免存储的 CRC 与从存储读取的 CRC 再次校验产生嵌套问题——存的是 `Mask(crc)`，校验时算 `Mask(actual_crc)` 比对。

#### 5.4.2 `Extend` —— stride 优化

```cpp
static const uint32_t kStrideExtensionTable0[256];
static const uint32_t kStrideExtensionTable1[256];
static const uint32_t kStrideExtensionTable2[256];
static const uint32_t kStrideExtensionTable3[256];
```

每次处理 16 字节（4×4），用 4 个 stride 表并行处理——比逐字节快 4 倍。可选 `port::AcceleratedCRC32C` 走硬件（SSE4.2 CRC32 指令）。

### 5.5 `bloom.cc` —— Bloom Filter

```cpp
static uint32_t BloomHash(const Slice& key) {
  return Hash(key.data(), key.size(), 0xbc9f1d34);
}
class BloomFilterPolicy : public FilterPolicy {
 private:
  size_t bits_per_key_;
  size_t k_;             // 探测次数
};
```

**双哈希优化（Kirsch/Mitzenmacher）**：用两个独立哈希模拟 k 个：
```
h_i(x) = h1(x) + i * h2(x)  (i = 0..k-1)
```

LevelDB 简化：
```cpp
uint32_t h = BloomHash(key);
const uint32_t delta = (h >> 17) | (h << 15);
for (size_t j = 0; j < k_; j++) {
  const uint32_t bitpos = h % bits;
  array[bitpos/8] |= (1 << (bitpos % 8));
  h += delta;
}
```

**参数选择**：
- `k_ = bits_per_key * 0.69`（ln2，最优）
- 最多 30 次探测
- filter 末尾存 k 值，便于不同参数兼容读取

### 5.6 `hash.cc` —— Murmur-like Hash

```cpp
uint32_t Hash(const char* data, size_t n, uint32_t seed) {
  const uint32_t m = 0xc6a4a793;
  const uint32_t r = 24;
  uint32_t h = seed ^ (n * m);
  // 4 字节一组
  while (n >= 4) { /* mix */ }
  // 尾部 switch 处理剩余
  return h;
}
```

### 5.7 其他工具

#### 5.7.1 `no_destructor.h`

```cpp
template <typename InstanceType>
class NoDestructor {
 public:
  template <typename... ConstructorArgTypes>
  explicit NoDestructor(ConstructorArgTypes&&... constructor_args) {
    new (instance_storage_) InstanceType(...);
  }
  ~NoDestructor() = default;     // 不析构！
 private:
  alignas(InstanceType) char instance_storage_[sizeof(InstanceType)];
};
```

**设计理由**：函数级静态变量在程序退出时析构顺序不可控，可能引发崩溃。`NoDestructor` 跳过析构，让 OS 在进程退出时统一回收。

**用途**：`BytewiseComparator`、`Env::Default()` 等单例。

#### 5.7.2 `mutexlock.h`

```cpp
class SCOPED_LOCKABLE MutexLock {
 public:
  explicit MutexLock(port::Mutex* mu) EXCLUSIVE_LOCK_FUNCTION(mu) : mu_(mu) {
    mu_->Lock();
  }
  ~MutexLock() UNLOCK_FUNCTION() { mu_->Unlock(); }
 private:
  port::Mutex* const mu_;
};
```

RAII + Clang `SCOPED_LOCKABLE` 标注，配合 `-Wthread-safety` 编译期检测。

#### 5.7.3 `histogram.h`、`random.h`

- `Histogram`：154 个 bucket 的对数分桶，`Percentile` 线性插值，用于 `db_bench`
- `Random`：Park-Miller 乘同余（A=16807, M=2^31-1），`Skewed` 偏小采样

---

## 第 6 章 平台抽象层（port/）

### 6.1 `port_stdcxx.h`

```cpp
class Mutex {
 public:
  Mutex() = default;
  void Lock() { mu_.lock(); }
  void Unlock() { mu_.unlock(); }
  void AssertHeld() { }
 private:
  std::mutex mu_;
};

class CondVar {
 public:
  explicit CondVar(Mutex* mu) : mu_(mu) {}
  void Wait() {
    std::unique_lock<std::mutex> lock(mu_->mu_, std::adopt_lock);
    cv_.wait(lock);
    lock.release();                // 不释放，让 Mutex::Unlock 处理
  }
  void Signal() { cv_.notify_one(); }
  void SignalAll() { cv_.notify_all(); }
 private:
  std::condition_variable cv_;
  Mutex* const mu_;
};
```

**值得学习**：`Wait` 用 `adopt_lock` 配合外部已锁的 mutex，`release()` 防止析构时 unlock——优雅地复用 `std::condition_variable` 而不破坏 `Mutex` 抽象。

#### 条件编译

```cpp
#if HAVE_CRC32C
extern uint32_t AcceleratedCRC32C(uint32_t crc, const char* buf, size_t size);
#endif
#if HAVE_SNAPPY
bool Snappy_Compress(...) { ... }
bool Snappy_Uncompress(...) { ... }
#endif
#if HAVE_ZSTD
bool Zstd_Compress(...) { ... }
bool Zstd_Uncompress(...) { ... }
#endif
```

### 6.2 `thread_annotations.h`

```cpp
#ifdef __clang__
#define GUARDED_BY(x) __attribute__((guarded_by(x)))
#define EXCLUSIVE_LOCKS_REQUIRED(...) __attribute__((exclusive_locks_required(__VA_ARGS__)))
#define SCOPED_LOCKABLE __attribute__((scoped_lockable))
// ... 等等
#else
#define GUARDED_BY(x)
#define EXCLUSIVE_LOCKS_REQUIRED(...)
#define SCOPED_LOCKABLE
// ...
#endif
```

**值得学习**：编译期静态分析，能在编译阶段发现潜在死锁、未加锁访问等问题。`db_impl.h` 中所有共享状态都用 `GUARDED_BY(mutex_)` 标注。

### 6.3 `port_config.h.in`（CMake 模板）

```cmake
#cmakedefine01 HAVE_CRC32C
#cmakedefine01 HAVE_SNAPPY
#cmakedefine01 HAVE_ZSTD
#cmakedefine01 HAVE_FDATASYNC
#cmakedefine01 HAVE_FULLFSYNC
#cmakedefine01 HAVE_O_CLOEXEC
```

CMake 生成实际 `port_config.h`，通过 `__has_include` 自动检测可用性。

---

## 第 7 章 环境实现（util/env_posix.cc / util/env_windows.cc）

### 7.1 POSIX 实现（util/env_posix.cc）

#### 7.1.1 文件类

| 类 | 实现 |
|----|------|
| `PosixSequentialFile` | `read()` 顺序读 |
| `PosixRandomAccessFile` | `pread()` 随机读，可选 fd_limiter 限制打开数 |
| `PosixMmapReadableFile` | `mmap()`，受 `mmap_limiter` 限制 |
| `PosixWritableFile` | 64KB 用户态缓冲（小写缓冲、大写直写） |
| `PosixFileLock` | `fcntl(F_SETLK)` |
| `PosixLogger` | 先栈缓冲 512B 再堆 fallback |

#### 7.1.2 `PosixWritableFile::Sync`

```cpp
Status Sync() override {
  // 先 flush 用户态缓冲
  // 若是 manifest 文件，先 fsync 父目录（防崩溃后元数据丢失）
  // 调用 SyncFd：优先 fdatasync > F_FULLFSYNC > fsync
}
```

**值得学习**：manifest 文件特别处理——sync 父目录保证 rename 后元数据持久化。

#### 7.1.3 `PosixEnv` —— 后台线程池

```cpp
class PosixEnv : public Env {
 private:
  void BackgroundThreadMain() {
    while (true) {
      BGItem item;
      MutexLock lock(&bg_mutex_);
      while (bgqueue_.empty()) bg_cv_.Wait();
      item = bgqueue_.front();
      bgqueue_.pop_front();
      lock.Unlock();
      item.function(item.arg);
    }
  }
  void Schedule(void (*function)(void*), void* arg) {
    MutexLock lock(&bg_mutex_);
    bgqueue_.push_back({function, arg});
    if (!started_bgthread_) {
      started_bgthread_ = true;
      StartThread(&BackgroundThreadMain, this);
    }
    bg_cv_.Signal();
  }
};
```

**懒启动**：第一次 `Schedule` 时才创建后台线程。

**`SingletonEnv<PosixEnv>`**：用 `NoDestructor` 保证单例不析构，避免静态对象析构顺序问题。

#### 7.1.4 资源限制 `Limiter`

```cpp
class Limiter {
 public:
  Limiter(int max_acquires) : acquires_allowed_(max_acquires) {}
  bool Acquire() {
    int old = acquires_allowed_.fetch_sub(1);
    return old > 0;
  }
  void Release() { acquires_allowed_.fetch_add(1); }
 private:
  std::atomic<int> acquires_allowed_;
};
```

用于限制打开的 fd 数、mmap 数。`MaxOpenFiles` 取 `getrlimit(RLIMIT_NOFILE)/5`。

### 7.2 Windows 实现（util/env_windows.cc）

| 类 | 实现 |
|----|------|
| `ScopedHandle` | RAII 包装 `HANDLE` |
| `WindowsSequentialFile` | `ReadFile` |
| `WindowsRandomAccessFile` | `ReadFile + OVERLAPPED`（异步） |
| `WindowsMmapReadableFile` | `CreateFileMapping + MapViewOfFile` |
| `WindowsWritableFile` | 64KB 缓冲，`Sync` 用 `FlushFileBuffers`（不 sync 父目录） |
| `WindowsFileLock` | `LockFile/UnlockFile` |
| `WindowsLogger` | `GetLocalTime` 替代 `gettimeofday` |
| `WindowsEnv` | 与 `PosixEnv` 同构（后台线程 + queue） |

### 7.3 两平台对比

| 维度 | POSIX | Windows |
|------|-------|---------|
| 句柄 | int fd | HANDLE |
| 随机读 | pread / mmap | ReadFile + OVERLAPPED |
| 同步 | fdatasync / fsync / F_FULLFSYNC | FlushFileBuffers |
| 文件锁 | fcntl(F_SETLK) | LockFile |
| 父目录 sync | manifest 文件需要 | 不需要 |
| mmap | mmap() | CreateFileMapping |
| 错误码 | errno | GetLastError |
| 时间 | gettimeofday | GetLocalTime |
| 后台线程 | pthread / std::thread | std::thread |

**值得学习**：抽象一致，差异封装在实现内部，对上层完全透明。

---

## 第 8 章 关键设计模式与工程实践总结

### 8.1 设计模式

| 模式 | 应用 |
|------|------|
| **迭代器模式** | `BlockIter`、`MergingIterator`、`TwoLevelIterator`、`DBIter`、`MemTableIterator`、`ConcatenatingIterator` 多态组合 |
| **Pimpl 惯用法** | `DB`/`Table`/`TableBuilder`/`Cache`/`WriteBatch` 仅暴露最小接口 |
| **策略模式** | `Comparator`、`FilterPolicy`、`Env`、`Cache` |
| **装饰器模式** | `EnvWrapper` 装饰 `Env`，便于扩展 |
| **模板方法 + 回调** | `Iterator::RegisterCleanup`、`Version::Get` 的 `SaverState` 回调、`WriteBatch::Handler` |
| **RAII** | `MutexLock`、`ScopedHandle`、`Arena`、`FileGuard` |
| **引用计数** | `Version`、`MemTable`、`FileMetaData`、`Cache::Handle` |
| **对象池 + 延迟初始化** | `TableCache`、后台线程懒启动 |
| **柔性数组** | `LRUHandle::key_data[1]` |
| **NoDestructor 单例** | `BytewiseComparator`、`Env::Default()` |

### 8.2 并发与性能工程实践

#### 8.2.1 无锁数据结构

- `SkipList` 单写多读，靠 `std::atomic` acquire/release 发布节点
- `max_height_` relaxed 读：即使读到陈旧值也安全

#### 8.2.2 锁优化

- `ShardedLRUCache` 16 分片降锁竞争
- `TableCache` 自带锁，与 `DBImpl::mutex_` 解耦
- 读路径在 SSTable 查询时释放 `mutex_`

#### 8.2.3 写入优化

- **Group commit**：`BuildBatchGroup` 合并多个 Writer
- **L0 三档阈值流控**（4/8/12）：在内存压力与写入吞吐间平衡
- **WAL 顺序写**：避免随机 I/O
- **Arena 内存池**：减少 `malloc` 调用，降低碎片

#### 8.2.4 读取优化

- **Bloom filter**：减少不必要的磁盘读（约 100 倍降低）
- **前缀压缩 + restart array**：减少 block 体积 + 二分查找
- **block_cache**：缓存解压后的 block
- **IteratorWrapper**：缓存 `valid_`/`key_` 减少虚函数调用
- **Iterator cleanup 内联 head**：快路径零堆分配

#### 8.2.5 编码优化

- **Varint 变长编码**：小整数 1 字节，节省空间
- **InternalKey tag 压缩**：sequence + type 共 8 字节
- **`pending_index_entry`**：用下一 key 缩短索引键
- **柔性数组 `key_data[1]`**：handle 与 key 一次分配

#### 8.2.6 硬件加速

- `AcceleratedCRC32C`（SSE4.2 CRC32 指令）
- `mmap` 随机读（绕过用户态缓冲）
- `fdatasync` 优先于 `fsync`（不更新元数据，更快）

#### 8.2.7 编译期优化

- 内联快速路径（`GetVarint32Ptr`、`DecodeEntry`）
- `alignas` 对齐
- Clang `-Wthread-safety` 静态分析
- `-fno-exceptions` / `-fno-rtti` 减少 runtime 开销

### 8.3 数据完整性保障

| 机制 | 作用 |
|------|------|
| **CRC32C + Mask/Unmask** | 防止校验嵌套问题；每 block trailer 5 字节校验 |
| **WAL 分片 + record type** | 大 record 跨 block 分片重组，便于恢复 |
| **MANIFEST 增量 + CURRENT 指针** | 元数据变更以 log 形式追加，崩溃后可重放恢复 |
| **原子 rename + 父目录 sync** | CURRENT 切换通过 rename 实现，manifest 父目录 sync 保证持久化 |
| **SnapshotList MVCC** | 读不阻塞写，写不阻塞读 |
| **引用计数** | 防止活跃 Iterator 引用的资源被回收 |
| **`paranoid_checks`** | 严格模式下立即报错，而非容忍损坏 |

### 8.4 可学习的设计思想

1. **严格的层次分离**：公共 API / 引擎 / 格式 / 工具 / 平台各司其职
2. **文件格式自描述**：footer + magic number 让文件能自校验
3. **失败可恢复**：WAL + MANIFEST + 拷贝写 + 原子切换保证崩溃一致性
4. **抽象与具体解耦**：`Env` 多平台、`Comparator`/`FilterPolicy` 可定制
5. **性能关键路径内联优化**：`coding.h` 内联、`IteratorWrapper` 缓存
6. **测试友好**：`testutil`、`ErrorEnv`、`EnvPosixTestHelper`、`memenv` 便于注入测试
7. **渐进式改进**：L0 → L1 → L2... 的 LSM Tree 让数据逐步沉淀到更稳定的层
8. **快路径优化**：`Status` OK 路径零分配、`Iterator cleanup` 默认内联头
9. **资源约束显式化**：`Limiter`、`MaxOpenFiles` 防止资源耗尽
10. **静态对象禁析构**：`NoDestructor` 避免析构顺序问题

---

## 第 9 章 模块依赖关系图

```
┌─────────────────────────────────────────────────────────────┐
│                    include/leveldb/                          │
│  (公共 API 契约层：仅头文件，仅依赖 STL，零外部依赖)          │
└─────────────────────────────────────────────────────────────┘
                  ▲                  ▲                  ▲
                  │                  │                  │
        ┌─────────┴────┐      ┌──────┴─────┐      ┌─────┴─────┐
        │   util/       │      │   port/    │      │ helpers/  │
        │  (工具/基础)   │◀────│ (平台抽象)  │      │  memenv/  │
        └─────────┬────┘      └────────────┘      └───────────┘
                  │                  ▲
                  │                  │
        ┌─────────┴──────────────────┴────┐
        │           table/                 │
        │      (SSTable 文件格式)           │
        └────────────────┬────────────────┘
                         │
                         ▼
                ┌────────────────┐
                │     db/         │
                │  (数据库核心)    │
                └────────────────┘
                         │
                         ▼
        ┌─────────────────────────────────────┐
        │  util/env_posix.cc  util/env_windows.cc  │
        │       (具体平台 Env 实现)                │
        └─────────────────────────────────────┘
```

**依赖规则**：
- `include/leveldb/*` 不依赖任何内部模块（仅 STL）
- `port/` 仅依赖 STL 与系统头
- `util/` 依赖 `include/leveldb` 与 `port/`
- `table/` 依赖 `include/leveldb`、`util/`、`port/`
- `db/` 依赖 `include/leveldb`、`table/`、`util/`、`port/`
- `helpers/memenv/` 依赖 `include/leveldb`
- `util/env_posix.cc` 与 `util/env_windows.cc` 依赖 `include/leveldb`、`port/`、`util/`

**值得学习**：自底向上的依赖方向，公共契约在最底层，平台实现可独立替换。

---

## 第 10 章 总结

### 10.1 LevelDB 的核心价值

LevelDB 是 **LSM-Tree 存储引擎的教科书级实现**：
- 代码量适中（核心约 1 万行 C++），却覆盖了存储引擎的所有关键问题
- 注释清晰、设计文档完整，便于学习
- 大量应用经典设计模式，是工程实践的范例

### 10.2 优秀之处

| 维度 | 优秀点 |
|------|--------|
| **架构清晰度** | 公共 API / 引擎 / 格式 / 工具 / 平台严格分层 |
| **类型安全** | 强类型接口、`Slice` 不可变、编译期线程安全分析 |
| **性能** | 写优化（LSM + WAL + Group commit）、读优化（Bloom + 缓存 + 前缀压缩） |
| **正确性** | MVCC、引用计数、CRC 校验、崩溃恢复 |
| **可移植性** | `Env` 抽象使平台移植只需替换实现 |
| **可扩展性** | `Comparator`、`FilterPolicy`、`Cache` 可定制 |
| **可测试性** | `Env` 注入、错误注入、内存 Env |
| **代码风格** | RAII、Pimpl、内联优化、注释规范 |

### 10.3 限制与权衡

| 限制 | 说明 |
|------|------|
| **单写线程** | 写串行（虽然 group commit 部分缓解） |
| **无范围删除** | 不支持原生 range delete |
| **无事务** | 仅 WriteBatch 提供单 batch 原子性 |
| **压缩串行** | 同一时刻只跑一个 Compaction |
| **无多线程 Compaction** | 后台单线程 |

### 10.4 学习路径建议

1. **入门**：读 `doc/index.md`、`doc/impl.md`，跑 demo
2. **理解接口**：通读 `include/leveldb/*` 全部头文件
3. **理解引擎**：从 `db/db_impl.cc` 入手，跟踪 `Put`/`Get`/`Write`/`Recover`
4. **理解元数据**：读 `db/version_set.h/cc`，理解 `Version`/`VersionSet`/`Compaction`
5. **理解存储格式**：读 `table/format.h`、`table/block.cc`、`table/table_builder.cc`
6. **理解工具**：读 `util/arena.h`、`util/cache.cc`、`util/coding.h`、`util/crc32c.cc`
7. **理解平台**：读 `util/env_posix.cc`，对比 `util/env_windows.cc`
8. **实践**：写自定义 `Comparator`、自定义 `FilterPolicy`、自定义 `Env`

### 10.5 结语

LevelDB 用极其精炼的代码完整展现了 LSM-Tree 存储引擎的设计与实现，是学习存储系统、C++ 工程实践、并发编程、文件格式设计、平台抽象的绝佳范本。其设计哲学（清晰分层、契约稳定、性能与正确性并重、可扩展可定制）值得在更多系统中借鉴。

---

## 附录：关键源码文件索引

| 模块 | 关键文件 |
|------|---------|
| 公共 API | `include/leveldb/db.h`、`env.h`、`cache.h`、`comparator.h`、`filter_policy.h`、`iterator.h`、`options.h`、`slice.h`、`status.h`、`table.h`、`table_builder.h`、`write_batch.h` |
| 数据库核心 | `db/db_impl.h/cc`、`db/version_set.h/cc`、`db/version_edit.h/cc`、`db/dbformat.h/cc`、`db/memtable.h/cc`、`db/skiplist.h`、`db/log_writer.h/cc`、`db/log_reader.h/cc`、`db/log_format.h`、`db/filename.h/cc`、`db/snapshot.h`、`db/write_batch.cc`、`db/table_cache.h/cc`、`db/db_iter.h/cc`、`db/builder.h/cc` |
| Table 模块 | `table/block.h/cc`、`table/block_builder.h/cc`、`table/format.h/cc`、`table/table.cc`、`table/table_builder.cc`、`table/merger.h/cc`、`table/two_level_iterator.h/cc`、`table/filter_block.h/cc`、`table/iterator.cc`、`table/iterator_wrapper.h` |
| 工具模块 | `util/arena.h/cc`、`util/cache.cc`、`util/coding.h/cc`、`util/crc32c.h/cc`、`util/hash.h/cc`、`util/bloom.cc`、`util/logging.h/cc`、`util/histogram.h/cc`、`util/random.h`、`util/no_destructor.h`、`util/mutexlock.h`、`util/status.cc`、`util/options.cc`、`util/filter_policy.cc`、`util/comparator.cc`、`util/env.cc` |
| 平台抽象 | `port/port.h`、`port/port_stdcxx.h`、`port/thread_annotations.h`、`port/port_config.h.in` |
| 环境实现 | `util/env_posix.cc`、`util/env_windows.cc`、`util/posix_logger.h`、`util/windows_logger.h` |
| 设计文档 | `doc/index.md`、`doc/impl.md`、`doc/table_format.md`、`doc/log_format.md` |

---

> 本文档基于 LevelDB 源码与官方设计文档系统化整理，可作为学习 LSM-Tree 存储引擎的参考资料。
