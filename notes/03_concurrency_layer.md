# 并发层复习笔记 — 线程、信号、原子变量

## 1. 线程模型

```
进程
├── 线程1 (main): accept() 阻塞，等新连接
├── 线程2: 处理 client A
├── 线程3: 处理 client B
└── 线程4: 处理 client C
```

每个线程有独立的**栈**（局部变量在栈上），但**堆、全局变量、fd 表**是共享的。
→ 所有 worker 线程共享同一个全局 `kv`（跳表实例）
→ 这是并发问题的根源：多个线程同时读写同一块内存

## 2. std::ref — 为什么必须写？

```cpp
std::thread t(handle_client, clientfd, std::ref(kv));
```

`std::thread` 默认把所有参数**拷贝**一份传给线程函数。
不写 `std::ref` → 每个线程获得独立的 kv 副本，数据不共享，并发无意义。

`std::ref` 创建一个 `std::reference_wrapper`，告诉 `std::thread`："别拷贝，传引用。"

需要 `#include <functional>`。

## 3. detach vs join

| | detach | join |
|------|--------|------|
| 含义 | "我不管你了，独立运行" | "我等你结束" |
| 析构时 | 线程还在跑也 OK | 没 join → std::terminate 杀进程 |
| main 退出 | 分离线程被内核默默 kill | main 等 join 完才退出 |
| 使用场景 | 长期 worker 线程 | 需要获取线程结果时 |

**这里为什么用 detach？** main 的工作是不停 accept 新连接。如果用 join，main 会卡在等第一个客户端断开，其他人都连不进来 → 退化成了单客户端串行服务。

**detach 的代价**：main 失去对 worker 的控制。优雅退出时 worker 可能还在操作数据。

**改进方向**：`vector<std::thread>` + 退出时逐个 `join()`，配合 `running` 原子变量通知退出。

## 4. 信号处理与优雅退出

### 4.1 整体流程

```
Ctrl+C → 内核发 SIGINT
→ handle_signal() 设 running=false
→ accept() 被中断，返回 -1, errno=EINTR
→ main 检查 errno==EINTR → 检查 running → break
→ kv.dump_file() 保存数据
→ 进程正常退出
```

**设计原则**：信号处理函数只做最小的事（设 flag），主循环负责响应并做清理。
信号处理上下文中几乎所有 C++ 操作都不安全。

### 4.2 为什么必须是 std::atomic<bool> 而非普通 bool

| 风险 | 说明 |
|------|------|
| **数据竞争** | 信号处理器写、main 读，无 mutex → UB |
| **编译器优化吃修改** | 编译器可能把 `while(running)` 提升成 `if(running) while(true)` |
| **弱一致性CPU** | ARM 上 store buffer 可能无限延迟写出，其他核看不到 |

`std::atomic<bool>` 保证：
- 原子性（不会读一半写一半）
- 可见性（写入对其他线程可见）
- 禁止编译器乱优化

`memory_order_relaxed` 足够：没有其他变量和 running 之间有顺序依赖。

### 4.3 volatile 为什么不行？

`volatile` 语义是"禁止寄存器缓存"，不禁止指令重排，不是为多线程设计的。
Java 的 volatile ≈ C++ 的 atomic，但 C/C++ volatile ≠ atomic。

## 5. errno == EINTR 的处理

```cpp
if (clientfd == -1) {
    if (errno == EINTR) {           // 是被信号打断，不是真错误
        if (!running.load()) break; // 退出信号 → 退出
        continue;                   // 其他信号（如 SIGCHLD）→ 重试
    }
    // 真错误
}
```

**关键**：`accept()` 等慢速系统调用被信号打断时返回 -1，必须查 errno 区分"被信号打断"和"真失败"。

## 6. 信号详解

### 6.1 什么是信号？

信号是**内核发给进程的"短信"**，内容是信号编号。

| 信号 | 编号 | 默认行为 | 触发 |
|------|------|---------|------|
| SIGINT | 2 | 终止进程 | Ctrl+C |
| SIGTERM | 15 | 终止进程 | `kill <pid>` |
| SIGKILL | 9 | 强制终止（不可拦截） | `kill -9 <pid>` |
| SIGCHLD | 17 | 忽略 | 子进程状态变化 |

信号来了，三个选择：**默认动作**（死掉）、**忽略**、**自定义处理**（跳转到你的函数）。

### 6.2 struct sigaction 结构体

```cpp
struct sigaction {
    void     (*sa_handler)(int);    // ① 信号处理函数指针
    sigset_t   sa_mask;             // ② 处理时临时屏蔽哪些信号
    int        sa_flags;            // ③ 行为选项（位掩码）
    void     (*sa_restorer)(void);  // ④ 内部使用，不要碰
};
```

