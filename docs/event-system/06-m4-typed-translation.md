# M4 类型化翻译扩展（Typed Translation Extension）

> 状态：**IMPLEMENTED**（2026-10-11；设计 Rev 2 经架构审查 PASS 后编码落地）
> 上游：`docs/event-system/05-qt-bridge.md` §9 开放问题 #1（已裁定 M4 再做；Rev 5 改"已落地"）
> 原则来源：`03-rewrite-proposal.md` §1.2 设计原则 #1（类型安全第一）、#5（热路径零堆分配）

## 1. 目标与范围

### 1.1 目标

M3 桥以原始载荷 `CtQtEventPayload` 直通（简化形态）。M4 将鼠标/键盘事件翻译为 **CRISP 专用强类型载荷**，使 CRISP handler 面对类型安全、字段明确的 `CtEvent<CtMousePayload>` / `CtEvent<CtKeyPayload>`，而非 int 枚举 + 原始 `QEvent`。

### 1.2 范围

- **做**：
  1. 新增 `CtMousePayload` / `CtKeyPayload`（值语义，字段从 `QMouseEvent`/`QKeyEvent` 抽取），内置 **CRISP 原生动作枚举** `CtMouseAction`/`CtKeyAction`（P1-1：press/move/release/dblclick 判别不丢失）。
  2. 扩展默认桥集 `CtQtEventSet`：`CtEventSet<CtTimerPayload, CtQtEventPayload, CtMousePayload, CtKeyPayload>`。
  3. 翻译器返回类型改为 `CtQtTypedPayload = std::variant<CtQtEventPayload, CtMousePayload, CtKeyPayload>`。
  4. `CtQtEventSource::translateAndPush` 用 `std::visit` 构造并 push 强类型 `CtEvent<T>`。
  5. 安装默认翻译器：`MouseMove/MousePress/MouseRelease/MouseButtonDblClick` → `CtMousePayload`；`KeyPress/KeyRelease` → `CtKeyPayload`。
  6. 测试：类型化翻译正确性（字段 + 动作抽取）、M3 兼容（identity 兜底仍工作）、**既有 7 个用例 + 双跑后端同步修改**（§1.3）。
- **不做**：
  - `CtOpaqueEvent` 逃生舱（04 §14 为动态/跨 DLL 场景保留；M4 桥集是编译期闭合的，不需要）。
  - 嵌套 `QCoreApplication::exec` / `QAbstractEventDispatcher` 完全接管（05 §2.4 M4 可选演进，只注明不设计）。
  - 平台等待（`MsgWaitForMultipleObjectsEx`，04 §14 #8）——独立项。

### 1.3 兼容性（P1-2 修法：审查 REJECT 后如实改述）

- `CtQtEventPayload` **保留**在桥集中：未注册类型经 identity 翻译兜底（M3 语义不变）；**但"23 个桥测试不破坏"不成立**——需按下列清单同步修改既有测试：
  - **编译失败**（`tests/bridge/test_qt_translator.cpp` 3 个用例）：`qt_translator_identity_mapping_preserves_event`、`qt_translator_identity_fallback_bridges_user_plus`、`qt_translator_spontaneous_maps_to_system_origin` 直接调用翻译器并访问返回值 `.hasEvent()/.qtType()`——返回类型变 variant 后编译失败。**修法**：用例改为访问 variant 的 `CtQtEventPayload` 分支（`std::get<CtQtEventPayload>`）。
  - **断言失败**（`tests/bridge/test_qt_event_source.cpp` 4 个用例）：`qt_source_poll_translates_mouse_move_into_queue`、`qt_source_qobject_delete_later_is_safe`、`qt_source_reentry_guard_skips_nested_poll_in_release`、`qt_source_cross_thread_post_is_captured_and_translated` 发送 `QMouseEvent(MouseMove)` 并断言队列内是 `CtEvent<CtQtEventPayload>`——M4 默认类型化翻译后变为 `CtEvent<CtMousePayload>`。**修法（Rev 2 修正）**：M4 后 `KeyPress` 也已走类型化翻译（→`CtKeyPayload`），**不能**改用 `KeyPress` 作 identity 兜底验证。改用仍走 identity 的内置类型（如 `QEvent::FocusIn`/`QEvent::Enter`，未注册 → identity 兜底 `CtQtEventPayload`），或断言改为命中 `CtMousePayload` 分支。
  - **双跑后端**（`tests/bridge/dual/main_bridge.cpp`）：`CtDualReceiver::handleQtEvent` 经 `CtQtEventPayload` 处理鼠标/键盘——需迁移为 `CtMousePayload`/`CtKeyPayload`/`CtQtEventPayload`(Timer) 三 handler（§1.3 下条）。
