# Multi-Threaded TCP KV Server — 学习指导

## 总览

```
Phase 0 (skiplist 返回值) ──┐
                             ├──> Phase 2 (协议+KV) ──> Phase 3 (多线程) ──> Phase 4 (优雅退出)
Phase 1 (单线程 Echo) ──────┘
```

Phase 0 和 Phase 1 互相独立，可任选开始。

---

## Phase 0：Skiplist 返回值改造

### 要学什么

`search()`, `insert()`, `delete_node()` 目前返回值全是 `void`。作为库组件，调用方（server）需要知道"查到了吗？""插入了还是更新了？""删掉了吗？"。

### 核心知识点

1. **`std::optional<V>`**（C++17）：表示"可能有值，可能没有"。`search()` 返回它最自然——找到返回 `val`，没找到返回 `std::nullopt`。对比输出参数 `bool search(key, V& out)` 的优劣。

2. **RVO（返回值优化）**：编译器保证 `return local_val;` 不会产生拷贝。即使 `V = std::string` 也是零开销。

3. **向后兼容**：`void f()` 改成 `int f()` 后，旧调用方 `f();` 不接收返回值，完全合法。所以 main.cpp 的 benchmark 不用改也能编译。

### 你自己的任务

- `search()` → 返回 `std::optional<V>`，加 `#include <optional>`
- `insert()` → 返回 `bool`（新插入 true，覆盖更新 false）
- `delete_node()` → 返回 `bool`（找到了且删了 true，没找到 false）
- 顺手删掉 `delete_node()` 里的 `std::cout` 调式输出

做完后编译 main.cpp 验证——不改 benchmark 代码，它应该正常编译运行。

---

## Phase 1：单线程 Echo Server

### 要学什么

一个 TCP Server 从创建到通信的完整生命周期。这是网络编程的基本功，后面的协议、多线程都基于此。

### 核心知识点

1. **五个系统调用的顺序和职责**：

   ```
   socket()    创建 socket，返回一个 fd（文件描述符）
      ↓
   bind()      把 fd 绑定到 IP:Port
      ↓
   listen()    标记 fd 为"被动 socket"，准备接受连接。backlog 含义。
      ↓
   accept()    阻塞等待客户端连接，返回新的 client_fd
      ↓
   close()     释放 fd
   ```

2. **为什么 listen 和 accept 是分开的**：listen 让内核开始处理三次握手（TCP backlog 队列），accept 只从队列里取已完成的连接。分开允许你在 accept 前设置选项。

3. **read() 返回值语义**：
   - `> 0`：读到了 n 字节
   - `== 0`：对端关闭连接（FIN 到达）
   - `< 0`：错误

4. **SO_REUSEADDR**：没有它，关闭 server 后立即重启会报 `EADDRINUSE`（内核 TIME_WAIT 占用端口 60 秒）。

5. **TCP 是字节流，不是消息包**：后面 Phase 2 会用到的关键概念，但 Phase 1 先知道就行。

### 你自己的任务

1. 修复 server.cpp 现有 bug（括号、拼写）
2. 添加 `listen()` 
3. 实现 accept → read → echo → close 循环
4. CMakeLists.txt 加 `kv_server` target

### 验证

Terminal 1 跑 `./kv_server`，Terminal 2 用 `nc localhost 9090` 连接，输入字符，看到回显。

---

## Phase 2：文本协议 + KV 集成

### 要学什么

设计应用层协议，解决"TCP 是字节流"带来的组包问题，把跳表作为 server 后端。

### 核心知识点

1. **TCP 流式传输的坑**：客户端一次 `write("SET key val\n")` 到了服务端可能被拆成多次 `read()` 收到（"SET ke" + "y val\n"），或者多次 write 合并成一次 read。所以必须自己做**行缓冲**。

2. **行缓冲模式**：维护一个 `std::string buffer`，每次 `read()` 追加，然后循环找 `\n` 切出完整行。这是最简单的帧协议。

3. **协议设计**：
   ```
   SET <key> <value>\n   → OK\n 或 OK (updated)\n
   GET <key>\n           → VALUE <value>\n 或 NOT_FOUND\n
   DEL <key>\n           → OK\n 或 NOT_FOUND\n
   其他                  → ERR <message>\n
   ```

