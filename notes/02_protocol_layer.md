# 协议层复习笔记 — 行缓冲与命令解析

## 1. 为什么需要行缓冲？— TCP 的"无边界"特性

TCP 是**字节流**，不保证边界与发送方一致：

```
发送方 write("SET k v\n") write("GET k\n") write("DEL k\n")

接收方 read() 可能：
- 情况A（理想）: "SET k v\n"
- 情况B（粘连）: "SET k v\nGET k\nDEL k\n"  ← 三行粘在一起
- 情况C（断层）: "SET ke"  + 下次 read: "y1 val\nGET k\n"
```

不做行缓冲 → 情况B丢失后续命令，情况C解析出半截数据。

## 2. 行缓冲循环的机制

```
buffer:  累积区（std::string），可能含 0~多行
temp:    固定 1024 字节栈数组，仅用于 read() 接收

流程：
  ① read(clientfd, temp, 1023)  从内核读原始数据
  ② buffer.append(temp, bytes)  追加到累积区
  ③ while(find('\n') != npos)   反复切出完整行
  ④ line = buffer.substr(0, pos)  取出一行
  ⑤ buffer.erase(0, pos+1)        删掉已处理部分
  ⑥ 解析并处理该行
```

不管 TCP 怎么粘连/断层，最终都是一行一行完整被提出来。

## 3. 命令解析 — istringstream

```cpp
std::istringstream iss(line);
std::string cmd, key, val;
iss >> cmd >> key >> val;
```

- `operator>>` 以空白字符（空格、\t）为分隔符，跳过前导空白
- `"SET mykey myval"` → cmd=SET, key=mykey, val=myval
- `"GET mykey"` → cmd=GET, key=mykey, val=""

## 4. 边界问题与解决方案

### 4.1 Value 含空格

`"SET key hello world"` → val 只拿到 "hello"，"world" 丢失

### 4.2 缓冲区无限膨胀（DoS 攻击向量）

恶意客户端发永不换行的数据 → buffer 无限增长 → OOM

**修复**：设 MAX_BUFFER 上限，超限断开连接。

### 4.3 行业上 Key/Value 含空格的解决方案

| 方案 | 做法 | 代表 |
|------|------|------|
| 长度前缀 | 先发长度再发内容 | BSON, MessagePack |
| 转义/引号 | 空格转义或引号包裹 | CSV, shell |
| RESP 协议 | 二进制安全，`$长度\r\n<数据>` | Redis |

推荐了解 **RESP（Redis Serialization Protocol）**，文本协议和二进制协议之间的优雅平衡点：

```
*3\r\n          ← 3个参数
$3\r\nSET\r\n   ← 3字节: SET
$6\r\nmy key\r\n ← 6字节: my key（含空格）
$5\r\nvalue\r\n  ← 5字节: value
```

## 5. \r\n 兼容处理

```cpp
if(!line.empty() && line.back() == '\r')
    line.pop_back();  // "SET k v\r" → "SET k v"
```

兼容 Windows/telnet 客户端（发 \r\n），对 Linux netcat（发 \n）无害。
