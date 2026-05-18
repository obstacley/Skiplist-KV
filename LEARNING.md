# LEARNING.md — 学习历程

## Phase 0-4 教程大纲

（原 LEARNING.md 内容保留如下）

### Phase 0：Skiplist 返回值改造

- `std::optional<V>`（C++17）：表示"可能有值，可能没有"。`search()` 返回它最自然。
- **RVO（返回值优化）**：编译器保证 `return local_val;` 零拷贝。
- **向后兼容**：`void f()` 改成 `int f()` 后旧调用方不接收返回值也合法。

### Phase 1：单线程 Echo Server

- **五个系统调用的顺序**：`socket()` → `setsockopt()` → `bind()` → `listen()` → `accept()` → `read()`/`write()` → `close()`
- **`htons()`**：Host TO Network Short，端口号字节序转换
- **`SO_REUSEADDR`**：允许重用 TIME_WAIT 状态的端口，防止重启报 EADDRINUSE
- **`INADDR_ANY`**（0.0.0.0）：监听所有网卡接口
- **`sockaddr_in` → `sockaddr*` 强转**：C 语言的多态手法，按 sa_family 区分实际类型
- **`memset(&addr, 0, sizeof(addr))`**：必须清零，否则 padding 字节有垃圾值
- **server_fd vs client_fd**：server_fd 只用来 accept()，每个 accept() 返回新 client_fd 做读写
- **`read()` 返回值**：>0 读取字节数，==0 对端关闭（FIN），<0 错误
- **`write()`**：`ssize_t write(int fd, const void *buf, size_t count)`

### Phase 2：文本协议 + KV 集成

- **TCP 是字节流，不是消息包**：一次 write 可能拆成多次 read，多次 write 可能合并成一次 read
- **行缓冲**：`std::string buffer` 做蓄水池，`find('\n')` 循环切完整行
- **`\r\n` 处理**：telnet/Windows 发 CRLF，需 `line.pop_back()` 去掉 `\r`
- **`std::istringstream`**：`>>` 自动跳空格，天然适合协议解析
- **`if constexpr`**：编译期类型分发，`load_file()` 区分 string 和算术类型

### Phase 3：多线程

- **线程本质**：独立执行路径，所有线程共享内存，每个线程独立栈（默认 8MB）
- **`std::ref()`**：把引用包成 `reference_wrapper`，让 `std::thread` 能拷贝这个 wrapper（skiplist 不可拷贝）
- **`detach()` vs `join()`**：detach 线程独立运行，join 等待完成。必须二选一，否则 `std::thread` 析构调 `std::terminate()`

### Phase 4：优雅退出

- **POSIX 信号**：Ctrl+C → 内核发 SIGINT 给进程
- **`signal()` vs `sigaction()`**：Linux glibc 的 `signal()` 使用 BSD 语义（SA_RESTART），会**自动重启被中断的系统调用**，导致 `accept()` 不返回 -1/EINTR
- **SA_RESTART**：不设 → 中断的 accept 返回 -1 + EINTR；设了 → 自动重启
- **`EINTR`**：阻塞系统调用被信号中断时 errno 为此值
- **`std::atomic<bool>` 的信号安全**：x86 上 `store()` 是一条 mov，天然安全
- **信号处理函数限制**：只能用 async-signal-safe 操作（不能 cout、malloc、mutex lock）——信号可能在持锁时到达导致死锁

---

## 实战踩坑记录

### 坑 1：`delete` 是 C++ 关键字

将 `delete_node()` 重命名为 `delete()` → 编译错误。`delete` 是语言关键字，不能做函数名。**教训**：函数命名前先想是否和关键字冲突。

### 坑 2：`#include <random>` 放在哪

`skiplist.h` 用了 `std::mt19937`，但没 include `<random>`。`main.cpp` 恰好间接引入了，所以之前能编译。新写的 `server.cpp` 编译报错。**教训**：谁用谁 include，不依赖传递包含——`skiplist.h` 自己用的头文件必须自己 include。

### 坑 3：double detach

```cpp
std::thread t(...).detach();  // 临时对象上 detach
t.detach();                    // 对已 detach 的线程再 detach → UB
```
**教训**：`std::thread t(...)` 和 `.detach()` 分开写，看清楚只有一次 detach。

### 坑 4：`signal()` 自动重启导致 Ctrl+C 关不掉服务器 ⭐

**现象**：按 Ctrl+C 后 handler 执行了，但服务器不退出，accept() 继续等。

**原因**：Linux glibc 的 `signal()` 使用 BSD 语义，自动给被中断的 accept() 添加 SA_RESTART 行为——accept() 被信号打断后自动重试，不返回 -1/EINTR。

