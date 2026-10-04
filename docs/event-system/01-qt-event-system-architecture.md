# Qt 事件系统架构解析：QEvent 调用结构、类图与流程图

> 基于本仓库下载的 Qt 6.2.4 源码（`qtbase-everywhere-src-6.2.4/src/corelib/kernel/`）分析。
> 核心文件：`qcoreevent.h/cpp`、`qcoreapplication.cpp`、`qeventloop.h/cpp`、`qabstracteventdispatcher.h/cpp`、`qeventdispatcher_win.cpp`、`qobject.cpp`、`qthread.cpp`。

---

## 1. 总览

Qt 的事件系统是一个**单线程、对象归属（thread-affinity）约束、中央分发（central dispatch）** 的机制。它的设计哲学是：

> 事件（QEvent）是"值型"消息载体，对象（QObject）是接收者，事件循环（QEventLoop + QAbstractEventDispatcher）是驱动源，QCoreApplication::notify() 是唯一的中央分发入口。

核心参与者：

| 角色 | 类 | 职责 |
|------|-----|------|
| 事件本体 | `QEvent` + 数百个子类 | 携带事件类型和负载数据 |
| 接收者 | `QObject` | 提供 `event()`、`eventFilter()`、各 `xxxEvent()` 虚函数 |
| 分发中枢 | `QCoreApplication` | `notify()` / `sendEvent()` / `postEvent()` / `sendPostedEvents()` |
| 事件循环 | `QEventLoop` | `exec()` / `processEvents()` / `exit()` |
| 平台驱动 | `QAbstractEventDispatcher` | `processEvents()` / 定时器 / socket notifier / native 事件过滤 |
| 线程容器 | `QThreadData` | 每线程一份 posted 事件队列（`QPostEventList`） |

---

## 2. 类图

### 2.1 QEvent 类层次（核心部分）

```mermaid
classDiagram
    class QEvent {
        <<abstract base>>
        -Type m_type
        -bool m_accepted
        -bool m_spont
        -bool m_posted
        +type() Type
        +accept() void
        +ignore() void
        +isAccepted() bool
        +setAccepted(bool) void
        +spontaneous() bool
    }

    QEvent <|-- QTimerEvent
    QEvent <|-- QMouseEvent
    QEvent <|-- QKeyEvent
    QEvent <|-- QPaintEvent
    QEvent <|-- QResizeEvent
    QEvent <|-- QMoveEvent
    QEvent <|-- QCloseEvent
    QEvent <|-- QFocusEvent
    QEvent <|-- QWheelEvent
    QEvent <|-- QChildEvent
    QEvent <|-- QDeferredDeleteEvent
    QEvent <|-- QAbstractMetaCallEvent
    QEvent <|-- QEvent_Type_User : User+ 自定义事件

    QMouseEvent <|-- QEnterEvent
    QMouseEvent <|-- QHoverEvent

    class QTimerEvent {
        +int m_id
        +timerId() int
    }

    class QDeferredDeleteEvent {
        +int m_level
        +loopLevel() int
    }

    class QEvent_Type_User {
        <<用户自定义>>
        Type >= QEvent::User
    }
```

### 2.2 事件类型枚举（QEvent::Type）

```mermaid
flowchart LR
    subgraph 系统事件 0..User
        A[None=0] --> B[Timer=1]
        B --> C[MouseButtonPress=2 ... MouseMove=5]
        C --> D[KeyPress=6 ... KeyRelease=7]
        D --> E[FocusIn=8 ... FocusOut=9]
        E --> F[Paint=12 / Move=13 / Resize=14]
        F --> G[Show=17 / Hide=18 / Close=19 / Quit=20]
        G --> H[DeferredDelete=52]
        H --> I[ChildAdded=68 ... 等 100+ 种]
    end
    subgraph 用户事件
        J[User = 1000] --> K[User + n : 自定义]
    end
```

**要点**：
- 事件类型是一个 **int 枚举**，而非类型系统的一部分。`QEvent::Type` 是区分事件子类的唯一手段，全部通过 `switch(e->type())` 分发。
- `QEvent::User`（=1000）以上留给应用程序自定义。Qt 自身预留了 `QEvent::MaxUser` 上限。
- 枚举值一旦发布**不可变更**（ABI/源码兼容承诺），新增事件只能往后追加。

### 2.3 事件分发链路（调用结构）

