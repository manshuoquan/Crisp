# CRISP 多线程子系统（crisp::thread）设计文档

> 目标：用现代 C++（C++17）逐步替代 Qt 的多线程模块（`QThread`/`QThreadPool`/`QFuture`/`QtConcurrent`），作为事件系统 M2"执行侧"的延伸。

## 文档系列

| 编号 | 文档 | 内容 |
|------|------|------|
| 01 | [01-qt-threading-architecture.md](./01-qt-threading-architecture.md) | Qt 6 多线程架构调研（全景分类、场景分型、Qt6 变化、三条核心规则） |
| 02 | [02-qt-threading-pain-points.md](./02-qt-threading-pain-points.md) | Qt 多线程痛点分析（🔴/🟠/🟡 分级） |
| 03 | [03-rewrite-proposal.md](./03-rewrite-proposal.md) | crisp::thread 重写方案（调研版，六原则映射、模块划分、事件回传桥） |

## 里程碑

| 里程碑 | 内容 | 状态 |
|--------|------|------|
| MT0 | 调研文档（01/02/03） | ✅ 过架构审查 |
| MT1 | CtThread RAII + CtThreadPool + CtTask\<T\> | ⬜ 未开始 |
| MT2 | CtFuture\<T\> + 线程→事件回传桥 | ⬜ 未开始 |
| MT3 | CtParallel::map/filter/reduce | ⬜ 未开始 |
| MT4 | work-stealing / CtPromise\<T\>（可选） | ⬜ 未开始 |

## 关键咬合点

- 复用：`CtThreadId`/`CtThreadAffinity`/`CtQueue` 路由表/`CtTrace`（见 [03-rewrite-proposal.md §2.3](./03-rewrite-proposal.md)）
- 不使用：`CtEventLoop::injectEvent`（语义不符，见 03 §2.3 注）
- 完成回调唯一通道：`post/postTo` + `registerThreadQueue` 路由表

## 关联文档

- 事件系统（前置）：[docs/event-system/](../event-system/README.md)