- 新增载荷是**追加**，非替换——应用可按需从 `CtEvent<CtQtEventPayload>` 迁移到 `CtEvent<CtMousePayload>`。
- **双跑后端迁移（重要）**：`tests/bridge/dual/main_bridge.cpp` 的 `CtDualReceiver::handleQtEvent` 目前经 `CtEvent<CtQtEventPayload>` 处理鼠标/键盘（内部 `static_cast<QMouseEvent*>`）。M4 后鼠标/键盘事件进入 `CtMousePayload`/`CtKeyPayload`，该 handler 不再收到它们。**必须迁移**：分别绑定 `CtMousePayload`/`CtKeyPayload`/`CtQtEventPayload`(Timer) 三个 handler，保持双跑对比语义一致（§6.3 双跑不受影响）。

---

## 2. 载荷设计（CtMousePayload / CtKeyPayload）

### 2.1 动作判别（P1-1 修法：审查 REJECT 后修订）

Qt 的 `QMouseEvent` 用一个 `QEvent::Type` 表达动作（Move/Press/Release/DblClick）。M4 若把四个类型坍缩进单一 `CtMousePayload` 而无动作字段，handler 面对同一强类型却无法知道"发生了什么动作"——Qt 语义中最主要的判别维度被抹掉（违反原则 #1）。**修法**：每个载荷内置一个 **CRISP 原生动作枚举**（定义于桥层、零 Qt 依赖），翻译器把 `QEvent::Type` 映射为动作值。

```cpp
/// (M4) Mouse action — CRISP-native discriminant replacing the Qt type id.
/// Zero Qt dependency (bridge-layer enum). Defined in qt_mouse_payload.h.
enum class CtMouseAction : uint8_t {
    kMove,     ///< QEvent::MouseMove
    kPress,    ///< QEvent::MousePress
    kRelease,  ///< QEvent::MouseRelease
    kDblClick, ///< QEvent::MouseButtonDblClick
};

/// (M4) Key action — CRISP-native discriminant for press/release.
enum class CtKeyAction : uint8_t {
    kPress,    ///< QEvent::KeyPress
    kRelease,  ///< QEvent::KeyRelease
};
```

### 2.2 CtMousePayload

```cpp
/// Strong-typed mouse payload extracted from QMouseEvent (M4, §2.2).
/// Value semantics; hot-path zero allocation. Stored inside
/// CtEvent<CtMousePayload> by value in the bridge set variant.
struct CtMousePayload {
    CtMouseAction action = CtMouseAction::kMove; ///< press/move/release/dblclick
    int x = 0;                 ///< local x (QMouseEvent::position().x())
    int y = 0;                 ///< local y (QMouseEvent::position().y())
    Qt::MouseButton button = Qt::NoButton;   ///< the changed button
    Qt::MouseButtons buttons = Qt::NoButton; ///< all pressed buttons
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
};
```

- **Qt 依赖说明**：`Qt::MouseButton`/`Qt::KeyboardModifiers` 是枚举类型。按 AGENTS.md"核心库零 Qt 依赖"（05 §5.2 契约 #1），`CtMousePayload` 必须定义在 **`src/bridge/`**（桥库），不能进 `src/event/`。桥集的 `CtEventSet` 只需 `CtEventTraits<T>::kIsValid`，载荷类型本身可在桥层定义。
- **字段裁剪（P3）**：不保留 `globalPosition()`/`source()`/`device()`——M4 范围限定局部坐标 + 按键状态；GUI 应用若需全局坐标可自定义载荷 + `registerTranslator`（见 §8 开放问题）。

### 2.3 CtKeyPayload