**解决**：用 `sigaction()` 替换 `signal()`，不设 SA_RESTART 标志。accept() 被打断后返回 -1，errno=EINTR，代码检查 `running` 后 break。

**教训**：`signal()` 的行为在不同 Unix 系统上不一致（BSD 自动重启，System V 不自动重启）。Linux 选了 BSD 语义。`sigaction()` 是 POSIX 标准，行为明确可控。**永远用 sigaction 而非 signal。**

### 坑 5：进程杀不掉

Ctrl+C 关不掉后，盲目尝试 `kill` 普通信号无效（SIGTERM 默认行为和 SIGINT 类似）。**解决**：`kill -9`（SIGKILL）——内核强制终止，进程无法捕获或忽略。**理解**：SIGKILL 不给进程任何清理机会，数据可能丢失，所以优雅退出才需要正确实现。

### 坑 6：资源泄漏——`setsockopt` 失败后没 close

```cpp
// 错误
if (setsockopt(...) < 0) { return -1; }  // serverfd 泄漏

// 正确
if (setsockopt(...) < 0) { close(serverfd); return -1; }
```
**原则**：socket() 成功后，之后**所有**错误路径都必须 close(serverfd)。socket() 自身失败则不需要（没拿到 fd，没东西可关）。

### 坑 7：`reponse` 拼写错误贯穿全文

变量名 `reponse`（少了个 s，应为 `response`）。整个 server.cpp 都用这个错误拼写。**启示**：变量名一开始写对最重要，之后重命名很麻烦（除非用 IDE 重构工具）。

---

## 核心理解突破

### TCP 字节流的真正含义

**起初的错误理解**：read() 在遇到空格或换行时会停止。

**正确理解**：read() 完全不看内容，只关心内核缓冲区里有没有数据。有就拿出来（最少 1 字节，最多 count 字节），没有就阻塞等。收"SET key val\n" 可能被拆成 "SET ke" + "y val\n"，也可能和下一条命令合并成 "SET a 1\nGET b\n"。

**为什么不能靠 TCP 自身分帧**：TCP 是字节流协议，没有"消息边界"概念。应用层必须自己定义帧协议。行缓冲（\n 分隔）是最简单的帧协议。

### 行缓冲为什么用循环 find

单次 `find('\n')` 不够——一次 read 可能收到多条完整命令（批量发送或网络合并）。必须 while 循环切出所有完整的行，不完整的尾巴留在 buffer 里等下次 read 补全。

### 锁的粒度决定并发性能

全局 `shared_mutex` 的问题：一个写线程持写锁期间，**所有**读线程全部阻塞。2 个写线程拖慢 8 个读线程 4.6 倍。根本原因不是"有锁"，而是"锁太大"。分段锁将 key 空间切分，不同 shard 互不阻塞，16 个 shard 锁竞争降为 1/16。

### 信号处理的设计约束

信号就像"硬件中断"——在任意时刻（包括持锁时）强行插入执行。所以 handler 里不能做任何可能等待的事情：cout（底层有 mutex）、malloc（堆锁）、锁任何 mutex。唯一安全的跨线程通信方式是 `atomic<bool>` 的 store（x86 上单条 mov 指令）。

---

## 未来方向

### 分段锁（下一步优化，已确认方向）

性价比最高的优化。从 skiplist 移除锁 → 16 个 shard 各管一段 key 空间 → ShardedSkiplist 包装类。预期 QPS 从 1M 升至 4-6M。改动约 110 行。

### HNSW 向量索引（下一个项目）

跳表思想的高维推广——分层导航 + 贪心搜索 + 近邻图。触及 SIMD 距离计算、内存池、缓存优化。**与当前项目的关联**：分层索引思想一脉相承，你花在跳表上的时间不会白费。

### 职业方向

C++ AI 基础设施 / 向量数据库内核 / 搜索推荐引擎——卡 AI 工程师不懂 C++、C++ 工程师不懂 AI 的中间地带。

---

## 学习心法

1. **节奏比速度重要**：宁可学慢一点把原理吃透，也不要赶进度留下模糊地带。你两次主动要求放慢节奏——这是对的。
2. **先讲再写**：动手前先用自己话讲一遍原理，讲不清楚的地方就是没真懂。
3. **错误是最好的老师**：signal() 关不掉服务器这件事，比你顺利写对一百行代码学到的东西多得多。
4. **问"为什么"比问"怎么做"重要**：知道 `sigaction` 替换 `signal` 是"怎么做"；知道 BSD vs System V 语义差异、SA_RESTART 本质是"为什么"。面试考的是后者。
