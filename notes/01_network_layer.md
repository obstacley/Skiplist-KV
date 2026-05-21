# 网络层复习笔记 — socket() → bind() → listen() → accept()

## 1. socket() — 向内核申请通信端点

```cpp
int serverfd = socket(AF_INET, SOCK_STREAM, 0);
```

| 参数 | 值 | 含义 |
|------|-----|------|
| domain | AF_INET | IPv4 |
| type | SOCK_STREAM | 字节流（TCP） |
| protocol | 0 | 内核自动选择 |

- 返回的只是一个"空壳" fd，此时 TCP 状态机未启动，不能 read/write
- 对未连接的 SOCK_STREAM 调 `read()` → 返回 -1，`errno = ENOTCONN`

## 2. setsockopt() — 端口复用

```cpp
int opt = 1;
setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

- **目的**：允许 bind 到处于 TIME_WAIT 状态的地址/端口
- **为什么需要**：服务器主动 close 后，连接进入 TIME_WAIT（2MSL ≈ 60s），重启时端口可能被锁
- **opt 的值**：0 = 关，非零 = 开（惯例写 1）
- **更优做法**：`SO_REUSEADDR` 是最干净标准的方案；`SO_LINGER` 强制 RST 粗暴但可能丢数据

### setsockopt 的其他可设选项（非布尔值）

| 选项 | optval 类型 | 示例值 |
|------|------------|--------|
| SO_RCVBUF/SO_SNDBUF | int | 262144（缓冲区字节数） |
| SO_RCVTIMEO/SO_SNDTIMEO | struct timeval | {2, 0}（2秒超时） |
| SO_LINGER | struct linger | {1, 0}（close 发 RST） |
| TCP_NODELAY | int | 1（禁用 Nagle） |

### SO_REUSEADDR 在不同平台的差异

| 平台 | 行为 |
|------|------|
| Linux | 允许 bind 到 TIME_WAIT 端口 |
| BSD/macOS | 更激进，类似 Linux 的 SO_REUSEPORT |
| Windows | 类似 Linux 但语义更宽 |

Linux 3.9+ 引入了 SO_REUSEPORT，允许多进程同时 bind 同一端口，内核做负载均衡。

## 3. bind() — 给 socket 分配地址

```cpp
struct sockaddr_in address;
memset(&address, 0, sizeof(address));   // 防御：清零避免padding垃圾值
address.sin_family = AF_INET;           // 地址族，必须与socket()一致
address.sin_addr.s_addr = INADDR_ANY;   // 0.0.0.0，监听所有网卡
address.sin_port = htons(PORT);         // 主机字节序 → 网络字节序（大端）
bind(serverfd, (struct sockaddr*)&address, sizeof(address));
```

### 字节序与 htons()

- **主机字节序**：x86 是小端（低位在前）
- **网络字节序**：规定大端（高位在前）
- `htons()` = Host TO Network Short（16位），把端口号转成网络字节序
- 忘了写 htons → x86 上 9090 变成 33301
- 兄弟函数：`htons/s` `htonl` `ntohs` `ntohl`（to=大端序, from=主机序）

### bind 失败的常见 errno

| errno | 原因 |
|-------|------|
| EADDRINUSE | 端口已被占用（ss -tlnp 可查） |
| EACCES | 无权限（<1024 端口需 root） |
| EADDRNOTAVAIL | 指定 IP 不属于本机 |
| EBADF | fd 无效（忘了调 socket 或 socket 已失败） |

### sockaddr* 强制转型

- `sockaddr_in` 是 IPv4 专用结构体
- `bind()` 签名接收通用 `sockaddr*`
- C 语言多态：不同地址族各有结构体，统一收基类指针
- C++ 中可以用 `getaddrinfo()` 自动填充，更可移植

## 4. listen() — 进入被动监听模式

```cpp
listen(serverfd, 5);
```

- 做了两件事：
  1. 内核将 socket 迁到 **LISTEN** 状态（`ss -tlnp` 可看到）
  2. 创建两个队列

### 两个队列

```
客户端 SYN → [SYN队列(半连接)] → 握手完成 → [ACCEPT队列(全连接)] → accept() 取走
```

- **SYN 队列**（半连接）：收到 SYN，尚未完成三次握手
- **ACCEPT 队列**（全连接）：握手已完成，等待 accept() 取走
- `listen(fd, 5)` 的 backlog=5 控制的是 **ACCEPT 队列上限**（Linux）

### backlog 和生产环境

- 测试学习场景：5 足够
- 生产环境：`listen(fd, SOMAXCONN)` 使用内核允许的最大值

### SYN Flood 攻击

- 攻击目标：**SYN 队列**（半连接队列）
- `listen()` 的 backlog 控制的是 ACCEPT 队列，**对防御 SYN Flood 基本没用**
- 真正防御手段：
  - **SYN Cookie**（`tcp_syncookies=1`）：不立刻为 SYN 分配资源，把信息编码进 SYN-ACK 序列号
  - 增大 `tcp_max_syn_backlog`
  - 缩短 `tcp_synack_retries`

## 5. errno

- 线程局部的全局 `int`，定义在 `<errno.h>`
- 系统调用返回 -1（失败）时，写入具体错误原因
- **判断流程**：先看返回值，只有失败时才查 errno
- 成功时 errno 不会被清零，可能残留旧值，所以不能反过来用

| 常见 errno | 含义 |
|-----------|------|
| EINTR | 被信号中断 |
| EAGAIN/EWOULDBLOCK | 非阻塞操作暂时无数据 |
| ECONNRESET | 对端发送 RST |
| ENOTCONN | socket 未连接 |
| EADDRINUSE | 地址已被占用 |
| EACCES | 权限不足 |

## 6. accept() — 从 ACCEPT 队列取连接

```cpp
int clientfd = accept(serverfd, (struct sockaddr*)&client_addr, &client_len);
if (clientfd == -1) {
    if (errno == EINTR) {           // 被信号打断
        if (!running.load()) break; // 退出信号 → 退出循环
        continue;                   // 其他信号 → 重试
    }
    // 真正的错误
}
```

- 阻塞调用：队列空时阻塞等待
- 返回新的 fd（clientfd），与原 serverfd 独立
- **EINTR 的特殊处理**：`accept()` 被信号打断返回 -1，不代表服务端出了问题，必须检查 errno 区分
