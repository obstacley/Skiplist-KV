# CONTEXT.md — Phase 1: 分段锁实现指南

## 背景

当前 `skiplist<K,V>` 一把全局 `shared_mutex`，锁竞争是唯一瓶颈。8读2写下 QPS ~1M，纯读却能达到 4.6M——2 个写线程把 8 个读线程拖慢 4.6 倍。

## 目标

将 key 空间 hash 分成 16 个 shard，每个 shard 独立 `shared_mutex`，锁竞争降至 ~1/16。预期 QPS 从 1M 升至 4~6M。

## 设计决策

**从 `skiplist` 移除锁**：skiplist 变为单线程数据结构，所有并发控制交给新类 `ShardedSkiplist<K,V>`。避免双重锁定（ShardedSkiplist 层锁一次，skiplist 内部再锁一次）。

**持久化上移**：`load_file()` / `dump_file()` 由 ShardedSkiplist 管理，单个 skiplist 构造时不再自动加载文件。

**顺路修 bug**：`element_count` 改为 `std::atomic<int>`，修复 `get_size()` 数据竞争。

---

## 实现步骤

### 步骤 1：skiplist 去锁化

改 `include/skiplist.h`：

1. **加头文件**：`#include <atomic>`、`#include <functional>`

2. **`element_count` 改 atomic**：
   - `int element_count;` → `std::atomic<int> element_count{0};`
   - `get_size()` 返回 `element_count.load(std::memory_order_relaxed)`
   - `++element_count` → `element_count.fetch_add(1, std::memory_order_relaxed)`
   - `--element_count` → `element_count.fetch_sub(1, std::memory_order_relaxed)`
   - `dump_file` 的 cout 里也要 `.load()`

3. **删除 `_mtx` 成员**：`mutable std::shared_mutex _mtx;`

4. **删除所有方法里的锁**：`search`、`show`、`delete_node`、`insert`、`dump_file` 中的 `ReadLock/WriteLock lock(_mtx);` 全部删除

5. **删除构造函数中的 `load_file()` 调用**

6. **删除 `filename` 成员**，改为方法参数默认值

### 步骤 2：dump_file / load_file 参数化

将 `dump_file()` 和 `load_file()` 改为接收 `const std::string& fname` 参数，默认值 `"list_data.rbd"`。这样 ShardedSkiplist 可以控制文件名，单个 skiplist 也可以独立使用。

### 步骤 3：新增 serialize() 辅助方法

`void serialize(std::string& out) const` — 遍历跳表，把 `key:val\n` 追加到 out 字符串。供 dump_file 和 ShardedSkiplist::dump_file 复用。

**实现**：沿 `forward[0]` 遍历，用 `stringstream` 格式化每行，追加到 out。

### 步骤 4：新增 ShardedSkiplist 类

核心设计：

```cpp
template<typename K, typename V, int NUM_SHARDS = 16>
class ShardedSkiplist {
    skiplist<K,V> _shards[NUM_SHARDS];
    mutable std::shared_mutex _shard_mtx[NUM_SHARDS];

    int _shard_of(const K& key) const {
        return std::hash<K>{}(key) % NUM_SHARDS;
    }

public:
    // 禁止拷贝（skiplist 用原始指针，拷贝会 double free）
    ShardedSkiplist(const ShardedSkiplist&) = delete;
    ShardedSkiplist& operator=(const ShardedSkiplist&) = delete;

    // --- 各方法 ---
    void search(const K& key) const;
    void show() const;
    void delete_node(const K& key);
    template<typename RK, typename RV>
    void insert(RK&& key, RV&& val);
    void dump_file() const;
    void load_file();
    int get_size() const;
};
```

**各方法要点**：

- **search/delete_node**：`_shard_of(key)` → 锁对应 shard → 委托 `_shards[idx].xxx(key)`
- **insert**：先用 `const K& k = key;` 算出 shard 索引（不消费 key），然后加写锁，`std::forward` 转发给对应 shard
- **show**：逐个 shard 加读锁输出
- **dump_file**：对每个 shard 加读锁 → `serialize(buffer)` → 释放锁。所有 shard 收集完后，open + write + close 一次性写入
- **load_file**：读文件逐行解析 key/value → 调 `insert()`（insert 内部会 hash 路由到正确 shard）
- **get_size**：遍历各 shard 的 `get_size()` 求和

### 步骤 5：更新 main.cpp

改一行：
```cpp
// 之前
skiplist<int,std::string> list_test;
// 之后
ShardedSkiplist<int,std::string> list_test;
```

其余代码不变。接口完全兼容。

---

## 关键细节提醒

### insert 完美转发

```cpp
template<typename RK, typename RV>
void insert(RK&& key, RV&& val) {
    const K& k = key;            // ★ 关键：不消费 key，只读它来计算 shard
    int idx = _shard_of(k);
    WriteLock lock(_shard_mtx[idx]);
    _shards[idx].insert(std::forward<RK>(key), std::forward<RV>(val));
}
```

`const K& k = key` 这行：当 key 是右值时，const 左值引用可以绑定到右值，key 不会被移动。然后 `std::forward<RK>(key)` 正确还原原始值类别传给下层 insert。

### 无死锁保证

每个 public 方法每次只锁一个 shard。show/dump_file 虽然遍历所有 shard，但是一个一个锁、用完就释放，不会同时持有多把锁。不可能死锁。

### Hash 分布

`std::hash<int>` 典型实现就是 identity（hash(x) = x）。benchmark 用 `std::uniform_int_distribution` 产生均匀随机 key，`key % 16` 分布均匀。

---

## 编译验证

```bash
cd build && cmake .. && make -j$(nproc) && ./skiplist_node
```

预期：编译通过，QPS 从 ~1M 提升到 4~6M。

---

## 改动量估算

| 文件 | 改动 |
|------|------|
| `include/skiplist.h` | 删锁 ~10 行，atomic 改 ~8 行，参数化 ~6 行，serialize +15 行，ShardedSkiplist +80 行 |
| `src/main.cpp` | 改 1 行 |
| **合计** | ~110 行 |
