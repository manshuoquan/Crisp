# CRISP UI 控件子系统（crisp::ui）设计文档

> 目标：用现代 C++（C++17）逐步替代 Qt 的 GUI/Widgets 模块（`QWidget`/`QPainter`/`QStyle`/`QLayout`），作为事件系统（M1-M4）与多线程子系统（MT1-MT3）之后的第三个子系统，对应原路线图（`docs/event-system/03-rewrite-proposal.md` §5）的"阶段 3：替换 UI 层"。

## 文档系列

| 编号 | 文档 | 内容 |
|------|------|------|
| 01 | [01-qt-gui-architecture.md](./01-qt-gui-architecture.md) | Qt 6 GUI/Widgets 架构调研（QPA、QWidget 基座、QPainter、QStyle、布局、平台输入全链路） |
| 02 | [02-qt-gui-pain-points.md](./02-qt-gui-pain-points.md) | Qt GUI/Widgets 痛点分级分析（🔴/🟠/🟡 三级，9 类 30 项） |
| 03 | [03-rewrite-proposal.md](./03-rewrite-proposal.md) | crisp::ui 重写方案（六原则映射、分层架构、接口草案、里程碑路线） | ✅ 本文档（新建） |

## 里程碑

| 里程碑 | 内容 | 状态 |
|--------|------|------|
| UI0 | 调研文档（01/02）+ 重写方案（03） | ✅ 本文档 |
| UI1 | 核心基座：`CtWidgetBase` 对象树 + 输入载荷下放核心层（CRISP 原生枚举） | ⬜ 未开始 |
| UI2 | 绘制基元：`CtPainter` + `CtPaintBackend`（GDI + PixelBuffer）+ 脏区合成 | ⬜ 未开始 |
| UI3 | 平台窗口双模式：Win32 原生 + Qt 宿主（复用 M3/M4 桥） | ⬜ 未开始 |
| UI4 | 基础控件集 + 布局 + 焦点链：`CtLabel`/`CtPushButton`/`CtLineEdit` + `CtBoxLayout` | ⬜ 未开始 |
| UI5 | 扩展控件 + 样式 + 声明式 UI 工具（可选） | ⬜ 可选未开始 |

## 关键咬合点

- **复用（事件系统）**：`CtEvent<T>` 类型化事件、`CtDispatcher` 分发、`CtReceiver` 控件事件载体、`CtEventLoop`/`CtQueue`/`CtEventSource`（平台消息泵与定时器）、`CtTrace`（控件生命周期/绘制可观测）、`CtThreadAffinity`（UI 线程规则）。
- **复用（多线程）**：`CtFuture` + 线程→事件回传桥（控件重活丢后台，完成回投 UI 线程）。
- **前置改造（UI pre-M1，commit c3f0b2f）✅ 已落地**：核心层 `crisp::input` 原生载荷已落地（`src/event/ct_input.h`/`ct_mouse_payload.h`/`ct_key_payload.h`，原生枚举 `CtMouseButton`/`CtKeyButton`/`CtModifiers`/`CtMouseAction`/`CtKeyAction`，`text` 为 `std::u16string`），对应事件系统 06 §8 开放问题 #1/#2 已解决。桥层保留 Qt 字段版本（`src/bridge/qt_mouse_payload.h`/`qt_key_payload.h`）作 M3/M4 兼容。UI1 起控件直接消费核心层载荷，不再涉及载荷类型改造（迁移路径见 [03 §4.1.3](./03-rewrite-proposal.md)）。
- **平台层例外条款**：核心层 `crisp::ui` 纯 C++17 零 Qt/Win32 依赖；平台层 `CtPlatformWindow` 可依赖 Win32/GDI 或 Qt（仿 M3 桥契约写法，见 [03 §2.3](./03-rewrite-proposal.md)）。

## 关联文档

- 事件系统（前置）：[docs/event-system/](../event-system/README.md)
- 多线程子系统（并行前置）：[docs/threading/](../threading/README.md)
- Qt 6 UI/事件调研总纲：[docs/qt6-xml-ui-and-event-system.md](../qt6-xml-ui-and-event-system.md)
- 项目编码规范：`AGENTS.md`（强制）
