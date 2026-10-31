# M2 核心修订登记：CtQueueBase::waitForEvent 超时重载

> 状态：**已登记（UI3 前置修订）**
> 触发：UI3 架构审查 MUST-1（docs/ui/06-ui3-architecture.md §4.2/D10）——`CtEventLoop::exec` 的 `waitForEvent()` 是带谓词等待（`m_cv.wait(lock, [this]{ return !m_events.empty() || m_closed; })`），Win32 消息队列不是 `CtQueue`，新消息不唤醒条件变量，exec 一旦阻塞永不回到 poll → `PeekMessageW` 饿死。
> 注：04-event-system-redesign.md 中文乱码（UTF-8/GBK 双重编码，历史遗留）无法直接修订，本文件为**可读的修订登记承接**（UI3 §16 风险表指针）。

---

## 1. 变更内容

`CtQueueBase::waitForEvent` 增加**超时重载**（M2 核心，已部署模块——走文档修订门禁）：

```cpp
// ct_queue.h — M2 修订（UI3 前置，D10/MUST-1）：
/// Wait for events with a timeout. Returns TRUE if events are pending
/// (or the queue is closed) within `timeout`; FALSE on timeout.
/// The caller (CtEventLoop::exec) uses this when platform sources that
/// need polling wakeups are attached — a timed wait lets the loop
/// return to poll() and drain PeekMessageW (UI3).
virtual bool waitForEvent(std::chrono::milliseconds timeout) = 0;
```

实现（`CtQueue`，`ct_queue.inl`）：

```cpp
bool CtQueue::waitForEvent(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_cv.wait_for(lock, timeout,
                         [this] { return !m_events.empty() || m_closed; });
}
```

## 2. 集成规则（CtEventLoop::exec）

- **判定机制（P1 补齐）**：`CtEventSource` 增加虚接口 `virtual bool needsPollingWakeup() const noexcept { return false; }`——`CtWin32EventSource` 返回 true（其消息队列是平台队列，不经 `CtQueue` 唤醒）。
- **exec 判定**：attach 的源中**任一 `needsPollingWakeup()` 为 true** → 用**超时等待**（16ms）；否则保持原无超时等待（纯队列语义）。
- 超时后回到 poll 阶段，执行各源 `poll(queue)`（Win32 源 `PeekMessageW`）。

## 2.5 变更清单（M2 核心）

| 文件 | 变更 |
|------|------|
| `src/event/ct_queue.h/.inl` | `waitForEvent` 加超时重载（纯虚 + `m_cv.wait_for` 实现） |
| `src/event/ct_event_source.h` | 加 `needsPollingWakeup()` 虚接口（默认 false） |
| `src/event/ct_event_loop.cpp` | exec 判定（任一源 needsPollingWakeup → 超时等待 16ms） |

## 3. 兼容性

- 原 `waitForEvent()` 无参版本保留（纯队列语义的默认路径）；超时重载为新增纯虚——**所有既有 `CtQueue` 实现需补该重载**（本项目仅一个实现）。
- 不改变既有 API 行为（无源/无轮询需求时行为不变）。

## 4. 关联

- UI3：docs/ui/06-ui3-architecture.md §4.2（CtWin32EventSource）/§10 D10/§16 风险表。
- 04-event-system-redesign.md §14 开放问题 #8（"M4 加平台等待"——本修订为 UI3 前置的局部前移；完整 epoll/select 平台等待仍留 M4）。

---

## READY_FOR_REVIEW
