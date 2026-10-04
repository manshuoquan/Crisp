# 事件系统调研文档集

本目录是对 Qt 6.2.4 事件系统的完整调研与重写方案设计，服务于"逐步重写 Qt、用现代 C++ 替代其模块"的项目目标。

## 文档索引

| # | 文档 | 内容 | 对应需求 |
|---|------|------|---------|
| 01 | [`01-qt-event-system-architecture.md`](./01-qt-event-system-architecture.md) | Qt QEvent 事件系统**调用结构、类图、流程图** + 文字说明 | 需求 1 |
| 02 | [`02-qt-event-system-pain-points.md`](./02-qt-event-system-pain-points.md) | Qt 事件系统**痛点整理**（含调试不便等 9 类） | 需求 2 |
| 03 | [`03-rewrite-proposal.md`](./03-rewrite-proposal.md) | **重写方案设计**：架构、技术选型、逐步替代路线图 | 需求 3/4/5 |
| 04 | [`04-event-system-redesign.md`](./04-event-system-redesign.md) | **重设计规范**（Rev 13）：接口签名、分发管线、线程/所有权、M1–M4 实施路线 | 需求 3/4/5 |

## 实现状态

- **M1（同步核心）已完成**：`src/event/` 的 `CtEvent`/`CtDispatcher`/`CtReceiver`/`CtTrace` 等，测试 `tests/event/` 通过（§12.1、§13.1/§13.2）。
- **M2（异步核心）已完成**：`CtQueue`/`CtEventLoop`/`CtEventSource`/`CtTimerEventSource`/`ManualEventSource` 与 `post/postTo` 完整语义，测试 `tests/event/test_ct_queue.cpp` 通过（§12.2、§13.3）。
- 示例 `examples/event_demo` 演示 M1 同步 + M2 异步完整链路。

## 快速导览

- **想了解 Qt 事件怎么流转** → 看 01，重点：`sendEvent` vs `postEvent` 两条路径、`notify()` 中央分发、事件过滤器优先级、Windows 事件循环。
- **想知道为什么要重写** → 看 02，重点：调试困难、伪类型系统（int 枚举 + static_cast）、所有权手工管理、notify 上帝函数。
- **想了解新方案长什么样** → 看 03，核心思路：`Event<T>` 值语义强类型事件 + `std::variant` 分发 + 内置 `Trace`/`Observer` 可观测性 + 可组合 `EventSource` + CMake/纯 C++/无 moc/无 mock。

## 调研依据

- 源码：`d:\crisp\qtbase-everywhere-src-6.2.4\src\corelib\kernel\`
  - `qcoreevent.h/cpp`（QEvent、事件类型枚举）
  - `qcoreapplication.cpp`（notify/sendEvent/postEvent/sendPostedEvents/compressEvent）
  - `qeventloop.h/cpp`（QEventLoop）
  - `qabstracteventdispatcher.h/cpp`（分发器抽象）
  - `qeventdispatcher_win.cpp`（Windows 事件循环实现）
  - `qobject.cpp`（event/timerEvent/installEventFilter）
