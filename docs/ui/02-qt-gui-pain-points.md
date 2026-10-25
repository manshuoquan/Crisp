# Qt GUI/Widgets 痛点分级分析（crisp::ui 前置研究）

> 基于本地 Qt 6.2.4 源码（`qtbase-everywhere-src-6.2.4/`）研读整理，仿事件系统痛点文档（`docs/event-system/02-qt-event-system-pain-points.md`）的 🔴/🟠/🟡 三级分级。
> 分级标准：
> - 🔴 **严重**：影响类型安全/可维护性/性能本质，重构核心目标
> - 🟠 **中等**：常见坑/性能局部问题/语义耦合，设计中需显式规避
> - 🟡 **轻微**：心智负担/习惯成本，记录但不优先解决

---

## 1. 🔴 严重（重构核心目标）

### P1 巨型 event() switch（qwidget.cpp:8641）

```cpp
bool QWidget::event(QEvent *event) {
    if (!isEnabled() && (...)) return false;
    switch (event->type()) {          // int 枚举
    case QEvent::MouseMove:  mouseMoveEvent(...);  break;
    case QEvent::KeyPress:   keyPressEvent(...);   break;
    // 每种事件一个 case + static_cast 还原
    }
}
```

- 新增事件类型 → 改 switch；漏 case → **静默无处理**。
- `static_cast` 还原具体事件，类型错误运行期才暴露。

**CRISP 对策**：typed 事件 + `CtDispatcher<EventSet>` 穷尽分发（编译期校验）。

### P2 样式系统 int 枚举 + 运行期 CSS 解析

- `QStyle` 五枚举（PE/CE/SE/PM/CT）+ `QStyleOption` 结构，全靠 int 标识。
- `QStyleSheetStyle` 运行期解析类 CSS 字符串 → 规则树 → 基元映射；**错误静默**，性能开销在热路径。

**CRISP 对策**：`CtStyle` 首版仅文本/边框/背景三原色集（UI5），不做运行期解析。

### P3 样式与控件逻辑耦合

- 控件 `drawControl(CE_PushButton, ...)` 委托样式绘制，但控件自身仍嵌大量状态逻辑（hover/pressed/disabled）。
- 样式表优先级、继承、冲突处理复杂，难调试。

**CRISP 对策**：绘制委托独立（`CtStyle` 接口），控件状态机与绘制解耦。

### P4 绘制全量重绘

- 控件树 `render()` 默认全树重绘进 backingstore，无内置脏区语义（依赖 `QWidgetRepaintManager` 的 markDirty 聚合，但控件作者常直接 `update()` 全控件）。
- 大窗口高频更新（鼠标移动）性能代价高。

**CRISP 对策**：`markDamage(rect)` 脏区聚合 + 合批 + 只绘脏区（UI1/UI2）。

### P5 控件所有权手工管理

- `QObject` 树 + `deleteLater()`：子对象随父销毁，但父级漏设即泄漏/悬垂。
- `new QWidget(parent)` 与 `setParent` 两套入口并存，所有权约定靠纪律。

**CRISP 对策**：`shared_ptr` 对象树 + `std::weak_ptr` 子引用 + RAII 注销（六原则"所有权明确"）。

### P6 无内置可观测性

- `Q_TRACE` 宏在 Qt 6 是空实现（默认关闭）。
- 控件生命周期、绘制、焦点流转都是黑盒，调试靠打点。

**CRISP 对策**：`CtTrace` 记录控件创建/销毁/绘制/焦点/事件流转（事件系统已内置）。

---

## 2. 🟠 中等（设计中显式规避）

### P7 布局性能与嵌套

- `QLayout` 基于 `QLayoutStruct` 数组 + `invalidate()` 沿树递归全量失效。
- 深嵌套布局 → 每次 `setGeometry` 全树重算。

**CRISP 对策**：`CtBoxLayout` 显式 `relayout()`（无全树递归），两遍算法。

### P8 双击/hover 语义平台耦合

- 双击检测在 `processMouseEvent`（时间窗+距离窗+同按钮），平台相关。
- hover 事件依赖平台 WM_MOUSEHOVER 支持。

**CRISP 对策**：双击/hover 作为控件状态机显式实现（纯逻辑可测），开放问题 #2 引入 timestamp 支撑。

### P9 focus 全局可变状态

- `QApplicationPrivate::focus_widget` 全局可变，跨窗口焦点管理绕。
- Tab/Backtab 靠 `focusNextPrevChild_helper` 树遍历（qapplication.cpp:1936）。

**CRISP 对策**：焦点链作为控件树显式数据（开放问题 #3 裁决存储方式）。

### P10 文本渲染复杂度

- `QLabel` 走 `QTextDocument`（富文本引擎），简单标签也背全量富文本。
- 文本宽度计算、换行、字体度量开销大。

**CRISP 对策**：`CtLabel` 首版纯文本 + `DrawTextW`（GDI 后端），富文本不做。

---

## 3. 🟡 轻微（记录但不优先）

### P11 坐标映射族

- `mapFromGlobal/mapToGlobal/mapFromParent/...` 一大家族，心智负担。
- CRISP：根控件 hit-test 统一换算，提供最小集（UI4 再定）。

### P12 QWidgetData 位域维护

- `WA_*` 属性宏 + 位域状态，写错位即行为异常。
- CRISP：显式枚举/布尔成员。

### P13 隐式共享（COW）

- `QString/QPixmap/QImage` 隐式共享，多线程下 detach 陷阱。
- CRISP：值语义 + 显式 move（事件系统已有先例）。

---

## 4. 痛点 → 设计原则映射

| 痛点 | 设计原则 |
|------|---------|
| P1 巨型 switch | 类型安全第一（typed 事件穷尽分发） |
| P2/P3 样式耦合 | 类型安全第一 + 无代码生成（CtStyle 纯接口） |
| P4 全量重绘 | 热路径零堆分配（脏区合批） |
| P5 所有权 | 所有权明确（shared_ptr 树 + RAII） |
| P6 黑盒 | 可观测性内置（CtTrace） |
| P8/P9 输入/焦点语义 | 线程规则显式 + 可观测性（状态机显式化） |
| P7/P10 布局/文本性能 | 热路径零堆分配（两遍算法 + 纯文本） |

**结论**：Qt GUI 层痛点与事件层同构（int 枚举、巨型 switch、无观测）。CRISP 六原则 + 事件系统既有机制（typed 事件/分发器/trace/事件源）可直接覆盖 P1-P6 全部严重项。
