# 存储层复习笔记 — 跳表数据结构与并发控制

## 1. 跳表原理

跳表是**加了多级索引的有序链表**。普通有序链表查找 O(n)，跳表通过随机层高建立"快速通道"，平均 O(log n)。

```
Level 3:  header ──────────→ 50 ──────────→ NULL
Level 2:  header ──→ 20 ──→ 50 ──→ 80 ──→ NULL
Level 1:  header → 10 → 20 → 40 → 50 → 80 → NULL
Level 0:  header →5→10→15→20→35→40→45→50→70→80→NULL
```

- 第 0 层是完整链表（所有节点都在）
- 每往上一层约减少 75% 节点（p=0.25）
- 查找从最高层开始，每层尽量往右走，遇到 >= key 就降一层
- 最终在第 0 层判断是否命中

## 2. 核心数据结构

```cpp
template<typename K, typename V>
class Node {
    K key;
    V val;
    int node_level;
    Node<K,V>** forward;   // 指针数组，forward[i] = 第 i 层的后继
};

template<typename K, typename V>
class skiplist {
    int curr_level;                  // 当前最高层（0-indexed）
    Node<K,V>* header;               // 哨兵节点（key=V{}），永远在最高层
    int element_count;               // 节点计数（⚠️ 非原子，有数据竞争）
    mutable std::shared_mutex _mtx;  // 全局读写锁
};
```

### header 哨兵节点

构造时分配在 `max_level`（20）层。它的 forward 数组覆盖所有可能的层，简化边界处理——插入新节点时，即使 `new_level > curr_level`，`update[i]` 也可以直接指向 header。

## 3. 四个核心操作

### 3.1 search — 读锁

```cpp
std::optional<V> search(const K& key) const {
    ReadLock lock(_mtx);                    // ① 共享锁
    auto current = header;
    for (int i = curr_level; i >= 0; --i) {
        while (current->forward[i] != nullptr
               && current->forward[i]->key < key)  // ② 严格 <
            current = current->forward[i];
    }
    current = current->forward[0];
    if (current != nullptr && current->key == key)
        return current->val;
    return std::nullopt;
}
```

**关键细节**：

- **为什么是 `< key` 而非 `<= key`？** 确保找到的是"严格小于 key 的最后一个节点"，即插入位置的前驱。等号跳过的话会在有重复时找到正确的前驱。
- **为什么加读锁？** search 不修改任何数据。`shared_lock` 允许多个读线程并发，互不阻塞。
- 返回 `std::optional<V>` 而非裸指针，避免调用方操作野指针。

### 3.2 insert — 写锁

```cpp
bool insert(RK&& key, RV&& val) {
    WriteLock lock(_mtx);                       // ① 独占锁

    // 步骤1: 从高到低，每层记录 update[i] = 该层最后一个 < key 的节点
    NodePtr<K,V> update[max_level+1];
    auto current = header;
    for (int i = curr_level; i >= 0; --i) {
        while (current->forward[i] != nullptr
               && current->forward[i]->key < key)
            current = current->forward[i];
        update[i] = current;
    }

    // 步骤2: 检查 key 是否已存在
    current = current->forward[0];
    if (current != nullptr && current->key == key) {
        current->val = std::forward<RV>(val);   // 原地更新
        return false;
    }

    // 步骤3: 随机生成层数，必要时提升 curr_level
    int new_level = get_random_level();
    if (new_level > curr_level) {
        for (int i = curr_level + 1; i <= new_level; ++i)
            update[i] = header;                 // 高出部分的前驱 = header
        curr_level = new_level;
    }

    // 步骤4: 创建节点，逐层插入（标准链表插入）
    auto new_node = new Node<K,V>(...);
    for (int i = 0; i <= new_level; ++i) {
        new_node->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = new_node;
    }
    ++element_count;
    return true;
}
```

**关键细节**：

- **完美转发 `std::forward<RK>(key)`**：`insert` 是转发函数模板，接受左值和右值。`std::forward` 保持原始值类别——左值拷贝、右值移动，避免不必要的拷贝。
- **update 数组是 VLA（变长数组）**：`NodePtr<K,V> update[max_level+1]`。虽然 C++ 标准不支持 VLA，但 GCC/Clang 默认开了扩展。更标准的写法是 `std::vector<NodePtr<K,V>>` 或 `std::array`。
- **为什么加写锁？** insert 修改多个节点的 forward 指针和 element_count，操作期间不能有任何并发读或写——读可能看到半插入的节点（forward[2] 指着新节点但 forward[0] 还没连上）。

### 3.3 delete_node — 写锁