```mermaid
flowchart TD
    subgraph 生产者 Producer
        OS[操作系统<br/>Win32 MSG / X11 / Wayland]
        APP[应用程序代码<br/>QCoreApplication::postEvent]
        OBJ[QObject 内部<br/>startTimer / socket]
    end

    subgraph 队列 Queue
        QPL[QThreadData.postEventList<br/>QPostEventList 每线程一份]
    end

    subgraph 事件循环 EventLoop
        AED[QAbstractEventDispatcher<br/>processEvents 纯虚接口]
        WIN[QEventDispatcherWin32<br/>PeekMessage/MsgWaitForMultipleObjectsEx]
        EL[QEventLoop<br/>exec/processEvents]
    end

    subgraph 分发 Dispatch
        CA[QCoreApplication]
        N[notify 中央入口]
        NF[notifyInternal2<br/>scopeLevel 计数]
        DH[doNotify]
        NH[notify_helper]
        AF[应用级事件过滤器<br/>Application Event Filters]
        OF[对象级事件过滤器<br/>Object Event Filters]
        OBJ2[receiver->event 虚函数]
    end

    subgraph 消费 Consumer
        EW[QWidget::event<br/>先分发给子类专用处理器]
        EH[xxxEvent 处理器<br/>paintEvent/mousePressEvent...]
        EDF[eventFilter 过滤器链]
    end

    OS -->|原生消息| WIN
    APP -->|postEvent 入队| QPL
    OBJ -->|registerTimer| WIN
    QPL -->|wakeUp 唤醒| WIN
    WIN -->|取消息 TranslateMessage/DispatchMessage| EL
    EL -->|processEvents| AED
    AED -->|QCoreApplicationPrivate::sendPostedEvents| CA
    CA -->|postEvent/sendEvent| N
    N --> NF
    NF --> DH
    DH --> NH
    NH --> AF
    NH --> OF
    NH --> OBJ2
    OBJ2 --> EW
    EW --> EH
    OBJ2 -.-> EDF
```

---

## 3. 事件流转的完整流程

### 3.1 事件的两条入口：send 与 post

Qt 提供两类投递方式，语义差异极大：

```mermaid
flowchart LR
    subgraph 同步投递 sendEvent
        S1[QCoreApplication::sendEvent] --> S2[立即调用 notify]
        S2 --> S3[同步执行 event 处理器]
        S3 --> S4[调用者阻塞直到返回]
    end

    subgraph 异步投递 postEvent
        P1[QCoreApplication::postEvent] --> P2[入队 receiver 所在线程的 postEventList]
        P2 --> P3[按 priority 降序排序]
        P3 --> P4[置 canWait=false 并 wakeUp 事件分发器]
        P4 --> P5[控制权立即返回]
        P5 -.-> P6[下次事件循环迭代时 sendPostedEvents 取出并 notify]
    end
```

**关键差异**：
- `sendEvent`：同步、栈上事件、调用者驱动、**跨线程调用是未定义行为（实际会崩溃）**。
- `postEvent`：异步、**堆上事件（队列拥有所有权并负责 delete）**、线程安全（锁 `postEventList.mutex`）、投递后调用者**不得再触碰事件指针**。
- `sendSpontaneousEvent`：标记 `m_spont=true`（来自窗口系统的原生事件），走与 sendEvent 相同的 notify 路径。

### 3.2 notify() 中央分发详细流程

```mermaid
flowchart TD
    A[notify receiver event] --> B{应用正在关闭?}
    B -->|是| C[直接返回 true 不再投递]
    B -->|否| D[doNotify]
    D --> E{receiver 为空?}
    E -->|是| F[qWarning 并返回]
    E -->|否| G{是 QWidget 类型?}
    G -->|是| H[返回 false<br/>widget 事件由 QApplication 处理]
    G -->|否| I[notify_helper]
    I --> J{应用级过滤器<br/>sendThroughApplicationEventFilters}
    J -->|任一过滤器返回 true| K[事件被吞掉 filtered=true]
    J -->|都不拦截| L{对象级过滤器<br/>sendThroughObjectEventFilters}
    L -->|任一过滤器返回 true| M[事件被吞掉 filtered=true]
    L -->|都不拦截| N[receiver->event event]
    N --> O[返回 consumed 结果]
```

**事件过滤器（Event Filter）优先级规则**（来自源码注释与实现）：
1. 应用级过滤器（`QCoreApplication::installEventFilter`）— 只作用于主线程
2. 对象级过滤器（`QObject::installEventFilter`）— 按安装顺序逆序调用，后装先得
3. `receiver->event()` — 对象自身的总入口
4. QWidget::event 内部再先派发给专用事件处理器（如 `paintEvent`）

任何一级过滤器返回 `true` 即吞掉事件，不再向下传递。

### 3.3 QObject::event() 的分派逻辑

```mermaid
flowchart TD
    A[QObject::event e] --> B{switch e->type}
    B -->|Timer| C[timerEvent]
    B -->|ChildAdded/Polished/Removed| D[childEvent]
    B -->|DeferredDelete| E[qDeleteInEventHandler<br/>事件处理完毕后 delete this]
    B -->|MetaCall| F[placeMetaCall<br/>跨线程信号槽调用]
    B -->|ThreadChange| G[迁移定时器到新线程的事件分发器]
    B -->|>= QEvent::User| H[customEvent]
    B -->|其他| I[return false<br/>事件未被消费]
```