4. **用 `if constexpr` 还是留 `std::string`**：server 端 KV 的模板参数选 `std::string, std::string` 最简单。key/value 内部的序列化/反序列化在 `load_file()` 里已有 `if constexpr` 处理。

### 你自己的任务

1. 在 server.cpp 引入 `skiplist<std::string, std::string> kv;`
2. 实现行缓冲（`buffer.find('\n')` 循环）
3. 处理 `\r\n`（兼容 telnet）
4. 实现命令行解析（用 `std::istringstream` 或手写空格分割）
5. 调用 kv 方法，格式化响应，write 回去

### 验证

用 `nc` 发送 SET/GET/DEL，验证每种响应。多命令一次发送（`printf "SET a 1\nGET a\n" | nc localhost 9090`）验证行缓冲。

---

## Phase 3：多线程

### 要学什么

每连接一个线程处理，真正让服务端能同时服务多个客户端。

### 核心知识点

1. **`std::thread` + `std::ref`**：skiplist 不可拷贝（有裸指针成员 header），所以传给线程必须用 `std::ref()` 传递引用。

2. **detach vs join**：
   - `detach()`：线程独立运行，主线程不等它。简单但不能知道线程何时结束。
   - `join()`：主线程等它完成。需要保存所有线程句柄。
   - 学习项目用 detach 就够了。

3. **skiplist 自带线程安全**：你已经在每个方法里加了 `ReadLock`/`WriteLock`。多线程同时调用不需要额外同步。

4. **线程开销**：每个线程默认 8MB 栈。1000 连接 = 8GB 虚拟内存，引出后续可学的线程池/epoll。

### 你自己的任务

1. 把客户端处理循环抽成函数 `handle_client(client_fd, kv)`
2. accept 循环中创建线程并 detach
3. accept 错误处理（EINTR 等情况）

### 验证

开多个 terminal 同时连，互不阻塞。写个小脚本批量发请求验证并发。

---

## Phase 4：优雅退出

### 要学什么

Ctrl+C 时 server 不暴力崩，而是"收工"。

### 核心知识点

1. **信号处理器中的限制**：信号处理器内只能用 async-signal-safe 的操作。设置 `std::atomic<bool>` 是安全的，`std::cout`、`malloc`、锁 mutex 都不行。原因是信号可能在持锁时到达，再锁就死锁。

2. **`std::atomic<bool>` 的信号安全**：在 x86 上 `atomic<bool>::store(true)` 就是一条 `mov`，天然信号安全。

3. **EINTR**：信号可能打断阻塞中的 `accept()`，返回 -1，`errno = EINTR`。要检查并据此退出而非报错。

4. **detach 的代价浮现**：主线程退出时不能 join 已 detach 的工作线程。但它们持有的 client_fd 会在下次 read 时失败（对端已断或自己 close），自然退出。够用了。

### 你自己的任务

1. 加 `std::atomic<bool> running{true}`
2. 注册 SIGINT handler 把它设为 false
3. accept 循环检查 running
4. 退出时 close(server_fd)

### 验证

启动 server，Ctrl+C → 看到 shutdown 提示，进程正常退出。

---

## 后续扩展（知道方向就行，不现在做）

| 方向 | 学什么 |
|------|--------|
| 线程池 | mutex + condition_variable，生产者-消费者 |
| epoll | 非阻塞 I/O，一个线程管数千连接 |
| WAL | 写前日志，crash 恢复 |
| 连接超时 | setsockopt SO_RCVTIMEO，或定时器 |

---

## 涉及文件

- `include/skiplist.h` — Phase 0
- `src/server.cpp` — Phase 1~4
- `CMakeLists.txt` — Phase 1

## 完整验证（Phase 4 之后）

```bash
cd build && cmake .. && make -j$(nproc)
./kv_server &
for i in $(seq 1 100); do echo "SET k$i v$i" | nc localhost 9090; done
for i in $(seq 1 100); do echo "GET k$i" | nc localhost 9090; done
# Ctrl+C 退出，然后重新启动验证 load_file 数据恢复
kill %1 && ./kv_server
for i in $(seq 1 100); do echo "GET k$i" | nc localhost 9090; done
```