```cpp
/// Strong-typed keyboard payload extracted from QKeyEvent (M4, §2.3).
/// CtKeyPayload lives in the bridge layer (src/bridge/), so Qt types are
/// permitted in its fields (05 §5.2 契约 #1 constrains only src/event/).
struct CtKeyPayload {
    CtKeyAction action = CtKeyAction::kPress; ///< press/release
    Qt::Key key = Qt::Key_unknown;            ///< the pressed key (P2-1: NOT
                                              ///< int — principle #1 forbids
                                              ///< int-enum reinterpretation)
    QString text;                             ///< unicode text (may be empty)
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    bool isAutoRepeat = false;
};
```

- **P2-1 修法**：`key` 用 `Qt::Key`（桥层允许 Qt 依赖；`modifiers` 已是 Qt 枚举，用 int 反而自相矛盾）。handler 收到 `CtKeyPayload` 直接得 `Qt::Key`，无需 int→枚举解释。
- `text` 保留 `QString`（`QKeyEvent::text()` 返回 `QString`）；若未来要进核心层可改 `std::u16string`（M4 不扩展范围）。

### 2.4 CRISP_EVENT_TYPE

```cpp
CRISP_EVENT_TYPE(crisp::event::CtMousePayload, "Mouse");
CRISP_EVENT_TYPE(crisp::event::CtKeyPayload, "Key");
```

- 命名对齐 04 §7.2 的 `"Mouse"`/`"Key"`（tests/event/test_ct_dispatcher.cpp 已用同名测试载荷名）。

---

## 3. 翻译器返回类型：CtQtTypedPayload

### 3.1 定义

```cpp
/// (M4) Typed translation product: one of the bridge's typed payloads.
/// The M3 identity path (raw QEvent -> CtQtEventPayload) remains a member.
using CtQtTypedPayload =
    std::variant<CtQtEventPayload, CtMousePayload, CtKeyPayload>;
```

### 3.2 翻译器签名变更

```cpp
// M3 (固定返回 CtQtEventPayload):
using CtQtTranslator =
    std::function<CtQtEventPayload(std::unique_ptr<QEvent>, bool spontaneous)>;

// M4 (返回 typed variant):
using CtQtTranslator =
    std::function<CtQtTypedPayload(std::unique_ptr<QEvent>, bool spontaneous)>;
```

- **向后兼容**：返回类型从具体类型变为包含它的 variant，`CtQtEventPayload` 分支保留——注册表 API 形态不变（`registerTranslator(type, translator)`）。
- identity 翻译器返回 `CtQtEventPayload` 分支。

### 3.3 translateAndPush 变更

```cpp
const bool spontaneous = ev->spontaneous();
CtQtTypedPayload payload = (*translator)(std::move(ev), spontaneous);
const CtEventOrigin origin =
    spontaneous ? CtEventOrigin::kSystem : CtEventOrigin::kApplication;

// std::visit: construct the STRONG-TYPED CtEvent<T> per branch and push.
std::visit(
    [&](auto&& p) {
        using P = std::decay_t<decltype(p)>;
        queue.push(std::weak_ptr<CtReceiver>{},
                   CtEvent<P>(origin, std::move(p)));
    },
    payload);
```

- `std::visit` 的 lambda 对 `CtMousePayload`/`CtKeyPayload`/`CtQtEventPayload` 三分支各自构造 `CtEvent<P>`——`P` 必须 ∈ 桥集（static_assert 由 `queue.push` 保证，编译期校验）。
- **零堆分配**：variant 值语义，`CtEvent<P>` move 入队（M3 已如此，不变）。

---

## 4. 桥集扩展

```cpp
// qt_event_set.h (M4)
using CtQtEventSet =
    CtEventSet<CtTimerPayload, CtQtEventPayload, CtMousePayload, CtKeyPayload>;
```