---

## 4. 定时器子系统

定时器是事件系统与平台耦合最深的子系统之一：

```mermaid
flowchart LR
    subgraph 用户层
        T[QTimer 高层封装<br/>signal timeout]
        BT[QBasicTimer 轻量封装]
        ST[QObject::startTimer<br/>返回 timerId]
    end

    subgraph 分发层
        AED2[QAbstractEventDispatcher::registerTimer<br/>纯虚接口]
        TI[TimerInfo<br/>timerId interval timerType]
    end

    subgraph 平台层 Win32
        W1[QEventDispatcherWin32::registerTimer<br/>SetTimer 或 timeSetEvent]
        W2[WM_TIMER 消息<br/>internalHwnd]
        W3[定时器事件压缩<br/>同 timerId 去重]
    end

    T --> BT --> ST
    ST --> AED2
    AED2 --> TI
    AED2 --> W1
    W1 --> W2
    W2 -->|QTimerEvent 投递| QPL2[postEventList]
    W1 --> W3
```

**要点**：
- 定时器 ID 由事件分发器分配，`QObject::startTimer` 返回的 int 只是句柄。
- `QTimerEvent` 通过 `postEvent` 异步投递，因此**定时器事件是排队事件**，精度受事件循环负载影响。
- `Qt::PreciseTimer`（Windows 上 `timeSetEvent`）与 `CoarseTimer` 的取舍是平台相关的。

---

## 5. 线程模型与事件队列

```mermaid
flowchart TD
    subgraph 主线程
        MTD[QThreadData#1<br/>postEventList#1<br/>eventDispatcher#1]
        MCA[QCoreApplication 单例<br/>只存在于主线程]
    end

    subgraph 工作线程
        WTD[QThreadData#2<br/>postEventList#2<br/>eventDispatcher#2]
    end

    MCA -->|postEvent 查找 receiver 的 threadData| MTD
    OBJ2[任意线程代码<br/>postEvent 给工作线程对象] -->|锁 postEventList.mutex| WTD
    WTD -->|wakeUp| W2[工作线程事件分发器]
    MTD -->|sendPostedEvents| MCA

    style MCA fill:#f66
```

**核心规则**：
- 每个线程有独立的 `QThreadData`（posted 队列 + 事件分发器实例）。
- `postEvent` 是**唯一线程安全**的投递方式：它会跟随 receiver 找到其所在线程的队列并加锁。
- `sendEvent`/`notify` 只能投递给**当前线程**的对象（`checkReceiverThread` 断言）。
- `QCoreApplication` 单例只在主线程；应用级过滤器只处理主线程对象的事件。
- 跨线程调用（信号槽 QueuedConnection、`QMetaObject::invokeMethod`）最终都编码为 `QMetaCallEvent` 投递到目标线程队列。

---

## 6. Windows 平台事件循环（QEventDispatcherWin32::processEvents）

```mermaid
flowchart TD
    A[processEvents 入口] --> B[发送 posted 事件<br/>每轮迭代一次 防活锁]
    B --> C{interrupt 标志?}
    C -->|是| Z[返回]
    C -->|否| D{有排队的用户输入/套接字事件?}
    D -->|是| E[取出处理]
    D -->|否| F{PeekMessage 有消息?}
    F -->|是| G{是 WM_QT_SENDPOSTEDEVENTS?}
    G -->|是| H[startPostedEventsTimer 继续]
    G -->|否| I{是 WM_TIMER?}
    I -->|是| J[记录已处理 timer 防活锁]
    I -->|否| K{是 WM_QUIT?}
    K -->|是| L[app->quit 返回]
    K -->|否| M[filterNativeEvent<br/>TranslateMessage + DispatchMessage]
    F -->|否| N{MsgWaitForMultipleObjectsEx<br/>WAIT_OBJECT_0?}
    N -->|是| O[新消息到达 继续循环]
    N -->|否| P{canWait 且 WaitForMoreEvents?}
    P -->|是| Q[emit aboutToBlock<br/>阻塞等待 INFINITE]
    Q --> R[emit awake 唤醒]
    P -->|否| Z
```

**要点**：
- 事件循环本质是 **Win32 消息泵的包装**：`PeekMessage` 非阻塞轮询 + `MsgWaitForMultipleObjectsEx` 阻塞等待。
- `wakeUp()` 通过 `PostMessage(WM_QT_SENDPOSTEDEVENTS)` 跨线程唤醒阻塞中的循环。
- `aboutToBlock` / `awake` 信号供外部（如网络库）挂接。
- 原生事件先过 `filterNativeEvent`（QAbstractNativeEventFilter 链），再翻译分发。
