# CONTEXT.md — 对话记忆

## 当前状态

复习阶段已完成（4/4 话题），已知 bug 已修复。正在推进**分段锁（ShardedSkiplist）**实现。

## 复习笔记

| 序号 | 话题 | 笔记文件 |
|------|------|----------|
| 1 | 网络层 | `notes/01_network_layer.md` |
| 2 | 协议层 | `notes/02_protocol_layer.md` |
| 3 | 并发层 | `notes/03_concurrency_layer.md` |
| 4 | 存储层 | `notes/04_storage_layer.md` |

## 已修复问题

- [x] `reponse` 拼写错误（server.cpp 通篇）
- [x] skiplist.h 中注释掉的死代码（旧 ofstream 版 dump_file）
- [x] `element_count` → `std::atomic<int>`，get_size() 数据竞争修复
- [x] 添加 `namespace skv`
- [x] `skiplist` 构造函数支持自定义文件名（分段锁前置需求）
- [ ] test/ 目录为空，无单元测试

## 分段锁（ShardedSkiplist）—— 进行中

### 架构

```
ShardedSkiplist<K,V>
├── shards[0]  → skiplist(文件: list_data_0.rbd, 锁: _mtx_0)
├── shards[1]  → skiplist(文件: list_data_1.rbd, 锁: _mtx_1)
├── ...
└── shards[15] → skiplist(文件: list_data_15.rbd, 锁: _mtx_15)

key "abc" → hash % 16 = 5 → shards[5] 处理，只锁 _mtx_5
key "xyz" → hash % 16 = 2 → shards[2] 处理，只锁 _mtx_2
不同分片完全并发，互不阻塞。
```

### 关键设计

- **SHARD_COUNT = 16**（2 的幂，位运算取模 `hash & 15` 替代 `% 16`）
- **头文件** `include/sharded_skiplist.h`（模板类，声明+实现在同一文件）
- **不修改** 现有 `skiplist.h`，包装模式零破坏
- **预估** ~110 行新代码，预期 QPS 提升 4-6x

### 接口声明

```cpp
template<typename K, typename V>
class shardedskiplist {
private:
    std::vector<skiplist<K,V>> shards;
    static constexpr size_t SHARD_COUNT = 16;

    size_t shard_index(const K& key) const {
        return std::hash<K>{}(key) & (SHARD_COUNT - 1);
    }

    static std::string shard_filename(size_t idx) {
        std::ostringstream oss;
        oss << "list_data_" << idx << ".rbd";
        return oss.str();
    }

public:
    shardedskiplist();

    std::optional<V> search(const K& key) const;
    bool insert(K key, V val);          // 按值传 + move，不是 const&
    bool delete_node(const K& key);
    int get_size();                     // 遍历 16 片求和，近似值
    void show();
    void dump_file();
};
```

### 构造函数实现

```cpp
template<typename K, typename V>
shardedskiplist<K,V>::shardedskiplist() {
    shards.reserve(SHARD_COUNT);
    for (size_t i = 0; i < SHARD_COUNT; ++i) {
        shards.emplace_back(shard_filename(i));
        // emplace_back：在 vector 内部直接构造 skiplist，传入文件名
        // push_back 不行：skiplist 不可拷贝（含 shared_mutex）
    }
}
```

### 单分片操作（代理调用）

```cpp
// search — 路由到目标分片，内层自己加读锁
template<typename K, typename V>
std::optional<V> shardedskiplist<K,V>::search(const K& key) const {
    return shards[shard_index(key)].search(key);
}

// insert — 按值传参，move 进内层，内层自己加写锁
template<typename K, typename V>
bool shardedskiplist<K,V>::insert(K key, V val) {
    return shards[shard_index(key)].insert(std::move(key), std::move(val));
}

// delete_node — 路由到目标分片，内层自己加写锁
template<typename K, typename V>
bool shardedskiplist<K,V>::delete_node(const K& key) {
    return shards[shard_index(key)].delete_node(key);
}
```

### 跨分片操作（聚合）

```cpp
// get_size — 遍历求和，结果是近似值（各分片独立加锁，无全局快照）
template<typename K, typename V>
int shardedskiplist<K,V>::get_size() {
    int total = 0;
    for (auto& s : shards) total += s.get_size();
    return total;
}

// show — 逐片打印结构
template<typename K, typename V>
void shardedskiplist<K,V>::show() {
    for (size_t i = 0; i < SHARD_COUNT; ++i) {
        std::cout << "=== Shard " << i << " ===" << std::endl;
        shards[i].show();
    }
}

// dump_file — 每片写自己的文件 (list_data_0.rbd ~ list_data_15.rbd)
template<typename K, typename V>
void shardedskiplist<K,V>::dump_file() {
    for (auto& s : shards) s.dump_file();
}
```

### 后续步骤

1. 完成 `sharded_skiplist.h` 声明 + 实现
2. 新建 `test/sharded_test.cpp`，编译验证模板正确性
3. 修改 `main.cpp` / `server.cpp`：`skiplist` → `shardedskiplist`
4. benchmark 对比分段锁 vs 全局锁的 QPS 提升

### 已知 trade-off

- **热点 key** 仍然竞争（hash 到同一分片的 key 共享一把锁）
- **get_size() 是近似值**，不是精确事务快照
- **每个分片独立持久化**，产生 16 个小文件而非 1 个大文件

---

**秋招定位**：C++ AI 基础设施 / 向量数据库内核 / 搜索推荐引擎。卡位逻辑：AI 工程师不会 C++，C++ 工程师不懂 AI，中间地带竞争小、壁垒高。

## 项目协作约定

- 用户亲手写所有代码，Claude 负责教学、review、答疑
- 先理解原理再动手，节奏以学会为准而非完成功能
- 参考 CLAUDE.md 和 memory（learning-not-delivering）