- `CtQtEventPayload` 保留：identity 兜底（未注册的桥接类型仍以原始载荷进入，M3 兼容）。
- `CtMousePayload`/`CtKeyPayload` 追加：默认注册的鼠标/键盘类型走类型化翻译。
- **静态断言**：`CtQtEventSource<EventSet>` 的 `static_assert(EventSet::contains<CtQtEventPayload>)` 保留（M3 契约）。
- **新契约（P2-2 修法：审查 REJECT 后显式声明）**：`CtQtEventSource<EventSet>` 的 `translateAndPush` 对 `CtQtTypedPayload` 三分支逐一实例化 `queue.push<P>`，其 `static_assert(EventSet::contains<P>)` 要求 **EventSet ⊇ {CtQtEventPayload, CtMousePayload, CtKeyPayload}**。M3 的"自定义 EventSet 仅需含 CtQtEventPayload"契约不再成立——任何不含 `CtMousePayload`/`CtKeyPayload` 的自定义桥集会编译失败。**取舍**：不采用 `if constexpr` 降级（回退 identity 会静默丢失类型化语义，违背原则 #1）；自定义 EventSet 必须包含全部三个载荷（扩展闭合集，04 §7.8 既定路径）。此契约变更写入本文档，05 §3.2 契约 #1 相应修订。
- **报错体验（Rev 2 审查残留意见 #3，可选）**：新契约目前由 `std::visit` 三分支逐一实例化 `queue.push` 间接强制，编译错误信息较绕。建议在 `CtQtEventSource<EventSet>` 类内加显式 `static_assert(EventSet::contains<CtMousePayload> && EventSet::contains<CtKeyPayload>, "...")`，报错更直接（M3 已有 `contains<CtQtEventPayload>` 断言，一并扩展）。

---

## 5. 默认翻译器安装

`CtQtTranslatorTable::installDefaults` 扩展（qt_translator.cpp）：

| QEvent::Type | 翻译器 | 产物 |
|---|---|---|
| `Timer` | identity | `CtQtEventPayload` |
| `User` (及 User+) | identity | `CtQtEventPayload` |
| `MouseMove` / `MousePress` / `MouseRelease` / `MouseButtonDblClick` | typed mouse | `CtMousePayload` |
| `KeyPress` / `KeyRelease` | typed key | `CtKeyPayload` |
| kIgnoredQtEvents | （无） | 捕获层跳过 |

### 5.1 类型化鼠标翻译器

```cpp
/// QEvent::Type -> CtMouseAction (the ONLY place Qt mouse type ids map to
/// CRISP-native discriminants; §2.1).
CtMouseAction mouseActionFor(QEvent::Type t) noexcept {
    switch (t) {
    case QEvent::MousePress:          return CtMouseAction::kPress;
    case QEvent::MouseRelease:        return CtMouseAction::kRelease;
    case QEvent::MouseButtonDblClick: return CtMouseAction::kDblClick;
    case QEvent::MouseMove:
    default:                          return CtMouseAction::kMove;
    }
}

CtQtTypedPayload mouseTranslate(std::unique_ptr<QEvent> ev, bool) {
    auto* me = static_cast<QMouseEvent*>(ev.get());  // type checked by registry
    if (me == nullptr)  // defensive: never crash; fall back to raw payload
        return CtQtEventPayload(std::move(ev));
    const QPointF pos = me->position();
    return CtMousePayload{mouseActionFor(me->type()),
                          static_cast<int>(pos.x()),
                          static_cast<int>(pos.y()),
                          me->button(), me->buttons(), me->modifiers()};
}
```

### 5.2 类型化键盘翻译器

```cpp
CtQtTypedPayload keyTranslate(std::unique_ptr<QEvent> ev, bool) {
    auto* ke = static_cast<QKeyEvent*>(ev.get());
    if (ke == nullptr)  // defensive: never crash; fall back to raw payload
        return CtQtEventPayload(std::move(ev));
    const CtKeyAction action =
        ke->type() == QEvent::KeyRelease ? CtKeyAction::kRelease
                                         : CtKeyAction::kPress;
    // QKeyEvent::key() returns int; CtKeyPayload::key is Qt::Key — the
    // explicit cast is the ONLY int->enum conversion (bridge boundary).
    return CtKeyPayload{action, static_cast<Qt::Key>(ke->key()),
                        ke->text(), ke->modifiers(), ke->isAutoRepeat()};
}
```

- **实现备忘（Rev 2 审查残留意见 #2）**：`QKeyEvent::key()` 返回 `int`，`CtKeyPayload::key` 是 `Qt::Key`（enum class 不允许隐式转换），必须 `static_cast<Qt::Key>(ke->key())`——该 cast 只存在于桥翻译器这一受控边界。
- `static_cast` 安全性：注册表以 `QEvent::Type` 为键，类型与翻译器一对一（单翻译器约束）——`MouseMove` 键必对应 `QMouseEvent` 实例（Qt 保证 `QEvent::type()` 与动态类型一致）。仍加防御：`if (me == nullptr) return CtQtEventPayload(std::move(ev));`（保底回退原始载荷）。**kDropped/ignored 分支与 M3 完全不变**（P3：§3.3 的 null 标记 kDropped 与 ignored 跳过在新 translateAndPush 中原样保留）。

