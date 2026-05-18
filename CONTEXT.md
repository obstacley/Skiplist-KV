# CONTEXT.md — 对话记忆

## 当前状态

跳表 KV 项目核心功能完成。Skiplist（C++17 跳表 + shared_mutex 并发控制）+ TCP Server（多线程、行缓冲协议、优雅退出）。当前处于**巩固复习阶段**，暂不推进新功能。

## 下一步：巩固复习（4 个话题，用户主讲、Claude 追问）

| 序号 | 话题 | 核心要讲清的内容 |
|------|------|-----------------|
| 1 | 网络层 | socket→bind→listen→accept 每个做什么、顺序为什么固定 |
| 2 | 协议层 | 为什么需要行缓冲、`find('\n')` 循环怎么工作 |
| 3 | 并发层 | 线程是什么、`std::ref` 必要性、detach vs join |
| 4 | 存储层 | 跳表 insert/delete/search 逻辑、锁加在哪、为什么 |

## 性能优化方向（已确认，暂不推进）

**分段锁（ShardedSkiplist）确认为最佳方案**。对比结论：

| 方案 | 改动量 | 难度 | 预期提升 | 风险 |
|------|--------|------|----------|-----|
| 分段锁 | ~110行 | 中 | 4~6x | 热点 key 仍竞争 |
| Lock-Free CAS | 重写 | 极高 | 10x+ | ABA、内存回收、难调试 |
| 乐观锁+验证 | ~200行 | 高 | 3~5x | 回滚逻辑复杂 |

分段锁性价比最高——改动最小、能学会"降低锁粒度"核心思想、QPS 提升立竿见影。Lock-Free 是终极方案但需要先掌握 memory order、CAS、ABA、Hazard Pointer/Epoch-Based Reclamation。

## 已知待修复问题

- `reponse` 拼写错误（server.cpp 通篇）
- skiplist.h 中注释掉的死代码（旧 ofstream 版 dump_file）
- `element_count` 非 atomic，get_size() 存在数据竞争
- test/ 目录为空，无单元测试
- 无 namespace

## 未来项目规划

详细内容见 `/home/pickchu/claudepj/ROADMAP.md`。核心思路：

1. **HNSW 向量索引引擎**——跳表思想的高维推广，触及 SIMD 优化、内存布局、图算法
2. **Agent Memory Backend**——将自研 HNSW 包装为 Memory 服务

**秋招定位**：C++ AI 基础设施 / 向量数据库内核 / 搜索推荐引擎。卡位逻辑：AI 工程师不会 C++，C++ 工程师不懂 AI，中间地带竞争小、壁垒高。

## 项目协作约定

- 用户亲手写所有代码，Claude 负责教学、review、答疑
- 先理解原理再动手，节奏以学会为准而非完成功能
- 参考 CLAUDE.md 和 memory（learning-not-delivering）
