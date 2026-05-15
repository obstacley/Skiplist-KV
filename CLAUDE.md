# CLAUDE.md

This file provides guidance to Claude Code when working in this repository.

## 项目定位

**Skiplist-KV** 是一个基于 C++17 实现的并发键值存储引擎，核心数据结构为跳表（Skip List）。

项目的双重目标：
1. **深入学习**：掌握 C++ 并发编程、内存模型、数据结构设计、性能调优等核心技能
2. **简历展示**：代码质量、工程规范、性能指标均需达到可展示水准

因此所有改动需遵循以下原则：代码整洁、命名规范、设计有据、性能可测。

**协作方式**：本项目以学习为首要目标。Claude 负责提供建议、解释原理、审查代码，具体代码修改由用户亲手完成。除非用户明确要求，否则不直接编辑源文件。

## 构建与运行

```bash
cd build && cmake .. && make -j$(nproc)
./skiplist_node
```

- 编译目标：`skiplist_node`（入口：`src/main.cpp`）
- C++ 标准：C++17
- 调试模式：取消 `CMakeLists.txt` 中 `-g -fsanitize=address` 的注释后重新构建

## 项目结构

```
.
├── include/skiplist.h    # 核心实现（header-only 模板）
├── src/
│   ├── main.cpp          # 并发基准测试（当前入口）
│   └── server.cpp        # TCP Server 原型（未完成，不参与编译）
├── test/                 # 单元测试
├── build/                # CMake 构建产物
├── CMakeLists.txt
├── optimization.md       # 性能分析与优化路线图（重要参考）
└── CLAUDE.md
```

## 架构说明

### 数据结构（`include/skiplist.h`）

模板类 `skiplist<K,V>`，包含两个内部组件：

- **`Node<K,V>`**：跳表节点。持有 key、value、level，以及 `Node<K,V>** forward` 指针数组（通过 `new[]` 分配，析构时 `delete[]` 释放）。
- **`skiplist<K,V>`**：跳表主体。
  - **并发控制**：`std::shared_mutex _mtx`。读操作（`search`、`show`、`dump_file`）加共享锁；写操作（`insert`、`delete_node`）加独占锁。
  - **持久化**：`dump_file()` 通过 POSIX `write()` 全量序列化至 `list_data.rbd`（格式：`key:val\n`）。`load_file()` 在构造时读取该文件，逐行解析后调用 `insert()` 恢复数据。键值解析使用 `if constexpr` 区分 `std::string` 与算术类型。
  - **随机层数**：线程局部 `std::mt19937`，p=0.25 递增，上限 `max_level`。
  - **内存管理**：原始指针，析构函数沿 `forward[0]` 遍历释放所有节点。

### 测试入口（`src/main.cpp`）

并发基准测试：预填充 100k 条随机数据，启动 10 线程（8 读 + 2 写），每线程执行 100k 次操作，输出 QPS 统计。

## 编码规范

- C++17，不使用非标准扩展
- 命名：类名 PascalCase（`skiplist` 除外，历史遗留），变量 snake_case，成员变量后缀 `_`（待统一）
- 头文件使用 `#ifndef` / `#define` / `#endif` 保护
- 禁止裸 `new` / `delete` 出现在新代码中，优先使用 `std::unique_ptr` / RAII
- 锁粒度：只锁必要区间，不在持锁时执行 I/O
- 日志输出使用 `std::cerr`（错误）与 `std::cout`（信息），不混用
- 注释用中文，面向读者说明设计意图而非复述代码

## 性能数据（详见 `optimization.md`）

| 场景 | QPS |
|------|-----|
| 纯读 10 线程 | 4.60M |
| 8 读 2 写 | ~1.00M |
| 5 读 5 写 | 0.27M |
| 纯写 10 线程 | 0.34M |

当前瓶颈：全局读写锁。优化方向为分段锁 → Lock-Free CAS → 内存布局重构，详见 `optimization.md`。

## 注意事项

- `src/server.cpp` 有笔误（`soket`、`setsocketopt`、`famliy`），不参与编译，后续修正后纳入构建
- 构建前确认 `build/` 目录存在
- 写操作会修改 `list_data.rbd`，调试时注意备份