---

## 6. 测试计划

### 6.1 新增测试（tests/bridge/test_qt_translator.cpp）

| # | 用例 | 断言 |
|---|------|------|
| T1 | MouseMove → CtMousePayload | `get_if<CtEvent<CtMousePayload>>` 命中，`action==kMove`、`x/y/button/buttons/modifiers` 正确 |
| T1a | MousePress → CtMousePayload | `action==kPress`、`button==Qt::LeftButton` |
| T1b | MouseButtonDblClick → CtMousePayload | `action==kDblClick`（P3：dblclick 注册配用例） |
| T2 | KeyPress → CtKeyPayload | `get_if<CtEvent<CtKeyPayload>>` 命中，`action==kPress`、`key==Qt::Key_A`、`text=="a"` |
| T2a | KeyRelease → CtKeyPayload | `action==kRelease`、`key==Qt::Key_B` |
| T3 | 未注册类型 → identity 兜底 | 仍 `CtQtEventPayload`（M3 兼容） |
| T4 | Timer → identity | 仍 `CtQtEventPayload`（M3 兼容） |

### 6.2 新增测试（tests/bridge/test_qt_event_source.cpp）

| # | 用例 | 断言 |
|---|------|------|
| S1 | 捕获 MouseMove → 队列内 `CtEvent<CtMousePayload>` | `findQueued` 新 helper 命中 CtMousePayload 分支，`action==kMove` |
| S2 | 捕获 KeyPress → 队列内 `CtEvent<CtKeyPayload>` | 同上，`action==kPress` |
| S3 | 捕获未知自定义事件 → 仍 `CtQtEventPayload`（identity） | M3 兼容回归 |

### 6.3 回归（P1-2 修法：如实改述）

- **既有测试修改**（M4 破坏面，§1.3 兼容性详述）：`test_qt_translator.cpp` 3 个用例改访问 variant 分支；`test_qt_event_source.cpp` 4 个用例改断言/换事件类型；`dual/main_bridge.cpp` 迁移三 handler。修改后全部桥测试（新增 + 修改）绿。
- `tests/event/` 零改动（桥不触核心）。
- 双跑验证（dual_compare）语义不变：桥后端迁移后仍输出相同模型调用序列（3 moves + 2 keys + 1 timeout），与纯 Qt 后端字节一致。

---

## 7. 文件变更清单

| 文件 | 变更 |
|---|---|
| `src/bridge/qt_mouse_payload.h` | 新增：CtMousePayload + CtMouseAction + CRISP_EVENT_TYPE |
| `src/bridge/qt_key_payload.h` | 新增：CtKeyPayload + CtKeyAction + CRISP_EVENT_TYPE |
| `src/bridge/qt_translator.h` | CtQtTranslator 返回类型改 CtQtTypedPayload；CtQtTypedPayload 定义 |
| `src/bridge/qt_translator.cpp` | installDefaults 注册鼠标/键盘翻译器；typed 翻译器 + action 映射实现 |
| `src/bridge/qt_event_set.h` | 桥集扩展 4 载荷 |
| `src/bridge/qt_event_source.h` | translateAndPush 改 std::visit |
| `src/bridge/qt_bridge.h` | 注释更新 |
| `src/bridge/CMakeLists.txt` | 链接 Qt6::Gui（QMouseEvent/QKeyEvent 在 QtGui 模块） |
| `CMakeLists.txt`（顶层） | find_package(Qt6 COMPONENTS Core Gui)（若顶部仅 Core 则补 Gui） |
| `tests/bridge/CMakeLists.txt` | 测试链接补 Qt6::Gui（若 Qt6::Widgets 已传递则不必要） |
| `tests/bridge/test_qt_translator.cpp` | **修改 3 个既有用例**（访问 variant 分支）+ 新增 T1–T4/T1a/T1b/T2a |
| `tests/bridge/test_qt_event_source.cpp` | **修改 4 个既有用例**（§1.3）+ 新增 S1–S3 + findQueued 新 helper |
| `tests/bridge/dual/main_bridge.cpp` | **迁移**到 CtMousePayload/CtKeyPayload/CtQtEventPayload 三 handler（§1.3 兼容性） |
| `docs/event-system/06-m4-typed-translation.md` | 本文档 |
| `docs/event-system/05-qt-bridge.md` | §3.2 契约 #1 修订（桥集新契约 P2-2）；§9 #1 从"已裁定 M4"改为"已落地"；偏差清单补 M4 项 |

