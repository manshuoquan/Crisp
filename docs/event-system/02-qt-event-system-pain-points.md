# Qt 事件系统痛点分析

> 本文基于对 Qt 6.2.4 源码（`qcoreapplication.cpp`、`qeventdispatcher_win.cpp`、`qobject.cpp`、`qcoreevent.h`）的研读，结合工程实践整理。
> 目的：为"重写事件系统"提供明确的问题清单与改进方向。痛点按严重程度分级：🔴 严重 / 🟠 中等 / 🟡 轻微。

---

## 1. 🔴 调试体验极差（用户已指出）

### 1.1 事件流转是隐式的，难以追踪

一个事件从产生到消费经历：`postEvent` → 队列 → `sendPostedEvents` → `notify` → 过滤器链 → `event()` → `xxxEvent()`。**没有任何内置的、可读的追踪手段**：

- Qt 的 `Q_TRACE` 宏默认是空实现，需要编译期开启 + 外部工具（LTTng/ETW）才能看到，普通开发者等于没有。
- 事件是**裸指针 + int 类型**，调试器里看到的是一串难以辨认的枚举数字，无法直接看到事件"从哪来、到哪去、被谁拦截"。
- `QSpontaneousEvent` 标志（`m_spont`）只是 1 个 bit，调试时无法直观看出事件的来源（系统/应用）。

**后果**：事件"消失"（被过滤器吞掉）、事件"重复"、事件顺序错乱这类问题，只能靠打断点 + 阅读源码手工推演。

### 1.2 缺少调用栈级别的因果信息

- 事件对象上**没有来源（sender）信息**，只有 `QMetaCallEvent` 内部携带 sender。
- 过滤器链上谁 `return true` 吞掉了事件，没有任何记录。
- 没有"事件血缘/传播路径"概念（事件在父子控件间传播时，看不到传播链）。

### 1.3 没有可观测性钩子

- `QCoreApplication::notify()` 是唯一可重写的观察点，但**一次只能有一个子类生效**，且重写后要自己处理过滤器、线程检查、widget 特判等全部细节，极易出错。
- 应用级过滤器能看到事件，但它是**参与分发**的（能改变结果），不是纯粹的观测者。
- 没有 per-event、per-type、per-object 粒度的过滤/采样机制。

---

## 2. 🔴 类型系统是"伪类型化"

### 2.1 事件用 int 枚举而非类型标识

```cpp
enum Type { None=0, Timer=1, MouseButtonPress=2, /* ... 100+ 项 */ User=1000 };
```

- 事件子类与 `Type` 之间**没有任何编译期关联**。`QTimerEvent` 的 `type()` 返回 1，是硬编码在构造函数里的。
- 分发靠 `switch(e->type())` + `static_cast`。**cast 错误在编译期无法发现**——把 `QKeyEvent` 当 `QMouseEvent` 用，编译器不报错，运行时才崩溃。
- 新增事件类型要"手工编号 + 维护枚举 + 保证不冲突"，没有类型安全的自定义事件机制。
- 派生类可能忘记设置正确的 `Type`，或设置成别人的 `Type`，形成隐蔽 bug。

### 2.2 自定义事件的样板代码

```cpp
class MyEvent : public QEvent {
public:
    static const Type MyType = static_cast<Type>(QEvent::registerEventType());
    explicit MyEvent() : QEvent(MyType) {}
};
```

需要手工 `registerEventType()`（内部还是全局原子计数器 + 范围检查），不能利用 C++ 类型系统。

---

## 3. 🟠 所有权与生命周期是手工管理的

### 3.1 堆/栈/队列三种所有权并存

| 投递方式 | 所有权 | 风险 |
|---------|--------|------|
| `sendEvent(obj, &e)` | 栈上，调用者持有 | 若被过滤器吞掉并 delete（少见但存在），调用者悬垂 |
| `postEvent(obj, new E)` | 队列持有，自动 delete | 一旦入队，调用者**绝不能**再触碰指针；忘 new 会崩，误复用会崩 |
| `DeferredDelete` | 队列持有，延迟 delete | 与 `deleteLater` 耦合，生命周期规则隐晦 |

- `postEvent` 的"堆上分配 + 队列接管"是**错误之源**：`new` 了没 post 会泄漏；post 了又 delete 会 double-free。
- 事件 `accept()` 状态与所有权无关，但文档和心智模型常常混淆。

### 3.2 事件在队列中可被压缩（compression）

```cpp
// QCoreApplication::compressEvent
// Timer 事件同 timerId 去重、DeferredDelete 去重、Quit 去重
```

- 压缩是**隐式的、类型特判的**（只对 Timer/DeferredDelete/Quit 生效），用户无法控制。
- 调试时"事件被压掉了"完全不可见，进一步加剧 1.1 的追踪难题。

---

## 4. 🟠 分发架构的耦合与脆弱点

### 4.1 notify() 是"上帝函数"

`QCoreApplication::notify` 同时承担：线程检查、关闭状态判断、应用级过滤器、对象级过滤器、widget 特判、最终投递。**任何一处扩展都要动这个函数**：

- 想加一个"全局日志过滤器"？只能继承 QApplication 重写 notify —— 但**一个进程只有一个 QApplication 实例**，重写意味着与框架其他扩展互斥。
- 逻辑高度集中，分支多，`sendPostedEvents` 里那个"tricky loop"（源码注释原话）充满了对 `startOffset`/`insertionOffset`/`canWait` 的手工维护，极易引入活锁/死锁。

