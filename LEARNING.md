# C++ 学习记录

本项目实践过程中的学习历程。

---

## 2026-05-15：别名模板、右值引用、完美转发

### 别名模板（Alias Template）

```cpp
// 简化复杂类型，提高可读性
template<typename K, typename V>
using NodePtr = Node<K,V>*;       // 替代手写 Node<K,V>*

using ReadLock  = std::shared_lock<std::shared_mutex>;  // 替代完整限定名
using WriteLock = std::unique_lock<std::shared_mutex>;
```

**要点**：别名模板不是新类型，纯粹是文本替换，不产生额外模板实例化。

### 右值引用（Rvalue Reference）

- 左值（lvalue）：有名字、可取地址的变量
- 右值（rvalue）：临时对象、字面量、`std::move()` 返回值
- `K&&` / `V&&` 绑定到右值，配合 `std::move` 实现资源转移而非拷贝
- 典型应用：Node 移动构造函数 `Node(K&& k, V&& v, int level)`

### 完美转发（Perfect Forwarding）

将两个 insert 重载（const& + &&）合并为单一模板：

```cpp
template<typename RK, typename RV>
void insert(RK&& key, RV&& val);
```

**关键区别**：
- `K&&` 在类模板已确定 → **普通右值引用**（只绑右值）
- `RK&&` 在函数模板推导 → **转发引用/万能引用**（可绑左右值）

### std::forward vs std::move

| 操作 | 本质 | 使用场景 |
|------|------|----------|
| `std::forward<T>(x)` | 有条件转型——T 被推导为左值时保持左值，右值时转为右值 | 转发引用参数 |
| `std::move(x)` | 无条件转型为右值 | 明确不再需要 x 时 |

**引用折叠规则**：`K& &&` → `K&`，`K&& &&` → `K&&`。这是转发引用能同时接受左右值的底层机制。

### 成员函数模板语法

类外定义需要两行 `template<...>`：

```cpp
template<typename K, typename V>    // 类的模板参数
template<typename RK, typename RV>   // 函数自身的模板参数
void skiplist<K,V>::insert(RK&& key, RV&& val) { ... }
```

### 性能认知

- 完美转发对 benchmark 的调用模式（`insert(dist(rng), "test_value")`）不改变生成代码
- QPS 抖动（760k~2M）来自随机 key 分布差异 + 系统因素，~1.0M 是稳定基线
- 优化需从锁模型入手（见 `optimization.md`），微小重构不会改变性能面貌

---

## 待学习

- [ ] `std::atomic` 内存序（memory_order_acquire/release/relaxed）
- [ ] CAS（compare_exchange_strong/weak）循环
- [ ] Hazard Pointer / Epoch-Based Reclamation
- [ ] 柔性数组成员（FAM）+ Arena Allocator
- [ ] WAL 持久化与 crash recovery