**实现备忘（Rev 2 审查残留意见 #4）**：
- `qt_translator.h` 需补 `#include <variant>`（`CtQtTypedPayload` 定义）。
- 顶层 `find_package(Qt6 COMPONENTS Core Gui)` 的 Gui 探测需与 `Qt6::Widgets` 传递依赖一致（tests 已链 Widgets 获得 Gui；桥库显式链 Gui）。
- `CtQtEventSource` 类内显式 static_assert 扩展（§4 报错体验）。

---

## 8. 开放问题

1. **Qt 枚举进载荷字段**：`Qt::MouseButton`/`Qt::Key`/`Qt::KeyboardModifiers` 作为字段类型使载荷依赖 Qt。已裁定：载荷定义在桥层（`src/bridge/`），允许 Qt 依赖（05 §5.2 契约 #1 只约束核心库 `src/event/`）。
2. **`CtKeyPayload::text` 用 QString**：同上，桥层允许；若未来需进核心层改为 `std::u16string`（不扩范围）。
3. **应用自定义载荷直通（P2-3 修法：如实声明范围）**：`CtQtTypedPayload` 是**闭合三元 variant**（`CtQtEventPayload`/`CtMousePayload`/`CtKeyPayload`）——应用自定义 `MyPayload` **无法**经默认翻译器直通。M3 的 `registerTranslator` 机制允许应用注册自定义翻译器，但其返回类型也受 `CtQtTypedPayload` 闭合约束。**应用自定义载荷直通超出 M4 范围**（需模板化翻译产物或 `CtOpaqueEvent`，另立文档）。本 M4 只做裁定点名的鼠标/键盘类型化。
4. **双跑验证是否纳入类型化事件**：双跑对比基于同一业务序列（3 moves + 2 keys + 1 timeout），桥后端迁移后**同样纳入**类型化事件（§6.3 已改述）——对比输出与纯 Qt 后端保持字节一致。

---

## 实现记录（IMPLEMENTED，2026-10-11）

- 设计 Rev 2 经架构审查 **PASS**（残留意见 4 条全部处理：§1.3 KeyPress 不再走 identity 的修正、§5.2 `static_cast<Qt::Key>` 显式转换、§4 显式 static_assert 报错体验、`<variant>`/Gui 探测实现备忘）。
- 落地文件：`src/bridge/qt_mouse_payload.h`/`qt_key_payload.h`（新载荷 + 动作枚举）、`qt_translator.h/.cpp`（`CtQtTypedPayload` + typed 翻译器）、`qt_event_set.h`（四载荷桥集）、`qt_event_source.h`（`std::visit` push + 显式 static_assert）、`qt_bridge.h`、CMake（桥库/顶层/tests 补 `Qt6::Gui`）。
- 测试：桥测试 30 个全绿（新增 7 个 M4 用例；修改 3+4 个既有用例 + dual 后端迁移三 handler）；完整 ctest 5 套件通过；双跑对比字节一致。
- 实现要点：Qt 6 枚举名用 `MouseButtonPress/Release`（非 Qt 5 的 `MousePress/Release`）；`QEvent::Enter`（未注册、不 ignored）验证 identity 兜底；定时器测试改单次触发（`SingleShotTimerContext`）消除 ctest 负载下的时序抖动。
- 顺带落地 04 Rev 10 P3-2：`CtPendingEventBase` 补 `virtual std::string_view typeName()`（经 visit 取 trait name），替换队列 trace `front()` kQueued 与 `dispatchAny` 目标已亡 kDropped 的 `"<async>"` 占位（`popDropped()` 溢出计数无事件对象仍记占位）；04 文档 §7.10 契约 #6 已同步。
- 05-qt-bridge.md 已同步：§3.2 契约 #1/#2、§3.3 契约 #1/#4、§9 #1、修订记录 Rev 5。
