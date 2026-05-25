# Skiplist-KV

**C++17 并发键值存储引擎**——基于跳表（Skip List）实现，从单线程到分段锁到内存池，逐步深入系统编程。

> 这是一个**练习项目**，目标不是造轮子，而是通过亲手实现一个 KV 引擎来掌握 C++ 并发编程、内存管理和数据结构设计。每个优化阶段都保留了独立分支，方便回溯和学习。

---

## 快速开始

```bash
cd build && cmake .. && make -j$(nproc)
./kv_main          # 并发基准测试
./sharded_test     # 分段锁功能测试
./bench            # 性能对比（全局锁 vs 分段锁 vs Arena）
```

- C++ 标准：C++17
- 调试模式：取消 `CMakeLists.txt` 中 `-fsanitize=address` 的注释

---

## 架构概览

```
┌─────────────────────────────────────────┐
│  sharded_skiplist<K,V>  （分段锁包装层） │
│  ├── shards[0]  → skiplist + _mtx_0     │
│  ├── shards[1]  → skiplist + _mtx_1     │
│  └── ...                                │
├─────────────────────────────────────────┤
│  skiplist<K,V>  （核心数据结构）         │
│  ├── Node（跳表节点，FAM/Arena 分配）    │
│  ├── shared_mutex（读写锁）              │
│  ├── dump_file / load_file（持久化）     │
├─────────────────────────────────────────┤
│  server.cpp（TCP Server，多线程）        │
└─────────────────────────────────────────┘
```

---

## 性能演进（10 线程，100k 预填充，同机对比）

| 阶段 | 分支 | 5R5W QPS | 关键改动 |
|------|------|----------|----------|
| 起点 | — | 0.27M | 全局 `shared_mutex` |
| Phase 1 | `server.cpp开始` | 0.72M (**2.5x**) | 16 分段锁 |
| Phase 2 | `phase2-arena-fam` | 1.13M (**4.0x**) | + Arena + FAM 内存池 |

---

## 适合新手练习的切入点

项目按难度递进，每个点都可以独立 fork 出去练习：

### 入门级

| 练习点 | 涉及文件 | 你能学到什么 |
|--------|---------|-------------|
| 补全单元测试 | `test/` | Google Test 用法、边界条件设计 |
| `show()` 输出改为 JSON/可视化 | `skiplist.h` | 数据结构的遍历与序列化 |
| 修 `server.cpp` 的已知 bug | `src/server.cpp` | socket 编程、epoll、TCP 协议 |
| 加命令行参数（端口、文件路径） | `src/main.cpp` | `getopt` / arg 解析 |

### 进阶级

| 练习点 | 涉及文件 | 你能学到什么 |
|--------|---------|-------------|
| 把 `std::shared_mutex` 换成 `std::mutex` + 手动读写计数 | `skiplist.h` | 锁的实现原理、写饥饿问题 |
| 实现 `range_scan(start_key, end_key)` | `skiplist.h` | 区间查询、迭代器模式 |
| 单测改为参数化测试（不同 K/V 类型组合） | `test/` | C++ 模板测试、类型系统 |
| 用 `perf` + 火焰图分析瓶颈 | — | 性能工程工具链 |

### 硬核级

| 练习点 | 涉及文件 | 你能学到什么 |
|--------|---------|-------------|
| 改用标准 C++ 定长 forward 数组替代 FAM | `skiplist.h` (`phase2-arena-fam` 分支) | 内存布局、cache line |
| 用 CAS 实现 lock-free insert | 重写 `skiplist.h` | `std::atomic`、memory order、ABA |
| 用 mmap 替代 `write()` 做持久化 | `skiplist.h` | 零拷贝 I/O、crash-safe |
| 写自己的 Arena Allocator 并对比 benchmark | `skiplist.h` | 内存管理、分配器设计 |

---

## 项目结构

```
.
├── include/
│   ├── skiplist.h            # 核心跳表（header-only 模板）
│   └── sharded_skiplist.h    # 分段锁包装（16 shard）
├── src/
│   ├── main.cpp              # 并发基准测试
│   └── server.cpp            # TCP Server（待修复）
├── test/
│   └── sharded_test.cpp      # 分段锁正确性测试
├── benchmark/
│   └── bench.cpp             # 全局锁 vs 分段锁 vs Arena 对比
├── optimization.md           # 优化路线图 & 复盘
├── CONTEXT.md                # 开发日志
└── CMakeLists.txt
```

---

## License

MIT — 这是练习代码，欢迎参考、fork、PR。
