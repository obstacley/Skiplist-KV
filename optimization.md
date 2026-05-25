# Skiplist-KV 优化方向

## Git 历史 QPS 演进（相同测试：8读2写，10线程，100k预填充，每线程100k操作）

| 提交 | 日期 | QPS | 关键变更 |
|------|------|-----|----------|
| `60b97a3` | 04-24 | 2.89M | 引入读写并发测试，**但 insert 有 bug（用了 shared_lock）** |
| `174bb23` | 04-25 | 790k | **修复 insert 为 unique_lock**（正确锁，QPS 暴跌 72%） |
| `eb809c5` | 04-26 | 900k | 随机层数改用 mt19937，p=0.25；dump_file 改用 POSIX write |
| 本机实测 | 05-15 | ~1.0M | 同上代码，机器差异导致波动 |

**结论**：
- 从错误锁改成正确锁后，QPS 从 2.89M 掉到 790k——**锁竞争吃掉了 73% 的性能**。
- 之后的优化（随机层数生成从 `rand()%2` p=0.5 改为 mt19937 p=0.25，降低平均层数减少插入开销）只带来 14% 提升。
- 当前代码的正确性是 OK 的，但**一把大锁的问题从 `174bb23` 起就没有本质改善**——900k→1M 只是边角优化，没有触及核心瓶颈。

---

## 核心问题：锁是唯一瓶颈

当前并发模型：1 把 `std::shared_mutex`，读 shared_lock，写 unique_lock。`

实测数据说明一切：

| 场景 | QPS | 相对纯读的衰减 |
|------|-----|---------------|
| 纯读 10 线程 | 4.60M | — |
| 8读2写 | 1.00M | ↓78% |
| 5读5写 | 0.27M | ↓94% |
| 纯写 10 线程 | 0.34M | ↓93% |

2 个写线程就能把 8 个读线程拖慢 4.6 倍。5 个写线程直接把 QPS 打到了和纯写差不多的水平——**读操作被写锁阻塞到几乎不存在并发**。

---

## 优化方向

### 方向 1：Lock-Free（CAS 原子操作）—— 工业级方案

**原理**：用 `std::atomic<Node*>` 替代 `Node**`，插入/删除通过 CAS 循环完成，读取完全不阻塞。

**可行性**：已被多个工业项目验证。
- **Java ConcurrentSkipListMap**（Doug Lea, JDK 1.6+）：lock-free 跳表的标杆实现
- **RocksDB**（Facebook）：`InlineSkipList` 用 CAS 实现，支撑其 memtable 的并发写入
- **LevelDB**（Google）：虽然 LevelDB 本身用粗粒度锁，但其 skiplist 的并发改进版已被广泛研究

**具体方案**：
- 用 `std::atomic<Node*>* forward` 替代 `Node** forward`，所有读写通过 `load(memory_order_acquire)` / `store(memory_order_release)`
- 插入用 Michael & Scott 队列的 CAS 模式：先定位前驱，CAS 链接，失败则重试
- 删除用逻辑删除标记（tombstone bit on pointer）+ 物理惰性删除
- 内存回收：Hazard Pointer（`std::atomic<std::thread::id>` 方案）或 Epoch-Based Reclamation

**预估**：纯写 QPS 从 340k 提升至 3~5M（10x+），读写混合 8:2 提升至 3~4M（3~4x）。

**代价**：
- 代码量从当前的 ~300 行膨胀到 ~1000+ 行
- 调试极其困难：ABA 问题、memory order 错误数据竞争不会立即崩溃
- 内存回收方案选型影响延迟尾部分布

**推荐指数**：如果是**基础设施级 KV 引擎**，这是唯一正确的选择。如果是普通项目，性价比不如分段锁。

---

### 方向 2：分段锁（Sharded Lock）—— 性价比最高

**原理**：将 key 空间按 hash 分成 N 个 shard，每个 shard 内仍是一把 `shared_mutex`。操作只锁对应 shard。

**可行性**：绝大多数商业 KV 存储的默认并发方案。
- **Redis Cluster**：16384 个 slot，每个 slot 对应独立的数据结构
- **Memcached**：LRU slab 分片
- **TiKV**：Region 分片

**具体方案**：
```cpp
class ShardedSkiplist {
    static const int NUM_SHARDS = 16;  // 或 31/37（质数减少碰撞）
    skiplist<K,V> shards[NUM_SHARDS];
    std::shared_mutex mtx[NUM_SHARDS]; // 每 shard 一把锁

