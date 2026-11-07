# CRISP 现状总览（2026-11-01）

> 本文档是 CRISP 项目的**现状快照**：当前改造的模块、各模块技术栈、对比 Qt 的提升。随里程碑推进持续更新。
> 项目目标：用现代 C++（C++17）逐步替代 Qt 模块，摆脱 moc/qmake，保留 Qt 成熟设计思想。

---

## 1. 改造总览

| 子系统 | 命名空间 | 替代的 Qt 模块 | 里程碑 | 状态 |
|--------|---------|----------------|--------|------|
| 事件系统 | `crisp::event` | `QCoreApplication` 事件分发 / `QEvent` / `QTimer` / `QAbstractEventDispatcher` | M1-M4 | ✅ 全部落地 |
| 多线程 | `crisp::thread` | `QThread` / `QThreadPool` / `QFuture` / `QtConcurrent` | MT1-MT3 | ✅ 全部落地（MT4 可选未开始） |
| UI 控件 | `crisp::ui` | `QWidget` / `QPainter` / `QStyle` / `QLayout` / QPA 平台窗口 | UI1-UI4 | ✅ UI1/UI2/UI3/UI4 落地；UI5 未开始 |
| Qt 桥 | `crisp::event`（bridge 层） | 无（兼容层） | M3/M4 | ✅ 双跑验证通过 |

**代码量**：`src/` 4 个库（event / thread / ui / bridge）+ `src/ui/platform/` 平台层 10 文件；`tests/` 21 个测试 TU；ctest **9/9** 全绿（UI4 新增 24 例）。

---

## 2. 事件系统（crisp::event）— M1-M4 ✅

### 2.1 技术栈

| 技术 | 用途 |
|------|------|
| C++17 类模板 | `CtEvent<T>` 强类型事件 |
| `std::variant` + `std::visit` | `CtEventSet<Ts...>` 闭合事件域、穷尽分发 |
| `CtEventTraits<T>` 特化（`CRISP_EVENT_TYPE` 宏） | 编译期事件类型注册（纯宏展开，无 moc） |
| `std::shared_ptr` / `std::weak_ptr` | `CtReceiver` 所有权 + 分发表弱引用 |
| `std::condition_variable` + `thread_local` | `CtQueue` 等待 / `CtEventLoop` 每线程循环 |
| 无锁 SPSC 环形缓冲（`std::atomic`） | `CtTrace` 可观测性（6 相位：kQueued/kDelivered/kDropped/...） |
| `std::chrono` | 事件时间戳、定时器 |

### 2.2 模块构成

| 模块 | 职责 |
|------|------|
| `CtEvent<T>` / `CtEventSet<Ts...>` | 强类型事件 + 闭合事件域（编译期校验） |
| `CtDispatcher<EventSet>` | 分发器：`std::visit` 穷尽匹配，杜绝 int 枚举 + static_cast |
| `CtReceiver` / `CtHandler<T>` / `CtFilter<T>` | 接收者 / 处理器 / 过滤器（`std::function` 类型擦除） |
| `CtHandlerToken` | RAII 处理器注销 |
| `CtQueue` / `CtEventLoop` | 异步队列 + 每线程事件循环 |
| `CtEventSource` / `CtTimerEventSource` / `CtWin32EventSource` | 平台事件源抽象（轮询模型） |
| `CtTrace` | 内置可观测性（无锁轨迹环） |
| `CtThreadAffinity` | 线程归属显式化（创建线程记录 + 断言） |

### 2.3 对比 Qt 的提升

| Qt 痛点 | CRISP 对策 | 提升 |
|---------|-----------|------|
| `QEvent::Type` int 枚举 + `static_cast` 还原，类型错误运行期才暴露 | `CtEvent<T>` + `CtEventSet` 编译期校验 + `std::visit` 穷尽 | **类型安全**：错误编译期暴露，杜绝"再 cast" |
| 事件流转黑盒、调试靠打点 | `CtTrace` 无锁环记录 6 相位轨迹 | **可观测性内置**：trace 是一等公民 |
| `deleteLater()` 所有权靠约定 | `shared_ptr` 对象 + `weak_ptr` 分发表 + RAII token | **所有权明确**：无手工 delete |
| 跨线程投递靠运行时碰运气 | `CtThreadAffinity` + `post/postTo` 显式路由 | **线程规则显式**：跨线程是"类型不同"的操作 |
| 高频路径（鼠标移动）堆分配 | `std::variant` 值语义 + 强类型 push 零分配 | **热路径零堆分配**（测试断言验证） |
| moc/qmake 代码生成 | 纯 C++17 + CMake | **无代码生成**：可独立编译测试 |

---

## 3. 多线程子系统（crisp::thread）— MT1-MT3 ✅

### 3.1 技术栈

