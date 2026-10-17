# Qt 多线程痛点分析

> 本文基于 Qt 6.2.4 源码研读（`src/corelib/thread/`、`src/concurrent/`）与工程实践，分析 Qt 多线程的痛点。
> 目的：为"重写多线程子系统"提供明确的问题清单与改进方向。痛点按严重程度分级：🔴 严重 / 🟠 中等 / 🟡 轻微。

---

## 1. 🔴 QThread 生命周期陷阱

### 1.1 线程结束 ≠ 线程对象安全

`QThread` 的 `run()` 返回只意味着线程主体函数结束，**线程内核对象仍在收尾**。常见崩溃：

- **析构时线程仍在 running**：`QThread` 析构函数对 running 线程 `qWarning` 并中止（abort 语义）。
- **`quit()` 后直接析构**：`quit()` 只是请求退出事件循环，队列可能还有事件；不等 `wait()` 就析构，是悬挂态。
- **`wait()` 死锁**：在 UI 线程调用 `wait()` 等 worker，若 worker 又在等 UI 线程（如 `BlockingQueuedConnection` 回调），互等死锁。

**本质**：线程生命周期是"手工约定"，没有任何类型或 RAII 机制强制"先 join/quit 再析构"。

### 1.2 `moveToThread` 的隐式契约

`moveToThread(t)` 后，worker 对象归属 t 线程。但：

- 线程外直接调用 worker 的方法（非信号槽）是 **UB**，只有文档约束，无编译期/运行期检查。
- `moveToThread` 只能在"对象当前线程"调用（跨线程 move 本身要排队）。
- 对象被移走后，原线程的信号槽连接语义悄悄变化（Auto→Queued），**调用方无感知**。

**本质**：线程归属是"注释级别的契约"，Qt 靠 `Q_ASSERT(q_ptr->thread() == currentThread)` 在**部分**入口自检，覆盖面不全，且 release 下不生效。

---

## 2. 🔴 AutoConnection 的"碰运气"跨线程

```cpp
// 用户代码：不知道 a 和 b 是否同线程，Qt 运行时决定直连还是排队
QObject::connect(a, &A::sig, b, &B::slot); // Qt::AutoConnection 是默认
```

- `Qt::AutoConnection` 在**信号发射时**才根据 `sender`/`receiver` 线程运行时决定：同线程→直连，跨线程→Queued。**类型系统对跨线程语义零感知**。
- `BlockingQueuedConnection` 阻塞等待槽执行完，在 UI 线程用极易死锁（槽又反过来等 UI 线程）。
- 后果：同样的 `connect` 代码，线程配置不同行为就不同；跨线程调用的"排队性"是碰运气的。

**本质**：跨线程通信的语义（同步/异步、谁排队、是否阻塞）没有在**调用点**显式表达，而是隐藏在连接参数里。

---

## 3. 🟠 QFuture 不可取消 + QFutureWatcher 回调啰嗦

### 3.1 取消是协作式且不彻底

- `QFuture::cancel()` 只置 Canceled 状态并请求中断；**正在执行的任务不能强制停**（线程引擎协作检查）。
- 任务函数本体无法感知"被取消"——除非自己轮询取消状态（`QPromise::isCanceled()` 面向任务侧已存在，6.2.4 `qpromise.h`，可减少样板；但任务侧与 `QFuture` 消费者侧的取消状态同步仍靠约定），样板代码重复。
- 取消后结果存储的状态处理（半途 result 丢弃）容易出隐蔽 bug。

### 3.2 QFutureWatcher 回调依赖事件循环 + 回调啰嗦

- `QFutureWatcher` 是 QObject，信号（`resultReadyAt`/`finished`）经 QueuedConnection 投回 watcher 线程——**监听线程必须有运行中的 `exec()`**，否则回调永远不到。
- 无 `exec()` 的线程（纯 worker 线程）收不到 watcher 信号，只能手动 `waitForFinished`。
- 每结果回调（`resultReadyAt(i)`）粒度太细、样板多；批量完成只有 `finished`，中间进度要自己维护计数。