### 4.2 线程规则靠"约定 + 断言"，不是类型

- `sendEvent` 跨线程是 **UB（未定义行为）**，只在 debug 构建有 `checkReceiverThread` 断言。release 构建下是内存错误。
- 对象 `moveToThread` 后，`threadData` 是原子指针，事件队列加锁逻辑绕来绕去（`lockThreadPostEventList` 的 for(;;) 循环），脆弱且难懂。

### 4.3 事件循环与平台耦合

- `QAbstractEventDispatcher` 是纯虚接口，但 Windows/Linux/macOS 各自实现，**行为差异大**（定时器精度、原生事件过滤、wakeUp 机制都不同）。
- 跨平台抽象层（`qeventdispatcher_glib` / `_win` / `_unix` / `_wasm` / `_cf`）每个都是几百上千行平台代码，新平台接入成本高。

---

## 5. 🟡 性能与扩展性的隐性问题

| 问题 | 说明 |
|------|------|
| 虚函数调用链长 | `notify → event() → xxxEvent()` 每级都是虚调用，热路径（鼠标移动、绘制）开销被放大 |
| 堆分配频繁 | 每个 posted 事件都是独立 `new`；`QPostEvent` 结构 + 事件对象两次分配 |
| `QList<QPointer<QObject>>` 过滤器 | 用 `QPointer`（弱引用）存过滤器，遍历有原子操作开销 |
| 事件压缩不可扩展 | 压缩逻辑硬编码在 `compressEvent` 的 if 链里，第三方事件无法声明"可压缩" |
| 类型转换依赖 RTTI/static_cast | `static_cast<QMouseEvent*>(e)` 无校验；`qobject_cast` 需要 Q_OBJECT 宏 |
| 元对象系统耦合 | 跨线程调用（MetaCall）依赖 moc 生成的元数据，脱离 moc 无法工作 |

---

## 6. 🟡 与现代 C++ 的格格不入

Qt 事件系统诞生于 C++98 时代，留下了鲜明的时代烙印：

| 现代 C++ 特性 | Qt 事件系统的现状 |
|--------------|------------------|
| `std::variant` / 强类型枚举 | 用 int 枚举 + static_cast 裸指针 |
| 智能指针（`unique_ptr`/`shared_ptr`） | 手工 new/delete + QPointer 自研弱引用 |
| `std::chrono` | 定时器用 `int interval`（毫秒），接口无时间单位 |
| `std::function` / 泛型回调 | 信号槽依赖 moc 预处理 + 字符串签名匹配 |
| 协程 `co_await` | 无原生支持，异步要靠信号槽嵌套 |
| Concepts / 编译期多态 | 全部运行时虚函数 + RTTI |
| 结构化绑定 / 模式匹配 | 手写 switch + cast |

---

## 7. 痛点总结表

| # | 痛点 | 严重度 | 一句话描述 |
|---|------|--------|-----------|
| 1 | 调试追踪困难 | 🔴 | 事件流转隐式、无因果信息、无可观测性钩子 |
| 2 | 伪类型系统 | 🔴 | int 枚举 + static_cast，编译期无保护 |
| 3 | 所有权手工管理 | 🟠 | 堆/栈/队列三种所有权并存，易泄漏/双删 |
| 4 | notify 上帝函数 | 🟠 | 所有扩展互斥，逻辑集中难维护 |
| 5 | 线程规则靠约定 | 🟠 | 跨线程 sendEvent 是 UB，release 无保护 |
| 6 | 平台耦合 | 🟠 | 每个平台一套事件分发器，行为不一致 |
| 7 | 压缩/过滤不可扩展 | 🟡 | 硬编码 if 链，第三方无法参与 |
| 8 | 热路径开销 | 🟡 | 虚调用链 + 每事件堆分配 |
| 9 | 与 C++98 遗产绑定 | 🟡 | 无 chrono、无智能指针、依赖 moc |

---

## 8. 痛点 → 重写目标的映射（引出第三份文档）

| 痛点 | 重写方案对应目标 |
|------|-----------------|
| 调试追踪困难 | 内置可观测性：事件轨迹（trace）、来源标记、可插拔 observer |
| 伪类型系统 | 强类型事件：类型即 `std::variant`/继承 + `constexpr` type 标签，编译期校验 |
| 所有权手工管理 | 智能指针统一生命周期：`unique_ptr` 投递、队列持有、无 raw new/delete |
| notify 上帝函数 | 责任分离：dispatcher / filter chain / observer 各自独立组件 |
| 线程规则靠约定 | 类型层面约束：投递 API 区分 same-thread / cross-thread，编译期或断言保护 |
| 平台耦合 | 干净的平台抽象：`EventSource` 接口 + 可组合的 event loop |
| 压缩/过滤不可扩展 | 可扩展管线：过滤器/压缩器注册机制 |
| 热路径开销 | 值语义事件 + 轻量分发，避免每事件堆分配 |
| C++98 遗产 | 全面使用 C++17/20：chrono、variant、smart pointers、coroutines |

> 下一步见《03-rewrite-proposal.md》——基于这些痛点设计的新事件系统方案。