| 技术 | 用途 |
|------|------|
| `std::thread` / RAII | `CtThread` 生命周期管理 |
| 侵入式优先队列 | `CtTaskQueue`（优先级 + FIFO，零分配入队） |
| `std::future` 语义自研（`CtFuture<T>`） | 可取消异步结果 |
| `std::chrono` | 超时 / 取消窗口 |
| 模板 + 结构化绑定 | `CtParallel::map/filter/reduce` 确定性保序分块 |
| `postTo` 路由表（复用事件系统） | 线程 → 事件回传桥（`CtTaskEventBridge`） |

### 3.2 模块构成

| 模块 | 职责 | 替代 |
|------|------|------|
| `CtThread` | RAII 线程封装（生命周期/退出） | `QThread` |
| `CtThreadPool` | 线程池（优先级队列、超时回收） | `QThreadPool` |
| `CtTask<T>` / `CtFuture<T>` | 强类型任务 + 可取消 Future | `QRunnable` / `QFuture` |
| `CtParallel::map/filter/reduce` | 并行算法（确定性保序） | `QtConcurrent` |
| `CtTaskEventBridge` | 任务完成 → 回投目标线程事件循环 | 无对应（Qt 需手动 queue） |

### 3.3 对比 Qt 的提升

| Qt 痛点 | CRISP 对策 | 提升 |
|---------|-----------|------|
| `QRunnable` 无返回值、结果靠成员字段 + 互斥 | `CtTask<T>` 强类型返回值 + `CtFuture<T>` | **类型安全**：无类型擦除 |
| `QThreadPool` 生命周期手工管理 | RAII + 超时回收 + 显式 shutdown | **所有权明确** |
| 线程完成回调跨线程投递繁琐 | `CtTaskEventBridge`：完成 → `postTo` 目标线程事件循环 | **线程规则显式**：回传桥一等公民 |
| `QtConcurrent` 结果顺序不确定 | 自研分块 + 确定性保序 | **确定性**：结果序可预期 |

---

## 4. UI 控件子系统（crisp::ui）— UI1/UI2/UI3 ✅

### 4.1 技术栈

| 技术 | 用途 |
|------|------|
| C++17 值类型 | `CtRect`/`CtRegion`/`CtColor`/`CtSize`/`CtFont`（POD 值语义） |
| `std::u16string` / `std::u16string_view` | 文本（零 Qt 依赖，UTF-16） |
| `std::variant` | `CtInputMapperResult`（映射结果编译期穷尽判别） |
| `std::chrono::steady_clock` | 输入事件时间戳（双击窗口） |
| `std::shared_ptr` | `CtWidgetBase` 对象树所有权 |
| GDI（Win32）/ PixelBuffer | 双绘制后端（`CtPaintBackend` 抽象） |
| 平台事件源模板 | `CtWin32EventSource<CtUiEventSet>` / `CtQtHostEventSource<CtUiEventSet>` |

### 4.2 模块构成（按里程碑）

| 里程碑 | 模块 | 内容 |
|--------|------|------|
| UI1 | `CtWidgetBase` / `CtWidgetTrace` / `CtWidgetState` | 对象树 + 脏区 + 轨迹 + 输入载荷下放核心层 |
| UI2 | `CtPainter` / `CtPaintBackend` / `CtPaintBackendPixelBuffer` / `CtBackingStore` | 记录式绘制 + 像素缓冲后端 + 脏区合批 |
| UI3 | `ct_input_mapper` / `CtWin32EventSource` / `CtQtHostEventSource` / `CtPlatformWindow` / `CtPaintBackendGdi` | 平台输入全链路（双模式：Win32 原生 + Qt 宿主）+ GDI 后端 |
| UI4 | `CtLabel` / `CtPushButton` / `CtLineEdit` / `CtBoxLayout` / `CtFocusChain` | ✅ 已落地（07 架构规范） |
| UI5 | `CtStyle` / 声明式 UI | ⬜ 可选未开始 |

### 4.3 对比 Qt 的提升

| Qt 痛点 | CRISP 对策 | 提升 |
|---------|-----------|------|
| P1 巨型 `event()` switch + int 枚举 + static_cast | typed 事件 + `CtDispatcher` 穷尽分发（复用事件系统） | **类型安全**：编译期校验 |
| P4 绘制全量重绘 | `markDamage(rect)` 脏区聚合 + 合批 + 只绘脏区 | **性能**：高频更新只绘脏区 |
| P5 控件所有权手工管理 | `shared_ptr` 对象树 + RAII | **所有权明确** |
| P6 无内置可观测性 | `CtTrace` 记录控件创建/销毁/绘制/焦点 | **可观测性内置** |
| 输入映射分散在各控件 | `ct_input_mapper` 唯一受控点（双模式共用同一映射边界） | **单点映射**：控件层零平台依赖，双跑可对比 |
| Qt 消息泵与事件循环割裂 | Win32 消息泵 = `CtEventSource`（轮询模型，无第二套消息泵） | **统一模型**：平台源注入 CtQueue |
| 绘制命令即发即画 | 记录式命令批（`CtPaintCmd` 值语义零分配）+ 后端回放 | **热路径零分配** + 后端可替换（Pixel/GDI） |