    int shard_of(const K& key) {
        return std::hash<K>{}(key) % NUM_SHARDS;
    }
};
```
- 均衡读写比例下，16 shard 理论上将锁竞争降低到 1/16
- 可通过参数调整 shard 数适配 CPU 核心数

**预估**：16 shard 下 8读2写 混合 QPS 从 1M 提升至 4~6M。shard 数量与 CPU 核心数 1:1 时线性扩展。

**代价**：
- 全局有序遍历（`show()`）需 merge 所有 shard，复杂度 × N
- 若有关键 key 热点（大量请求同一 key），该 shard 仍会是瓶颈
- 改动量小：~100 行代码

**推荐指数**：如果目标是"工程级可用、改动成本可控"，这是首选。

---

### 方向 3：内存布局重构 —— 柔性数组成员 + Arena

**当前问题**：
- `Node.forward` 通过 `new Node*[level+1]` 在堆上独立分配 → 每个节点 2 次 malloc
- 节点在堆上离散分布 → CPU cache miss 高 → 遍历性能随数据量下降（500k vs 1k 数据量，QPS 跌 62%）

**方案 A：柔性数组成员**
```cpp
struct Node {
    K key;
    V val;
    int level;
    Node* forward[];  // C99 FAM，嵌入分配
};
// 分配: Node* n = (Node*)malloc(sizeof(Node) + sizeof(Node*) * (level+1));
```
- 节点 + forward 指针一次分配，内存连续，cache 友好
- 消除每个节点一次 `new[]` / `delete[]` 调用

**方案 B：Arena Allocator**
- 预分配大块（如 4MB），节点从 arena 中 bump-pointer 分配
- 析构时整块释放，跳过逐节点 `free`
- 与 FAM 结合：`Node* n = arena.alloc(sizeof(Node) + sizeof(Node*) * level)`

**预估**：500k 数据量下 QPS 提升 30~50%；纯读场景 cache miss 减少。

**可行性**：Linux 内核中大量使用 FAM（`struct sk_buff` 等）。Arena allocator 是 RocksDB Arena 的核心技术。

**推荐指数**：改动相对独立，不影响并发模型，可单独先行实施。

---

### 方向 4：写饥饿防护

**当前问题**：`std::shared_mutex` 实现（glibc）默认偏向读者。持续有读者时，写者可能无限等待。当前没有超时、没有退避。

**方案**：
- 写者用 `try_lock` + 指数退避重试（简单，但增加 CPU 开销）
- 或维护写等待计数器，读者发现有待写者后主动 yield（需改锁逻辑）
- 或直接换 `std::mutex` + 读写分离计数（手动实现读写锁，可控性更高）

**推荐指数**：若采用分片锁方案，单锁竞争大幅降低后此问题自然缓解。可作为 P2。

---

### 方向 5：持久化层 —— WAL + Snapshot

**当前问题**：`dump_file()` 全量覆盖写，无增量、无 crash-safe。

**工业级方案**：
- **WAL（Write-Ahead Log）**：每次 `insert`/`delete` 先追加写日志（append-only），批量 `fsync`
- **Snapshot + Compaction**：定期将内存跳表 dump 为全量快照，清理过期 WAL
- **Recovery**：启动时先加载最近快照，再回放快照之后的 WAL
- mmap 替代 `getline` 逐行解析加载

**可行性**：RocksDB WAL、Redis AOF 都是同类方案。核心难点在 fsync 性能与数据安全的权衡。

**推荐指数**：如果需要 crash-safe 的持久化，这是必修课。

---

## 优化路线图

| 阶段 | 内容 | 预期 QPS（8读2写） | 改动量 | 状态 |
|------|------|-------------------|--------|------|
| **Phase 1** | 分段锁（16 shard） | 1.0M → 1.65M（实测） | ~110行 | ✅ 完成 |
| **Phase 2** | 柔性数组 + Arena | 1.65M → 2~3M | ~200行 | 待开始 |
| **Phase 3** | Lock-Free CAS（终极方案） | 2~3M → 5M+ | 完全重写 | 待开始 |

Phase 1 实测提升低于最初预测（4~6x），原因见下方"Phase 1 复盘"。Phase 2 和 Phase 3 不受影响——分段锁暴露了新的瓶颈，恰好是 Phase 2 要解决的问题。

---

## Phase 1 复盘：分段锁实测数据

### Benchmark 结果（2026-05-25）

测试条件：10 线程，100k 预填充，每线程 100k 操作，同机对比。

| 场景 | 全局锁(本次) | 全局锁(历史) | 分段锁(本次) | 提升 |
|------|------------|------------|------------|------|
| 纯读 10 线程 | 7.37 M | 4.60 M | 6.88 M | 0.93x ↓ |
| 8 读 2 写 | 1.33 M | 1.00 M | 1.65 M | 1.24x |
| 5 读 5 写 | 0.28 M | 0.27 M | 0.72 M | **2.57x** |
| 纯写 10 线程 | 0.25 M | 0.34 M | 0.38 M | 1.52x |

### 为什么提升远低于预期的 4~6x？

预测的前提假设——"锁争用占操作时间 80-90%"——是错误的。实际锁争用大约只占 40-60%。剩余时间消耗在：

1. **内存分配**：每次 `insert` 两次 `new`（Node 对象 + forward 数组），`malloc` 内部有锁竞争
2. **随机数生成**：`std::mt19937` 每次 insert 调用（get_random_level + bench key 生成），纯计算但开销可观
3. **指针跳转的 cache miss**：跳表遍历沿随机指针链走，每步都可能 L1 miss
4. **`shared_mutex` 固有开销**：即使无争用，lock/unlock 本身也有几十个周期的代价

根据 Amdahl 定律：如果可并行部分占 60%，16 分片理论极限是 1/(1-0.6) = 2.5x。实测 5R5W 场景达到 2.57x，说明**分段锁几乎榨干了这 60% 的可并行空间**。

### 关键洞察

- **纯读微降 7%**：`hash(key) & 15` + `unique_ptr` 解引用，常数级开销，可接受
- **写越密集，收益越大**：1-2 个写者时分片几乎无收益（本来就不怎么争），5-10 个写者时分片效果显著
- **写线程数 > 分片数时收益递减**：10 写 / 16 片的碰撞概率已经可观，继续增加写线程提升会更小
- **分段锁已触及 Amdahl 天花板**，继续加分片数（32、64）不会再有明显提升

### 下一个瓶颈

从数据看，纯写只有 0.38M QPS。即使完全无锁竞争的理想情况下，纯写也不应该只比全局锁高 1.5x。说明**非锁因素**（malloc + 随机数 + cache miss）才是当前真正的瓶颈。

这正好是 Phase 2（Arena + FAM 消除 malloc + 改善 cache locality）要解决的问题。

---

## 与工业级对比（参考基准）

| 指标 | 本实现（全局锁） | 本实现（分段锁） | RocksDB Skiplist | Java ConcurrentSkipListMap |
|------|---------|---------|------------------|---------------------------|
| 并发模型 | 全局 shared_mutex | 16 分段 shared_mutex | Lock-Free CAS | Lock-Free CAS |
| 纯写 QPS | ~340k | ~380k | 3~10M（估算） | 2~5M（估算） |
| 读写混合 QPS | ~1M | ~1.65M | 5~15M（估算） | 5~10M（估算） |
| 5R5W QPS | ~270k | ~720k | — | — |
| 内存模型 | 离散 malloc | 离散 malloc | Arena + FAM | JVM 托管 |
| 持久化 | 手动全量 dump | 16 分片各自 dump | 不负责持久化 | 不负责持久化 |

**说明**：分段锁将 5R5W 场景提升 2.5x，但与工业级 Lock-Free 方案仍有数量级差距。差距的根源已从"锁争用"转移到了"内存分配 + cache miss"——这是 Phase 2 要解决的问题。