**本质**：完成回调的"投递目标线程"与"执行线程"混在一个 QObject 里，语义隐晦；且回调形态单一（信号），无"注册任意线程回调"的显式机制。

---

## 4. 🟠 QRunnable 类型擦除（无类型安全）

```cpp
class MyTask : public QRunnable {
public:
    void run() override {
        // 结果只能存成员字段，外部用互斥锁读
        m_result = compute(); // int 成员
    }
    int m_result; // 访问要自己加锁
};
```

- `QRunnable` 只有 `void run()`，**无返回类型**；结果靠成员 + 外部互斥访问，类型擦除。
- 异常：`run()` 抛异常 → 线程池 worker 崩（Qt 会 qWarning，但无异常传播通道）。
- 无取消、无进度、无任务标识——一切自己造。

**本质**：任务执行模型缺少类型化结果与错误通道，是 Qt 多线程里"裸奔"最严重的一层。

---

## 5. 🟠 完成回调强制回事件循环（与 L4 耦合）

- 信号槽 QueuedConnection / watcher 信号 / `invokeMethod(Queued)` **全部依赖目标线程的 `QAbstractEventDispatcher` + posted 队列**。
- 没有事件循环的线程（`std::thread` 直接跑的 worker）**收不到任何排队回调**。
- **注意**：QFuture 系另有一条不依赖事件循环的完成通道——`setContinuation`/`then()`（见 01 §3.4）由报告线程直接调用；"必须回事件循环"仅限 QObject 系（信号槽/watcher/invokeMethod）。但 continuation 的投递目标线程同样是隐式的（执行策略在 API 参数里，类型无感知）。

**本质**：Qt 把"投递"藏在连接机制后面，可观测性为零——投递是否到达、何时到达、被谁消费，全黑盒；且 QObject 系与 QFuture 系两条通道并存，选择依据不显式。

---

## 6. 🟡 QThreadStorage vs thread_local、QAtomic vs std::atomic 冗余

- `QThreadStorage<T>` 等价 `thread_local` + 额外 QObject 清理；`QAtomicInteger` 等价 `std::atomic`。
- Qt 6 已明确这些是 std 薄封装，**重写无增量价值**。
- 双套 API 增加认知负担（记两套语义、两套坑）。

---

## 7. 🟡 无任务轨迹/可观测性

- 任务从提交 → 入队 → 执行 → 完成/取消，**全链路不可查**（与 Qt 事件系统同病，见 `02-qt-event-system-pain-points.md` §1）。
- 没有"任务被谁取消""任务卡在队列多久""哪个任务拖垮线程池"这类事后排查手段。
- 线程池状态（活跃线程、队列长度、被拒任务）只有手动轮询 API，无统一观测钩子。

---

## 8. 痛点 → CRISP 现状对照

| # | Qt 痛点 | CRISP 现状（M1/M2） | 本次要解决的 |
|---|---------|--------------------|-------------|
| 1 | QThread 生命周期陷阱 | 无对应（未做线程生命周期） | ✅ MT1 CtThread RAII |
| 2 | AutoConnection 碰运气 | ✅ 已解决：`post/postTo` 显式分型 | 部分（任务完成回传沿用显式投递） |
| 3 | QFuture 不可取消 / watcher 啰嗦 | 无对应 | ✅ MT2 CtFuture + 事件桥 |
| 4 | QRunnable 类型擦除 | 无对应 | ✅ MT1 CtTask<T> 强类型 |
| 5 | 完成回调强制回事件循环 | ✅ 已解决一半：`CtEventLoop` + 路由表 | ✅ MT2 事件桥显式化 |
| 6 | 原语冗余 | ✅ 已定案直接用 std | 不重写 |
| 7 | 无任务轨迹 | ✅ 已有 `CtTrace` 框架 | ✅ MT1 扩展任务相位 |

---

> 下一份文档：`03-rewrite-proposal.md`（重写方案）。