```cpp
bool delete_node(const K& key) {
    WriteLock lock(_mtx);

    // 步骤1: 定位前驱（和 insert 一样）
    NodePtr<K,V> update[max_level+1];
    memset(update, 0, sizeof(update));  // 初始化为 nullptr
    // ... 遍历填充 update[i] ...

    // 步骤2: 逐层跳过目标节点
    current = current->forward[0];
    if (current != nullptr && current->key == key) {
        for (int i = 0; i <= curr_level; ++i) {
            if (update[i]->forward[i] != current) break;
            update[i]->forward[i] = current->forward[i];
        }
        delete current;

        // 步骤3: 收缩 curr_level（高层变空）
        while (curr_level > 0 && header->forward[curr_level] == nullptr)
            --curr_level;

        --element_count;
        return true;
    }
    return false;
}
```

**关键细节**：

- **`memset(update, 0, ...)`**：C 风格初始化。CLAUDE.md 禁止裸 new/delete，这里的 memset 也是类似的历史遗留问题。
- **break 而非 continue**：`if (update[i]->forward[i] != current) break` - 一旦某一层前驱不指向 current，说明目标节点在该层以上不存在，无需继续。
- **curr_level 收缩**：删除的可能是唯一的高层节点，此时需要降低 curr_level。这是正确的维护，否则查找会从空的高层开始浪费循环。

### 3.4 dump_file — 读锁

```cpp
void dump_file() const {
    ReadLock lock(_mtx);
    // 遍历 forward[0]（完整有序链表）
    // 逐行写 "key:val\n" 到 list_data.rbd
}
```

序列化格式极简：文本格式 `key:val\n`，无二进制编码，无长度前缀。优点是肉眼可读、调试方便；缺点是类型转换有开销、字符串 key/value 不能包含 `:` 和 `\n`。

## 4. 锁策略分析

### 4.1 为什么是全局锁？

| 设计 | 锁粒度 | 并发读 | 读-写并发 | 实现复杂度 |
|------|--------|--------|----------|-----------|
| 当前（全局 shared_mutex） | 整表 | ✓ | ✗ | 低 |
| 分段锁 | 按 key hash | ✓ | ✓（不同段） | 中 |
| Lock-Free | 单节点 CAS | ✓ | ✓ | 极高 |

### 4.2 从 benchmark 看瓶颈

| 场景 | QPS | 分析 |
|------|-----|------|
| 纯读 10 线程 | 4.60M | shared_lock 不互斥，完美并发 |
| 8 读 2 写 | ~1.00M | 写锁阻塞所有读，读-写互斥 → 降到 22% |
| 5 读 5 写 | 0.27M | 写竞争加剧 |
| 纯写 10 线程 | 0.34M | 写之间完全串行 |

**结论**：写锁是瓶颈。任何写操作都会阻止所有读操作，即使它们访问不同的 key。

### 4.3 锁的 RAII 管理

```cpp
// search 内
ReadLock lock(_mtx);   // 构造 → 加共享锁
// ... 查找逻辑 ...
// 函数返回 → lock 析构 → 自动解锁（即使异常也安全）
```

`ReadLock` = `std::shared_lock<std::shared_mutex>`
`WriteLock` = `std::unique_lock<std::shared_mutex>`

都是 RAII 类型——构造时加锁，析构时解锁。不需要手动 `unlock()`，不会因为忘记解锁而死锁。

## 5. 随机层高生成

```cpp
int get_random_level() {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    int level = 1;
    while (dist(rng) < 0.25 && level < max_level)
        ++level;
    return level;
}
```

- `p = 0.25`：每层概率 25%。node 在 level 1 的概率 100%，level 2 概率 25%，level 3 概率 6.25%，依此类推
- `thread_local`：每个线程一个独立的随机数引擎，避免锁竞争
- `std::mt19937`（梅森旋转）而非 `rand()`：质量更好，周期更长
- 上限 `max_level = 20`：防止无限层高。理论上 p=0.25 时，到达 20 层的概率约 10^-12

## 6. 已知问题

### 6.1 element_count 数据竞争

```cpp
int get_size() const {
    return element_count;   // 无锁读取！
}
```

`element_count` 在 insert/delete 中被修改（写锁内，安全），但 `get_size()` 不加任何锁直接读。多线程环境下：写线程正在 `++element_count`，读线程同时读 → 数据竞争 → 未定义行为。

**修复**：改为 `std::atomic<int>`，或 `get_size()` 内加读锁。

### 6.2 裸指针与 VLA

header、forward、update 数组都是裸指针 + new/delete。不符合现代 C++ 的 RAII 原则。改进方向：`std::unique_ptr`、`std::vector`。

### 6.3 序列化格式的局限

`key:val\n` 格式下 key 和 val 不能包含 `:` 或 `\n`。用 `std::string` 作为 key/value 类型时这是一个隐藏陷阱。