---

## 5. Qt 桥（M3/M4）— ✅

### 5.1 技术栈

| 技术 | 用途 |
|------|------|
| Qt 6.2.4（Core/Gui/Widgets） | 仅桥层依赖（核心库零 Qt） |
| QObject 事件过滤器 | 捕获 Qt 事件（克隆入缓冲） |
| `std::variant`（`CtQtTypedPayload`） | 桥翻译产物（三成员闭合） |
| 双跑验证（dual_run） | 同一业务逻辑跑 Qt 原生 vs 桥接后端，字节比对 |

### 5.2 职责

- `CtQtEventPayload`：原始 QEvent 的 RAII 封装（所有权转移）
- `CtQtTranslatorTable`：QEvent::Type → CRISP 载荷的映射表（int 枚举唯一受控点）
- `CtQtEventSource<EventSet>`：Qt 事件泵（processEvents + 过滤器）
- `CtQtHostEventSource<CtUiEventSet>`：UI3 Qt 宿主模式（复用 M4 翻译器 → mapBridge* → 核心载荷）
- `postToQt`：CRISP → Qt 双向投递

### 5.3 对比 Qt 的提升

- **int 枚举受控**：`QEvent::Type` 只在桥层出现，CRISP 侧永远是强类型载荷。
- **双跑验证**：同一业务逻辑在 Qt 原生与桥接后端上字节一致运行，桥正确性可证明。

---

## 6. 平台层（src/ui/platform/）— UI3 ✅

> 共 10 个文件（6 个 .cpp TU + 对应 4 个 .h 声明头）。

| 文件 | 职责 |
|------|------|
| `ct_input_mapper_win32.cpp` | Win32 MSG → CtRawMsg → mapWin32*（含双击状态机） |
| `ct_input_mapper_bridge.cpp` | M4 桥载荷 → crisp::input（mapBridge*，Qt 宿主） |
| `ct_win32_event_source.cpp` | Win32 消息泵 = CtEventSource（PeekMessageW → mapper → 强类型 push） |
| `ct_qt_host_event_source.cpp` | Qt 宿主源（M4 翻译器 → mapBridge* → 强类型 push） |
| `ct_platform_window_win32.cpp` | Win32 原生窗口（RAII HWND，析构序 deactivate+detach） |
| `ct_paint_backend_gdi.cpp` | GDI 绘制后端（FillRect/DrawTextW/BitBlt + 线程守门） |

**关键设计**：
- 双模式**一个接口**（`CtPlatformWindow`），控件层零平台依赖。
- `ct_input_mapper` 是**唯一受控点**：Win32/Qt 双模式共用同一映射边界，控件层看到的载荷完全一致。
- 平台层按 `if(WIN32)` / `if(Qt6_FOUND)` 条件编译，核心库零 Qt/Win32 依赖。

---

## 7. 测试现状

| 套件 | 用例数 | 内容 |
|------|--------|------|
| `event_tests` | 39 | M1 同步 + M2 异步核心 |
| `thread_tests` | 42 | MT1-MT3 |
| `ui_tests` | 96 | UI1/UI2 + UI3 核心 mapper + UI4 控件/布局/焦点链（状态机全路径、stretch 余数补偿黄金用例、焦点轨迹配对） |
| `ui3_platform_tests` | 11 | Win32 消息映射 + 双击回归 + 平台窗口 + GDI 全链路 |
| `ui3_qt_mapper_tests` | 7 | Qt 桥映射 + 跨模式一致性 + 零分配 |
| `qt_bridge_tests` | 30 | M3/M4 桥 + 翻译器 + 事件源 |
| `dual_run_qt` / `dual_run_bridge` / `dual_compare` | 3 | 双后端字节比对 |

**ctest 总计：9/9 通过（100%）**（注册来源：`tests/{event,thread,ui,bridge}/CMakeLists.txt`，共 9 个 add_test）。

---

## 8. 待办路线

| 里程碑 | 内容 | 状态 |
|--------|------|------|
| UI5 | 扩展控件 + 样式 `CtStyle` + 声明式 UI 工具 | ⬜ 可选 |
| MT4 | work-stealing / `CtPromise<T>` | ⬜ 可选 |
| 事件系统加固 | 对象池、flat-map、`CtOpaqueEvent` | ⬜ |
| 控件树级完整双跑 | 同一控件树双模式收到相同载荷序列（UI4 控件已完备） | ⬜ |
| 跨平台 | Qt 宿主模式完整双跑（非 Win32 环境验证） | ⬜ |

---

## 9. 关联文档

- 事件系统：`docs/event-system/`（README 索引）
- 多线程：`docs/threading/`（README 索引）
- UI 控件：`docs/ui/`（README 索引）
- 编码规范：`AGENTS.md`（强制）