#### ① sa_handler — 处理函数

```cpp
void handle_signal(int sign)  // sign 是信号编号
{
    running.store(false, std::memory_order_relaxed);
}
```

`int sign` 参数：同一个处理函数可注册给多个信号，通过 `sign` 区分是谁触发的。

#### ② sa_mask — 处理期间屏蔽信号集

处理函数执行时临时屏蔽指定信号，防止嵌套打断。一行就结束的处理函数不需要设置。

#### ③ sa_flags — 行为选项（位掩码）

| Flag | 含义 |
|------|------|
| 0（不设） | 被信号打断的系统调用返回 EINTR |
| `SA_RESTART` | 自动重试被中断的系统调用，不返回 EINTR |
| `SA_RESETHAND` | 处理一次后恢复默认行为 |
| `SA_NODEFER` | 处理期间不自动屏蔽自身 |

**本项目没设 SA_RESTART** → accept() 返回 -1 + EINTR → 有机会检查 running。这是正确的设计。

#### ④ sa_restorer — 别碰

内核内部使用。

### 6.3 为什么用 sigaction 而非老式 signal()

- `signal()` 在不同 Unix 变体上行为不一致（BSD 自动重置，System V 不重置）
- `sigaction()` 行为明确、字段可控，是 POSIX 标准推荐

## 7. 信号处理函数的安全限制

**信号处理函数中几乎所有 C++ 操作都是不安全的。** 因为执行时不知道主程序运行到哪一行——可能正持着锁。

```cpp
// 危险！如果 main 正在 insert() 里持锁，handle_signal 又去拿同一把锁 → 死锁
void handle_signal(int) {
    kv.dump_file();  // 也要拿 _mtx → 自己等自己 → 死锁
}

// 正确做法：只设 flag
void handle_signal(int) {
    running.store(false, std::memory_order_relaxed);
}
```

设计原则：**信号处理函数只做最小的事（设 flag），主循环负责响应 flag 并做清理。**

## 8. std::atomic 与 memory_order 详解

### 8.1 为什么必须 atomic？（vs 普通 bool）

```cpp
// 信号处理器写
running.store(false, relaxed);    // 写

// main 读
while (running.load(relaxed)) {   // 读
```

三个风险：

| 风险 | 说明 |
|------|------|
| **数据竞争（UB）** | 一个写一个读，无 mutex，C++ 标准规定这是 UB |
| **编译器优化** | 编译器可能把循环中的读提升为只读一次：`if(running) while(true)` |
| **弱一致性 CPU** | ARM 上 store buffer 可能无限延迟写出，其他核永远看不到 |

`std::atomic<bool>` 保证：**原子性**（不会读一半）、**可见性**（写入对其他线程可见）、**禁止编译器乱优化**。

### 8.2 显式 store/load vs operator=

```cpp
// 方式一：operator=（默认最强内存序 seq_cst）
running = false;

// 方式二：显式 store（可选更弱内存序）
running.store(false, std::memory_order_relaxed);
```

`operator=` 不可选内存序，固定用最安全的 seq_cst。`store()`/`load()` 允许根据需求降级。

### 8.3 memory_order 四种级别

| 级别 | 什么不能乱序 | 性能 | 典型用途 |
|------|------------|------|---------|
| `relaxed` | 只保证这一条读/写是原子的 | 最快 | 孤立的 flag（本项目场景） |
| `acquire` | 它之后的读写不能移到它前面 | 中 | 读锁 |
| `release` | 它之前的读写不能移到它后面 | 中 | 写锁 |
| `seq_cst` | 全局唯一顺序 | 最慢 | 默认，不确信时用这个 |

### 8.4 本项目为什么 relaxed 足够？

`running` 是一个孤立的 flag——没有任何其他变量依赖它，它也不依赖任何其他变量。只需要：**写入本身是原子的，且最终另一线程能看到。** `relaxed` 刚好满足。

### 8.5 volatile 为什么不行？

`volatile` ≠ `atomic`，两者的设计目的完全不同：

| | volatile | atomic |
|------|---------|--------|
| 设计目的 | 内存映射 I/O、信号处理中访问被中断的变量 | 多线程同步 |
| 防止寄存器缓存 | ✓ | ✓ |
| 防止指令重排 | ✗ | ✓（按 memory_order） |
| 保证原子性 | ✗ | ✓ |
| 适用场景 | 硬件寄存器 | 多线程数据 |
