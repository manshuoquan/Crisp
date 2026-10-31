# CRISP UI1 核心基座架构设计（crisp::ui）

> 状态：**权威规范（完整）**——§1–§17 全部定稿；§2 类型声明与 §3 接口为 UI1 唯一权威接口规范，编码 agent 无需猜测即可实现；**本文不包含任何 src/ 实现代码**。
> 本文档已按"实现落地后回写"流程与 `src/ui/` 现有实现（commit `6fca9a9` UI1 minimal scaffold）核对一致：§2/§3 的权威类型与 `ct_geometry.h`/`ct_widget_trace.h`/`ct_widget_state.h`/`ct_widget_base.h` 完全对应；§8 决策 D1–D5 与代码注释中的 DECISION 标记一一对应。

---

## 1. 范围（Scope）

### 1.1 做（UI1 交付）

UI1 交付以下能力，构成 UI 子系统的"核心基座"（对齐 `03-rewrite-proposal.md` §4.1 与 §5 里程碑行）：

1. **CtWidgetBase 对象树**：`shared_ptr` 所有权树 + `weak_ptr` 子引用 + RAII 析构摘除（P5）；创建唯一入口 `createWidget<T>()` / `CtWidgetBase::create()`（仿 `createReceiver<T>()` 先例）；`setParent` 迁移唯一重排入口。
2. **显式状态**：`CtWidgetState`（visible/enabled/focusable/hasFocus/hovered/focusPolicy）替代位域（P12）。
3. **typed 输入**：`dispatchInput(CtEvent<CtMousePayload>&)` / `dispatchInput(CtEvent<CtKeyPayload>&)` 消费核心层 `crisp::input` 载荷（**已落地**，UI pre-M1 commit c3f0b2f）；禁用态拦截；`hitTest` 供 UI3 根分发使用。
4. **脏区**：`markDamage(CtRect)` / `dirtyRegion()` / `clearDamage()`；首版单矩形容斥式并集近似（裁决见 §2.2 与 §8）。
5. **可观测**：独立枚举 `CtWidgetPhase` + 独立记录 `CtWidgetRecord` + `CtWidgetTrace` 环形缓冲（仿 `CtTaskTrace` mutex-guarded 版）；`kCreated/kDestroyed` 成对轨迹。

### 1.2 不做（明确归后续里程碑）

| 能力 | 归属 | 依据 |
|------|------|------|
| 绘制（CtPainter / CtPaintBackend / 双缓冲 / BitBlt） | **UI2** | §4.2 |
| 布局（CtBoxLayout 两遍算法） | **UI4** | §4.4.3 |
| 基础控件（CtLabel / CtPushButton / CtLineEdit）与交互状态机（双击/hover 判定） | **UI4** | §4.4.1–4.4.2 |
| 焦点链（CtFocusChain 显式链表 / Tab/Backtab 步进 / kFocusIn/kFocusOut 轨迹产生） | **UI4** | §4.4.4 |
| 平台窗口（CtPlatformWindow 双模式 / Win32 消息泵 / Qt 宿主）与输入映射器（timestamp 注入点） | **UI3** | §4.3 |
| 语义事件载荷（CtClickPayload / CtTextChangedPayload / CtFocusPayload） | **UI4** | §4.4.4 |
| 控件级定时器（闪烁光标 / 长按重复，CtTimerEventSource 消费） | **UI3+ 随映射器** | 裁决见 §2.3 附注与 §8 |
| 样式（CtStyle / CtBasicStyle） | **UI5** | §4.5 |

> UI1 内**不产生** `kPainted/kFocusIn/kFocusOut` 记录（相位枚举先定义完整，见 §2.3）；产生 `kCreated/kDestroyed/kDamage` 三种。UI1 的 `kDamage` 记录在 `markDamage` 合批时产生（每 markDamage 调用一条，含当前累计 damageRectCount）。

### 1.3 审查残留意见落实对照表

| # | 残留意见 | 落实位置 |
|---|---------|---------|
| 1 | 输入字段消费契约（哪些字段消费/忽略） | **§3.10**（权威契约表）+ §6.2 用例 `ui1_input_field_consumption_contract` |
| 2 | timestamp 支撑（开放问题 #2） | **§3.10.3 裁决**（UI1 契约先定存在性，字段落地随 UI3 映射器）+ §13 开放问题 #2 状态 |
| 3 | WM_TIMER 二选一 | **§8 裁决 D4**（UI1 不涉及，UI3 定稿） |
| 4 | CtTimerEventSource 模板参数（控件级定时） | **§2.3 附注 + §8 裁决 D5**（UI1 不规划控件级定时，注明 EventSet 模板注意点） |
| 5 | CtRegion 首版单矩形并集近似 | **§2.2 裁决 + §8 裁决 D2**（并同步修订 03 §4.1.4/§4.1.6） |

---

## 2. 新类型定义（New Types）

> 本文件 §2 的全部类型声明为 **UI1 权威规范**：编码 agent 直接照抄到 `src/ui/ct_geometry.h`（CtRect/CtRegion）、`src/ui/ct_widget_trace.h`（CtWidgetPhase/CtWidgetRecord/CtWidgetTrace）、`src/ui/ct_widget_state.h`（CtWidgetState/CtFocusPolicy）。头文件命名与 §7.1 CMake 源列表一致。

### 2.1 crisp::ui::CtRect

```cpp
#pragma once

// ct_geometry.h — UI1 core geometry primitives (crisp::ui).
// Value semantics; int coordinates (widget-local and parent-local in UI1,
// root/window coordinates from UI3). Zero heap allocation everywhere.

#include <algorithm>
#include <cstdint>

namespace crisp::ui {

/// Axis-aligned integer rectangle. Canonical form: width/height >= 0
/// (an empty rect may have width/height == 0 and any x/y).
/// Value semantics; hot-path operations are constexpr-friendly, allocation-free.
struct CtRect {
    int x = 0;        ///< left edge (inclusive)
    int y = 0;        ///< top edge (inclusive)
    int width = 0;    ///< width in pixels (>= 0)
    int height = 0;   ///< height in pixels (>= 0)

    /// Right edge, exclusive.
    int right() const noexcept { return x + width; }
    /// Bottom edge, exclusive.
    int bottom() const noexcept { return y + height; }

    /// True if width <= 0 || height <= 0.
    bool isEmpty() const noexcept { return width <= 0 || height <= 0; }

    /// True if `px`/`py` lies inside [x, right()) x [y, bottom()).
    bool contains(int px, int py) const noexcept {
        return px >= x && px < right() && py >= y && py < bottom();
    }

    /// True if `other` intersects this rect (non-empty overlap).
    /// Empty rects never intersect.
    bool intersects(const CtRect& other) const noexcept;

    /// The intersection of this and `other`; empty if disjoint.
    /// Result may be empty (caller checks isEmpty()).
    CtRect intersect(const CtRect& other) const noexcept;

    /// The smallest rect covering both (union bounding box).
    /// If either operand is empty, returns the other (copy).
    CtRect united(const CtRect& other) const noexcept;
};

} // namespace crisp::ui
```

**契约**：
- 值语义，全部操作无堆分配；`right()/bottom()` 为**开区间**边界（半开区间约定，与事件系统 `CtDispatchContext` 的数值区间习惯一致）。
- `isEmpty()` 是 `width <= 0 || height <= 0`（允许负值输入被归一，见 `setGeometry` 归一化契约 §3.4）。
- `intersect` 使用半开区间数学：`max(x1,x2) .. min(right1,right2)`，结果宽度为负时置空（width/height 归 0）。
- `united` 为单矩形包围盒（**首版并集近似的基础操作**，见 §2.2 裁决）。
- 实现可全部为 inline/constexpr，但**头文件只声明、实现放 `ct_geometry.cpp`**（AGENTS.md：`.h` 与 `.cpp` 严格分离；除模板/inline 必需外禁止头文件函数体）。inline 关键字允许用于热路径访问器。

### 2.2 crisp::ui::CtRegion

```cpp
namespace crisp::ui {

/// Accumulated dirty region (UI1: single-rectangle union approximation).
///
/// DECISION D2 (authoritative, §8): UI1 represents the dirty region as ONE
/// bounding rectangle — markDamage() unions into it via CtRect::united().
/// Rationale: (1) hot-path zero heap allocation (a multi-rect list would
/// need storage growth on the damage path); (2) simple batching — one
/// repaint pass covers all accumulated damage; (3) UI2's backing store
/// flush consumes exactly one rect per widget. UI2 may evolve this into a
/// multi-rect list (rect count bounded, preallocated) WITHOUT changing the
/// public API shape (dirtyRegion() still returns a single CtRect).
struct CtRegion {
    CtRect bounds;   ///< union bounding box of all marked damage (may be empty)

    /// True if no damage has been accumulated.
    bool isEmpty() const noexcept { return bounds.isEmpty(); }

    /// Union `rect` into the region (bounding-box approximation).
    /// `rect` may be empty: no-op (an empty rect unions to a copy of
    /// the other operand — CtRect::united contract).
    void add(const CtRect& rect) noexcept;

    /// Reset to empty.
    void clear() noexcept { bounds = CtRect{}; }
};

} // namespace crisp::ui
```

**裁决 D2（单矩形并集近似）**：
- **首版 = 单矩形容斥式并集**：`markDamage(rect)` 内部 = `m_dirty.bounds = m_dirty.bounds.united(rect)`，**热路径零堆分配、零锁、O(1)**。
- 合批语义：一帧内 N 次 markDamage → 1 个包围盒 → UI2 flush 一次绘制全部（对齐 `QWidgetRepaintManager` 先合后绘，03 §4.1.4）。
- **不做**多矩形列表 / 矩形扫描线合并（UI2 演进点，开放问题 §13 O2）；UI2 演进不改公共 API 形状。
- 归一化：`setGeometry` 内部把负 width/height 归一为 0（§3.4 契约）。

### 2.3 crisp::ui::CtWidgetPhase

```cpp
#pragma once

// ct_widget_trace.h — widget-side trace (crisp::ui).
// CtWidgetPhase is deliberately INDEPENDENT from crisp::event::CtTracePhase
// (precedent: crisp::thread::CtTaskPhase, docs/threading/03 §3.4):
// extending the event enum would change its uint8_t value set and affect the
// event subsystem's ABI/spec. Widget records live in their own ring and are
// correlated with event-side traces by timestamp at UI3's input mapper.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "event/ct_thread_affinity.h" // crisp::event::CtThreadId

namespace crisp::ui {

/// Widget lifecycle/damage/focus phases (UI1: kCreated/kDestroyed/kDamage
/// are PRODUCED; kPainted/kFocusIn/kFocusOut are DEFINED now, produced from
/// UI2/UI4 — kept so the enum is stable across milestones, same rationale
/// as CtTaskPhase's full set in MT1).
enum class CtWidgetPhase : uint8_t {
    kCreated   = 0, ///< widget constructed + attached to parent (UI1)
    kDestroyed = 1, ///< destructor ran, detached from parent (UI1)
    kDamage    = 2, ///< damage marked, region unioned (UI1)
    kPainted   = 3, ///< one repaint pass flushed (UI2)
    kFocusIn   = 4, ///< focus entered this widget (UI4)
    kFocusOut  = 5, ///< focus left this widget (UI4)
};
```

### 2.4 crisp::ui::CtWidgetRecord

```cpp
/// One widget-side trace record (UI1). Filled by CtWidgetBase on the UI
/// thread; read by diagnostics/tests via CtWidgetTrace::copyOut().
/// Value semantics; NO heap allocation on the record() hot path.
struct CtWidgetRecord {
    CtWidgetPhase phase = CtWidgetPhase::kCreated;
    std::uint64_t objectId = 0;        ///< this widget's objectId (CtReceiver::objectId)
    std::uint64_t parentObjectId = 0;  ///< parent's objectId (0 = top-level)
    std::chrono::steady_clock::time_point when{}; ///< steady_clock, filled at record()
    std::uint32_t damageRectCount = 0; ///< accumulated markDamage count since last clearDamage
                                       ///< (kDamage records only; 0 otherwise)
};
```

### 2.5 crisp::ui::CtWidgetState

```cpp
#pragma once

// ct_widget_state.h — explicit widget state (crisp::ui, UI1).
// Replaces QWidgetData's bitfields + WA_* attribute macros (pain point P12).
// Plain struct-of-bools; hot-path cost unchanged (member of CtWidgetBase,
// no heap).

#include <cstdint>

namespace crisp::ui {

/// Focus policy enum (UI4 consumes; defined in UI1 so CtWidgetState is
/// complete — same "define now, produce later" rule as CtWidgetPhase).
enum class CtFocusPolicy : uint8_t {
    kNoFocus = 0,   ///< not focusable via Tab/click (UI4)
    kClickFocus,    ///< focusable by click only (UI4)
    kTabFocus,      ///< focusable by Tab/Backtab only (UI4)
    kStrongFocus,   ///< both click and Tab (UI4)
};

/// Explicit widget state — replaces bitfields (P12).
struct CtWidgetState {
    bool visible = true;    ///< participates in layout + painting (UI1: flag only)
    bool enabled = true;    ///< disabled widgets reject input (§3.6, §3.10)
    bool focusable = false; ///< participates in focus chain (UI4)
    bool hasFocus = false;  ///< current focus owner (UI4)
    bool hovered = false;   ///< UI4 state machine owns transitions; UI1: inert flag
    CtFocusPolicy focusPolicy{CtFocusPolicy::kNoFocus}; ///< UI4 consumes
};

} // namespace crisp::ui
```

### 2.6 crisp::ui::CtFocusPolicy

（声明见 §2.5 代码块内；无独立头文件，`ct_widget_state.h` 承载。）

### 2.7 crisp::ui::CtWidgetTrace

```cpp
/// Fixed-capacity ring buffer of widget trace records (UI1).
///
/// DECISION D3 (authoritative, §8): mutex-guarded ring, modeled on
/// crisp::thread::CtTaskTrace (not the SPSC CtTrace from the event system).
/// Rationale: UI thread is the SINGLE writer (record() inside
/// CtWidgetBase::markDamage/ctor/dtor), but diagnostics and tests read
/// from any thread (copyOut) — a plain SPSC ring would make cross-thread
/// reads UB. Trace writes are NOT the zero-allocation hot path (the hot
/// path is markDamage/dirtyRegion itself); the mutex cost is acceptable.
class CtWidgetTrace {
public:
    /// Record one widget trace entry. Returns false if the ring was full
    /// and the oldest entry was dropped (overflow, bounded ring).
    bool record(const CtWidgetRecord& rec);

    /// Number of records currently stored.
    std::size_t size() const noexcept;

    /// Copy stored records out (diagnostics / tests). Returns count copied.
    std::size_t copyOut(CtWidgetRecord* out, std::size_t max) const;

    static constexpr std::size_t kCapacity = 4096; ///< bounded ring (power of 2),
                                                   ///< mirrors CtTaskTrace::kCapacity

private:
    mutable std::mutex m_mutex;   ///< guards ring (writer: UI thread; readers: any)
    std::size_t m_head = 0;       ///< next write slot
    std::size_t m_count = 0;      ///< valid records (<= kCapacity)
    CtWidgetRecord m_ring[kCapacity]{}; ///< ring storage (no heap)
};

} // namespace crisp::ui
```

**契约**：
- `record()`：mutex 加锁、环形写入；满则丢弃最旧（bounded，仿 `CtTaskTrace` 溢出语义与 `kMaxQueued = 8192` 纪律）。
- `copyOut()`：mutex 加锁、按时间顺序拷贝到调用者提供的缓冲区（`out` 容量 `max`）；返回实际拷贝数。调用者负责 `out` 容量（测试/诊断路径，允许堆分配）。
- 轨迹关联：UI3 输入映射器按 `when`（steady_clock）与事件侧 `CtTrace` 交叉关联（对齐 threading `CtTaskTrace` 的时间关联约定）。

---

## 3. CtWidgetBase 完整接口

> 本节是 UI1 权威接口规范：与 `src/ui/ct_widget_base.h` 完全一致（实现已落地于 commit `6fca9a9`，本节为回写定稿）。所有变更必须同步本文档。

### 3.1 类声明（完整、可直接照抄）

```cpp
class CtWidgetBase : public crisp::event::CtReceiver {
public:
    static std::shared_ptr<CtWidgetBase> create(
        const std::shared_ptr<CtWidgetBase>& parent = {});

    template <typename W = CtWidgetBase, typename... Args>
    static std::shared_ptr<W> createWidget(
        const std::shared_ptr<CtWidgetBase>& parent, Args&&... args);

    virtual ~CtWidgetBase();

    CtWidgetBase(const CtWidgetBase&) = delete;
    CtWidgetBase& operator=(const CtWidgetBase&) = delete;

    // tree
    std::weak_ptr<CtWidgetBase> parent() const noexcept;
    const std::vector<std::weak_ptr<CtWidgetBase>>& children() const noexcept;
    void setParent(const std::shared_ptr<CtWidgetBase>& newParent);
    std::shared_ptr<CtWidgetBase> childAt(int px, int py) const;

    // geometry
    CtRect geometry() const noexcept;
    void setGeometry(const CtRect& rect);
    int width() const noexcept;
    int height() const noexcept;

    // state
    void setVisible(bool visible) noexcept;
    bool isVisible() const noexcept;
    void setEnabled(bool enabled) noexcept;
    bool isEnabled() const noexcept;
    bool focusable() const noexcept;
    bool hasFocus() const noexcept;
    const CtWidgetState& widgetState() const noexcept;

    // input
    void dispatchInput(crisp::event::CtEvent<crisp::input::CtMousePayload>& event);
    void dispatchInput(crisp::event::CtEvent<crisp::input::CtKeyPayload>& event);
    bool hitTest(int px, int py) const noexcept;

    // damage (P4 / D2)
    void markDamage(const CtRect& rect) noexcept;
    const CtRegion& dirtyRegion() const noexcept;
    void clearDamage() noexcept;

    // observability (P6 / D3)
    CtWidgetTrace& trace() noexcept;
    std::uint64_t objectId() const noexcept;
    crisp::event::CtThreadId affinityThread() const noexcept;

protected:
    CtWidgetBase();                                          // top-level, factory-only
    explicit CtWidgetBase(const std::shared_ptr<CtWidgetBase>& parent);

private:
    void recordTrace(CtWidgetPhase phase, std::uint32_t damageRectCount = 0);
    void attachTo(const std::shared_ptr<CtWidgetBase>& newParent);
    void detachFromParent();

    std::weak_ptr<CtWidgetBase> m_parent;          ///< weak back-reference
    std::weak_ptr<CtWidgetBase> m_selfWeak;        ///< set by factories before tree ops
    std::vector<std::weak_ptr<CtWidgetBase>> m_children; ///< z-order, back = topmost
    CtRect m_geometry;                             ///< parent-local
    CtWidgetState m_state;                         ///< explicit state (P12)
    CtRegion m_dirty;                              ///< accumulated damage (D2)
    std::uint32_t m_damageRectCount = 0;           ///< markDamage calls since clearDamage
    CtWidgetTrace m_trace;                         ///< widget-side ring (D3)
};
```

**契约要点**（完整语义见 §3.2–§3.10 与 §4）：
- **单 `enable_shared_from_this` 基类**：`CtWidgetBase` 只继承 `CtReceiver`（其已带 `enable_shared_from_this`），**不得**再叠加第二个 `enable_shared_from_this` 基类——`shared_ptr` 构造只初始化一个基类的弱引用，第二个基类的 `shared_from_this()` 会抛 `bad_weak_ptr`（静默破坏 `bindHandler`）。树的成员资格用工厂设置的 `m_selfWeak`（纯弱自引用）表达。
- **构造唯一入口**：`create()` / `createWidget<W>()` 工厂。子类构造器首参必须是 `shared_ptr<CtWidgetBase> parent`，且构造器内**不得触碰对象树**（`m_selfWeak` 在子类构造器返回后才设置，树调用会存空 weak_ptr）。
- **析构**：`virtual ~CtWidgetBase()` 先记 `kDestroyed`（保持父关联）再 `detachFromParent()`（RAII 摘除）。
- 全部变更方法（`setParent`/`setGeometry`/`setVisible`/`setEnabled`/`dispatchInput`/`markDamage`/`clearDamage`）遵守 §4.2 线程守门：debug abort / release 降级 no-op。

### 3.2 工厂：createWidget<T> / create

- `create(parent)`：构造顶层或带父控件；`createWidget<W>(parent, args...)`：构造子类。两者都在 `shared_ptr` 构造完成后设置 `m_selfWeak`，再 `attachTo(parent)`。
- **P5 唯一合法构造路径**：直接 `make_shared`/`new` 构造 `CtWidgetBase` 属契约违规（`m_selfWeak` 为空 → 树操作产生 zombie 弱引用，debug 下 `attachTo` 断言失败）。
- **实现细节（审计 WARN 已接受）**：`create` 内部用 `new`（非 `make_shared`）——受保护构造器在 MinGW 8.1 libstdc++ 下无法被 `make_shared` 的分配器合法调用（已实证）；裸指针立即被 `shared_ptr` 接管，不逃逸（非热路径）。

### 3.3 树操作：parent / children / setParent / childAt

- `parent()` 返回弱引用（顶层为空）；`children()` 返回 `const vector<weak_ptr<CtWidgetBase>>&`（z 序：**末尾 = 最顶**）。
- `setParent(newParent)`：唯一重排入口，**事务性**——先 `attachTo(newParent)`（`emplace_back` 强异常保证，抛 `bad_alloc` 时树不变），成功后才从旧父列表移除；`newParent == nullptr` 时置空 `m_parent` 并从旧父移除。
  - **同父重排 = 幂等 no-op**（防重复入队后 remove_all 误删整条链，破坏"父列表每子恰一次"不变量）。
  - **拒绝自父化与环**：沿新父的弱链上溯，遇到 `this` 即拒绝（防弱环）。
- `childAt(px, py)`：按 z 序**逆序**遍历（最顶先），跳过 stale 弱引用，返回首个 `geometry().contains(px, py)` 的子控件；无则 `nullptr`。UI3 根分发器用它做命中路由。
- **stale 弱引用**：子控件析构后其条目在下次 attach/detach 时被清除；`childAt` 跳过不返回。

### 3.4 几何：geometry / setGeometry / size / position

- `geometry()` 返回父坐标系 `CtRect`；`setGeometry(rect)` 执行**归一化**：负 width/height 归 0（保持 `CtRect` 规范形）。
- 线程守门：`setGeometry` 非本线程时 debug abort / release no-op。
- `width()/height()` 为控件局部尺寸；`position` 由 `geometry().x/y` 表达（无独立访问器）。
- UI1 坐标为控件局部/父局部；根/窗口坐标自 UI3 起。

### 3.5 状态：setVisible / setEnabled / isVisible / isEnabled / focusable / hasFocus

- `setVisible`/`setEnabled`：仅设标志（UI1 不产生布局/绘制效应，消费方为 UI4/UI2）；非本线程 debug abort / release no-op。
- `focusable()`/`hasFocus()`：UI1 惰性标志（UI4 消费），仅提供 API 稳定性。
- `widgetState()`：只读视图暴露完整 `CtWidgetState`（UI4+ 消费）。

### 3.6 输入：dispatchInput / hitTest

- `dispatchInput(CtEvent<CtMousePayload>&)` / `dispatchInput(CtEvent<CtKeyPayload>&)`：typed 输入入口，消费核心层 `crisp::input` 载荷（UI pre-M1 已落地）。
- **禁用态拦截**：`!m_state.enabled` → `event.ignore()` 且不调用任何 handler（§3.10 契约）。
- 载荷坐标为**控件局部**（`crisp::input` 契约）；UI1 无超越控件自身的命中测试，树级路由由 UI3 根分发器经 `childAt()`/`hitTest()` 完成。
- **UI1 暂态**：typed handler 注册面（`CtDispatcher` 挂接）尚未定稿，`dispatchInput` 为"通过门禁的 no-op 载体"（记录不产生）；UI4 经 `CtReceiver::bindHandler` 接线。
- `hitTest(px, py)`：`m_geometry.contains(px, py)`，const noexcept。

### 3.7 脏区：markDamage / dirtyRegion / clearDamage

- `markDamage(rect)`（widget-local，热路径）：`m_dirty.add(rect)`（O(1) 单矩形并集，**零堆分配**，见 D2）+ `++m_damageRectCount` + `recordTrace(kDamage, m_damageRectCount)`（一条 D3 互斥锁记录）。
- `dirtyRegion()`：返回 `const CtRegion&`（累积单矩形包围盒）；UI2 backing store 在 flush 时消费。
- `clearDamage()`：清空区域并重置计数（UI2 flush 后调用）。
- 线程守门：非本线程 debug abort / release no-op（跨线程损伤会静默腐蚀脏区）。

### 3.8 可观测：record / copyOut

- `recordTrace(phase, damageRectCount)`：私有，构造/析构/`markDamage` 内调用；填 `CtWidgetRecord`（phase/objectId/parentObjectId/when=steady_clock::now()/damageRectCount）写入 `m_trace`。
- `trace()` 返回控件自身 `CtWidgetTrace`（构造时创建）；`copyOut` 供诊断/测试跨线程读取（D3 互斥环）。
- **成对保证**：`kCreated`（构造）与 `kDestroyed`（析构，摘除前）记录同一 `parentObjectId`（§2.7 裁决）。

### 3.9 与 CtReceiver 的关系（裁决：继承）

**裁决 D1（§8）：`CtWidgetBase` 继承 `crisp::event::CtReceiver`**。
- 收益：稳定 `objectId` + 线程亲和（`CtThreadAffinity`）+ `bindHandler<T>` 注册入口（UI4 typed handler 用）。
- 约束：单 `enable_shared_from_this` 基类（见 §3.1），树成员用 `m_selfWeak`。
- 依据：事件系统先例（`createReceiver<T>` 工厂）、六原则"类型安全第一"（typed handler 需接收者承载）。

### 3.10 字段消费契约（输入载荷消费/忽略明细）

**权威契约表**（UI1 `dispatchInput` 通过门禁时的行为；handler 级消费随 UI4 `bindHandler`）：

| 载荷字段 | UI1 处理 | 说明 |
|----------|---------|------|
| `CtMousePayload.action`（kMove/kPress/kRelease/kDblClick） | **忽略**（透传） | UI1 无交互状态机；UI4 消费 |
| `CtMousePayload.x/y` | **忽略**（透传） | widget-local 坐标，UI1 无命中消费 |
| `CtMousePayload.button`（optional） | **忽略** | 变更按钮；UI4 状态机消费 |
| `CtMousePayload.buttons`（位掩码） | **忽略** | 按住集合；UI4 消费 |
| `CtMousePayload.modifiers` | **忽略** | UI4 消费（Ctrl/Shift 判定） |
| `CtKeyPayload.action`（kPress/kRelease） | **忽略** | UI4 消费 |
| `CtKeyPayload.key`（CtKeyButton） | **忽略** | UI4 消费 |
| `CtKeyPayload.text`（u16string） | **忽略** | 文本输入随 UI4 `CtLineEdit` |
| `CtKeyPayload.modifiers` | **忽略** | UI4 消费 |
| `CtKeyPayload.isAutoRepeat` | **忽略** | UI4 消费 |
| 事件 `accepted` 位 | **唯一消费** | 禁用态置 `ignore()`（拒绝输入）；启用态置"未处理"（不置 accepted） |

> **裁决（§3.10.3）**：UI1 契约先定"字段存在性 + 禁用态门禁"，逐字段消费语义随 UI4 交互状态机定稿；timestamp 字段落地随 UI3 输入映射器（见 §13 开放问题 #2）。测试用例 `ui1_input_field_consumption_contract`（§6.2）锁定禁用态 gate 行为。

---

## 4. 线程与所有权契约

### 4.1 UI 线程 = 主线程（单线程模型）

- UI1 采用**单线程模型**：所有控件操作在控件亲和线程（经 `CtReceiver` 继承的 `CtThreadAffinity`，默认 = 创建线程）上执行；UI3 事件循环集成后，平台输入仅在 UI 线程路由。
- 跨线程操作控件在 UI1 明确**越界**（见 §4.3）。worker 线程绝不触碰控件（对齐 MT 桥纪律，`docs/threading/03` §3.1 规则 1）。

### 4.2 assertSameThread 守门

- 每个变更方法（`setParent`/`setGeometry`/`setVisible`/`setEnabled`/`dispatchInput`/`markDamage`/`clearDamage`）入口执行 `assertSameThread()`：
  - **debug 构建**：断言失败 → abort（立即暴露跨线程违规）。
  - **release 构建**：返回 false → 方法**降级为 no-op**（不产生副作用、不写轨迹），并返回调用方。
- 依据：事件系统 §7.14 先例（`CtThreadAffinity` 契约）。

### 4.3 跨线程一律 postTo

- 跨线程与控件通信的唯一通道是事件投递 `post/postTo`（M2 语义），**绝不直接调用控件方法**。
- UI1 不实现控件级跨线程投递面（UI3 事件循环集成后由 UI 线程统一消费平台输入）；跨线程直接改控件 = 违反 §4.2，debug 必崩、release 静默 no-op（可观测性靠 §4.2 的断言纪律，UI1 无额外轨迹）。

### 4.4 所有权：shared_ptr 树 + weak_ptr 子引用 + RAII 析构

- **所有权模型**（P5）：每个 `CtWidgetBase` 由外部持有者经 `shared_ptr` 拥有其整个生命周期；**树内只有弱引用**——子控件对父持弱回引用（`m_parent`），父对子持弱条目（`m_children`）。无强父子边 → 无强环、无手工 delete、无 `deleteLater`。
- **`m_selfWeak`**：工厂设置的本控件纯弱自引用，树成员资格与摘除身份比对都基于它（§3.1/§3.3）。
- **RAII 析构**：析构 = 记 `kDestroyed` + `detachFromParent()`（从父 z 序列表摘除自身条目 + 清 `m_parent`）。子控件随外部引用释放自动析构并自动解注册，无泄漏、无悬垂条目。

### 4.5 析构顺序与弱引用防悬垂

- **析构顺序**：子类析构体 → 基类 `~CtWidgetBase`（记 `kDestroyed`，仍保有父关联）→ `detachFromParent()`。父控件先于子控件析构时，子控件经弱引用 `parent().lock()` 拿不到父 → 摘除为空操作，子控件仍独立存活（弱引用天然防悬垂）。
- **身份比对**：`detachFromParent`/`setParent` 移除列表条目用 **`owner_before` 双向比对**（`!w.owner_before(m_selfWeak) && !m_selfWeak.owner_before(w)`）——析构期间 `lock().get() == this` 失效（强计数已归零），`owner_before` 对**过期弱指针仍有效**（只要还持有控制块）。这是"析构时摘除不残留 zombie 条目"的关键（测试 `widget_dtor_detaches_from_parent` 覆盖）。
- **`m_selfWeak` 有效性判定**：`detachFromParent` 的守卫必须测"是否有控制块"（`empty.owner_before(m_selfWeak) || m_selfWeak.owner_before(empty)`），**不是** `!m_selfWeak.expired()`——析构时 `expired()` 恰为 true（最后一个强引用已释放）。

---

## 5. 关键路径零堆分配

### 5.1 零分配路径清单

| 路径 | 是否零堆分配 | 依据 |
|------|-------------|------|
| `markDamage(rect)` 热路径（并集运算） | ✅ **零堆分配** | D2 单矩形并集，O(1)，无存储增长 |
| `markDamage` 的 `kDamage` 轨迹 | ✅ 环内写入零分配（1 次互斥锁 + 时钟读） | D3 固定容量环，见 §2.7（锁不在零分配热路径上） |
| `dirtyRegion()` / `clearDamage()` | ✅ 零分配 | 纯访问/重置 |
| `hitTest` / `geometry` / `width`/`height` | ✅ 零分配 | 值语义 |
| `create`/`createWidget`（构造路径） | ⬜ 允许分配 | 非热路径；`shared_ptr`/`vector` 扩容 |
| `setParent`（重排） | ⬜ 允许分配 | `emplace_back` 可能 `bad_alloc`（事务性保证，见 §3.3） |
| `CtWidgetTrace::copyOut` | ⬜ 允许分配 | 诊断/测试路径，调用者提供缓冲区 |
| `CtWidgetRecord` 值语义 | ✅ 零堆分配 | 纯 POD 结构（`std::u16string` 仅在 `CtKeyPayload`，不在轨迹内） |

**限定**：零分配声明限于**上述热路径**；UI1 的"零分配"验收以 `markDamage` 单次调用为界（§5.2），不扩展到构造/重排/诊断路径。

### 5.2 验收方法（operator new 计数）

- 测试二进制（`tests/ui/test_ct_widget_base.cpp`）定义**全局计数版 `operator new`/`operator new[]`**（+ 配套 `delete` 重载），以原子计数 + 开关标志（`g_counting`）在**预热后**武装。
- 流程：构造控件 + 设置几何（构造路径允许分配，在计数前完成）→ `markDamage` 预热一次 → `clearDamage` → 清零计数并武装 → 连续两次 `markDamage` → 解除武装 → 断言 `allocs == 0` 且脏区并集正确。
- 用例：`widget_mark_damage_hot_path_allocates_zero`（§6.2）。

---

## 6. 测试计划（UI1 验收）

### 6.1 测试文件布局

```
tests/ui/
├── CMakeLists.txt              # ui_tests 可执行目标（§7.3）
├── main.cpp                    # 注册并运行全部用例（CtTestRegistry）
├── test_ct_geometry.cpp        # CtRect/CtRegion 几何（§2.1/§2.2）
├── test_ct_widget_trace.cpp    # CtWidgetTrace 环（§2.7/D3）
└── test_ct_widget_base.cpp     # CtWidgetBase 树/状态/输入/脏区/零分配（§3）
```

- 复用事件测试的 `test_harness.h`（手写 test double，**禁止 mock 框架**，项目规则）；`tests/ui/CMakeLists.txt` 以 `../event` 作为 include 目录。

### 6.2 测试用例清单（对应 §5 里程碑行）

| 用例 | 覆盖契约 | 状态 |
|------|---------|------|
| `rect_half_open_bounds_are_exclusive` | CtRect 半开区间（right/bottom 开区间、contains 边界） | ✅ 已实现 |
| `rect_isEmpty_normalizes_non_positive_size` | isEmpty 对 0/负尺寸判定 | ✅ 已实现 |
| `rect_intersects_half_open_math` | 相交半开数学、空矩形永不相交 | ✅ 已实现 |
| `rect_intersect_returns_empty_when_disjoint` | 交集为空时返回空矩形、重叠返回本身 | ✅ 已实现 |
| `rect_united_returns_bounding_box` | 并集包围盒、空操作数透传 | ✅ 已实现 |
| `region_unions_into_single_bounding_rect` | D2 单矩形并集、空/负矩形 no-op、clear | ✅ 已实现 |
| `widget_trace_records_and_copy_out_in_order` | 记录/按序拷出（最旧在前） | ✅ 已实现 |
| `widget_trace_copy_out_respects_capacity` | copyOut 容量尊重 | ✅ 已实现 |
| `widget_trace_drops_oldest_on_overflow` | 满环丢最旧、size 恒等容量 | ✅ 已实现 |
| `widget_create_attaches_to_parent_and_records_created` | 工厂附着 + kCreated 轨迹（含 parentObjectId） | ✅ 已实现 |
| `widget_set_parent_moves_ownership` | setParent 迁移（旧父移除/新父加入/顶层分离） | ✅ 已实现 |
| `widget_dtor_detaches_from_parent` | RAII 析构摘除 + childAt 无残留 | ✅ 已实现 |
| `widget_geometry_normalizes_and_hit_tests` | 几何归一化 + hitTest 边界 | ✅ 已实现 |
| `widget_child_at_returns_topmost` | z 序逆序命中（最顶优先） | ✅ 已实现 |
| `widget_damage_unions_and_traces` | 脏区并集 + kDamage 计数轨迹 + clearDamage 复位 | ✅ 已实现 |
| `widget_disabled_rejects_input` | 禁用态门禁（mouse/key 均 ignore；启用态不置 accepted） | ✅ 已实现 |
| `widget_state_defaults_are_explicit` | 状态默认值显式（visible/enabled/focusPolicy） | ✅ 已实现 |
| `widget_create_widget_factory_attaches_subclass` | `createWidget<W>` 子类工厂附着 + 命中 + 轨迹 | ✅ 已实现 |
| `widget_cycle_reparent_is_rejected` | 环重排拒绝（祖先/自父） | ✅ 已实现 |
| `widget_set_parent_to_same_parent_is_noop` | 同父重排幂等 no-op（树不变量） | ✅ 已实现 |
| `widget_create_widget_factory_destroy_subclass_cleans_tree` | 子类工厂路径 RAII 摘除 | ✅ 已实现 |
| `widget_destroyed_trace_keeps_parent_association` | kDestroyed 保持父关联（§2.7 裁决） | ✅ 已实现 |
| `widget_mark_damage_hot_path_allocates_zero` | §5.2 零分配验收（operator new 计数） | ✅ 已实现 |

> 全部用例已在 `tests/ui/` 落地并通过（基线 `ui_tests` 全绿）。**§3.10 字段消费契约用例**（`ui1_input_field_consumption_contract`）由禁用态门禁用例覆盖（`widget_disabled_rejects_input`），逐字段 handler 级消费随 UI4 补充。

### 6.3 零分配计数测试细节

- 计数重载**只在测试二进制内**生效（全局 `operator new` 重定义在 `test_ct_widget_base.cpp`），不污染其他测试。
- 武装时机：**预热后**（构造/几何/trace 环就绪），避免把注册与构造分配计入热路径。
- 断言：两次 `markDamage` 后 `g_allocCount == 0`；并集几何 `(10..60)x(50..50)` → `width==50, height==50` 验证并集正确性。

---

## 7. CMake 集成

### 7.1 src/ui/CMakeLists.txt

```cmake
add_library(crisp_ui STATIC
    ct_geometry.cpp
    ct_widget_trace.cpp
    ct_widget_base.cpp)

target_include_directories(crisp_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>)

target_compile_features(crisp_ui PUBLIC cxx_std_17)
target_link_libraries(crisp_ui PUBLIC crisp::event)   # CtReceiver base, payloads
add_library(crisp::ui ALIAS crisp_ui)
```

- **静态库**：非模板机制放 `.cpp`（AGENTS.md §5.3）；几何/轨迹/状态值类型留头文件。**纯 C++17，零 Qt/Win32 依赖**（平台层例外条款见 `docs/ui/README.md`：UI3 `CtPlatformWindow` 才允许依赖 Win32/GDI 或 Qt）。

### 7.2 顶层 CMakeLists.txt 修改

- 在事件（`crisp::event`）与线程（`crisp::thread`）子目录之后 `add_subdirectory(src/ui)`；`tests/` 下对应 `add_subdirectory(tests/ui)`。
- 顶层 install/导出若已含 `crisp::event`，同步加入 `crisp::ui` 别名。

### 7.3 tests/ui/CMakeLists.txt

```cmake
add_executable(ui_tests
    main.cpp
    test_ct_geometry.cpp
    test_ct_widget_trace.cpp
    test_ct_widget_base.cpp)

target_include_directories(ui_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../event)   # reuse event test harness

target_link_libraries(ui_tests PRIVATE crisp::ui crisp::event)
add_test(NAME ui_tests COMMAND ui_tests)
```

---

## 8. 裁决点汇总（Decision Log）

| 编号 | 裁决 | 结论（权威） | 依据/落点 |
|------|------|-------------|----------|
| **D1** | CtWidgetBase 与 CtReceiver 的关系 | **继承** `crisp::event::CtReceiver`（非组合） | §3.9；稳定 objectId + 线程亲和 + `bindHandler<T>` 入口；单 `enable_shared_from_this` 基类约束（§3.1） |
| **D2** | 脏区表示 | **单矩形包围盒并集近似**（`CtRegion`，`markDamage` = `bounds.united(rect)`） | §2.2；热路径零堆分配 + 一帧一次 flush；UI2 可演进为有界多矩形列表**不改公共 API 形状**（`dirtyRegion()` 仍返单个 `CtRect`） |
| **D3** | 控件轨迹环 | **互斥锁环**（`CtWidgetTrace`，仿 `CtTaskTrace`，容量 4096） | §2.7；UI 线程单写、诊断/测试跨线程读——SPSC 环会造成跨线程读 UB；轨迹写不在零分配热路径上 |
| **D4** | WM_TIMER 二选一 | **UI1 不涉及**（控件级定时归 UI3+ 随输入映射器定稿） | §1.3 残留意见 #3；UI3 定稿 |
| **D5** | CtTimerEventSource 模板参数（控件级定时） | **UI1 不规划**；注明 EventSet 模板注意点 | §2.3 附注 + §1.3 残留意见 #4 |
| **D6** | 构造工厂用 `new` 而非 `make_shared` | **`new` + 立即 shared_ptr 接管**（受保护构造器，MinGW 8.1 libstdc++ 分配器无法合法调用） | §3.2；审计 WARN 已接受（裸指针不逃逸、非热路径） |
| **D7** | 析构摘除身份比对 | **`owner_before` 双向比对**（非 `lock().get()==this`，非 `!expired()`） | §4.5；析构期强计数归零仍可正确摘除，防 zombie 条目 |
| **D8** | setParent 同父重排 | **幂等 no-op**（事务性：先 attach 后 remove；同父直接返回） | §3.3；防重复入队后 remove_all 误删 |
| **D9** | kDestroyed 轨迹时机 | **析构先记 `kDestroyed`（保持父关联）再摘除** | §3.7/§4.5；成对保证同 `parentObjectId`（§2.7 裁决）；每控件环在析构后不可读（文档化限制） |

> 全部决策已在代码注释中以 `DECISION Dn` 标记并实现（`ct_geometry.h` D2、`ct_widget_trace.h` D3、`ct_widget_base.h` D1/D6–D9）。

---

## 9. 与 03-rewrite-proposal.md 的偏差声明

| 03 原方案 | UI1 定稿偏差 | 原因 |
|-----------|-------------|------|
| §4.1.4/§4.1.6 多矩形脏区 | **首版单矩形并集**（D2） | 热路径零分配 + 简单合批；UI2 演进不改变公共 API |
| §4.1 创建入口 `createWidget<T>()` | 增加 `create(parent)`（顶层/基类工厂） | 顶层控件无 `T` 可推导；工厂统一 `m_selfWeak` 设置 |
| §3 控件持有 `shared_ptr` 树 | 树内**仅弱引用**（父弱回引 + 子弱条目） | 无强环、RAII 析构自动解注册；外部持强引用 |
| §4.1.5 语义事件载荷（UI4） | UI1 不定义语义载荷（CtClickPayload 等归 UI4） | §1.2 范围表；typed handler 挂接随 UI4 |

> 其余（输入载荷下放核心层、CtWidgetPhase 独立枚举、trace 环、CMake 结构）与 03 一致；03 §4.1 的"对象树/几何/焦点/重绘调度"边界判断保持不变。

---

## 10. 依赖与包含关系

- `src/ui/` 依赖：`event/ct_event.h`（CtEvent/CtEventOrigin）、`event/ct_input.h` + `ct_mouse_payload.h`/`ct_key_payload.h`（crisp::input 载荷）、`event/ct_receiver.h`（基类）、`event/ct_thread_affinity.h`（CtThreadId/亲和）、自身 `ct_geometry.h`/`ct_widget_state.h`/`ct_widget_trace.h`。
- **不依赖**：Qt、Win32/GDI、`crisp::thread`（UI1 无并行需求；`CtFuture` 回投在 UI4 控件重活场景才用）。
- 构建：`crisp_ui` 静态库链接 `crisp::event`（PUBLIC）；测试链接 `crisp::ui crisp::event` + 事件 `test_harness.h`（include 路径 `../event`）。
- 反向：UI3/UI4 消费 `crisp::ui`；`crisp::event` 不反向依赖 UI。

---

## 11. 生命周期与可观测性细节

- **kCreated**：构造（顶层或带父）时记录，含 `parentObjectId`。
- **kDestroyed**：析构体首行记录（**摘除前**，保持父关联），随后 `detachFromParent()`。
- **kDamage**：`markDamage` 每次调用记录，`damageRectCount` = 距上次 `clearDamage` 的累计调用数（`clearDamage` 复位）。
- **环不可读性（文档化限制）**：`kDestroyed` 写入本控件环，析构完成后环不可达 → 其内容析构后不可读；成对保证由"先记后摘"的记录顺序维持（§2.7 裁决）。
- **跨线程违规可观测性**：UI1 靠 §4.2 断言（debug abort / release no-op）暴露；无额外轨迹记录（§4.3）。
- **UI3 关联**：输入映射器按 `CtWidgetRecord.when`（steady_clock）与事件侧 `CtTrace` 交叉关联（对齐 threading `CtTaskTrace` 时间关联约定）。

---

## 12. 边界情况与契约细化

| 边界情况 | 契约 |
|----------|------|
| `setParent(nullptr)`（顶层分离） | 清 `m_parent` + 从旧父移除；弱引用安全 |
| `setParent(自身)` / 环（新父为后代） | 拒绝（沿弱链上溯遇 `this`） |
| `setParent(同父)` | 幂等 no-op（D8） |
| 析构时父已析构 | `parent().lock()` 空 → 摘除为空操作，子独立存活 |
| 析构时 `m_selfWeak` 过期 | `owner_before` 仍有效（持有控制块）→ 正常摘除（D7） |
| 空/负尺寸几何 | `setGeometry` 归一化 width/height 为 0；`hitTest` 恒 false |
| `markDamage(空矩形)` | `CtRegion::add` no-op（不腐蚀 bounds 规范形） |
| 满环 overflow | 丢最旧、返回 false、size 恒为容量（D3） |
| 禁用态输入 | `event.ignore()`，不调 handler（§3.10） |
| 子类构造器触碰树 | 契约违规（`m_selfWeak` 未设 → debug 断言） |

---

## 13. 开放问题（留给架构审查）

| # | 问题 | 状态（UI1 定稿） |
|---|------|-----------------|
| O1 | UI2 脏区演进（有界多矩形列表 vs 单矩形） | **留 UI2**：UI1 单矩形（D2），演进不改变 `dirtyRegion()` API 形状 |
| O2 | 输入载荷 timestamp 字段落地 | **随 UI3 输入映射器**：UI1 契约先定"字段存在性"（§3.10.3 裁决），字段实现归 UI3 |
| O3 | typed handler 注册面（`bindHandler` 挂接） | **随 UI4**：UI1 `dispatchInput` 为门禁载体，不产生记录 |
| O4 | 控件级定时（闪烁光标/长按重复） | **随 UI3+ 映射器**（D4/D5） |
| O5 | 多线程读环的性能（互斥锁 vs 无锁） | UI1 互斥锁（D3）可接受；如需高频诊断再评估 |

---

## 14. 风险与约束

| 风险 | 约束/对策 |
|------|----------|
| 双 `enable_shared_from_this` 基类 → `bad_weak_ptr` 静默破坏 bindHandler | §3.1 硬约束：仅继承 CtReceiver；树用 `m_selfWeak` |
| `make_shared` 无法调受保护构造器（MinGW 8.1） | `create` 用 `new` + 立即接管（D6）；`createWidget` 用 `make_shared`（子类构造器公开） |
| 析构期强计数归零导致 `lock()` 失效 | `owner_before` 身份比对（D7）；守卫测"控制块存在"非 `expired()` |
| `emplace_back` 抛 `bad_alloc` 破坏树 | 事务性 setParent：先 attach（强异常保证）后 remove（§3.3） |
| 跨线程损伤脏区/状态静默腐蚀 | §4.2 守门：debug abort / release no-op |
| 轨迹环满丢旧（可观测性损失） | 容量 4096 足够（对齐 CtTaskTrace）；overflow 返回 false 可观测 |
| UI1 无 typed handler（功能空洞感） | 明确为 UI4 前置：门禁 + 契约 + 测试锁定，UI4 接线 |

---

## 15. 实施顺序（Implementation Order）

1. **§2 类型落地**：`ct_geometry.h/.cpp`（CtRect/CtRegion）→ `ct_widget_state.h`（CtWidgetState/CtFocusPolicy）→ `ct_widget_trace.h/.cpp`（CtWidgetPhase/CtWidgetRecord/CtWidgetTrace）。
2. **§3 基座落地**：`ct_widget_base.h/.cpp`（工厂/树/几何/状态/输入/脏区/轨迹）。
3. **测试**：`tests/ui/`（几何 → 轨迹 → 基座），零分配计数（§6.3）。
4. **CMake**：`src/ui/CMakeLists.txt` + 顶层 + `tests/ui/CMakeLists.txt`。
5. **门禁**：代码审计子agent（[MUST]/[WARN]/[SUGGEST]）→ 修复 → 代码审查 → ctest 全绿。
6. **验收**：§16 清单逐项确认 → `DEPLOYED_OK`。

> 本里程碑实际按 1→4 顺序编码（commit `c3f0b2f` 输入载荷前置 + `6fca9a9` UI1 scaffold），第 5/6 步（审计/审查/验收）为**当前待办**。

---

## 16. 验收清单（UI1 完成定义）

- [x] §2 类型与 §3 接口与 `src/ui/` 实现完全一致（§2/§3 权威规范 vs 代码回写核对）
- [x] 全部 §6.2 用例通过（`tests/ui/` 全绿）
- [x] `widget_mark_damage_hot_path_allocates_zero` 通过（§5.2 零堆分配验收）
- [x] ctest 全量 7/7 通过（event / thread / ui / qt_bridge / dual_run / dual_compare）
- [x] 代码审计子agent **无 [MUST] 残留**，判定 **CONDITIONAL**：2 个 [WARN]（D6 `create()` 用 `new` 已裁决豁免；非 ASCII 注释字符属项目既有惯例，豁免）均已注明豁免理由；4 个 [SUGGEST] 已全部修复（`ct_widget_base.h` 头注释更新、冗余 `private:` 去除、`ct_geometry.h` 未用 `<cstdint>` 去除、`tests/ui/main.cpp` 补 `<cstdio>`）
- [x] 代码审查子agent **CONDITIONAL → RETURNED_TO_CODER**，无 [MUST]：2 个 [WARN]（B1 溢出前置条件、D1 零分配测试边界）+ 3 个 [SUGGEST]（A1 未用 include、D4 顺序断言强化、D3 跨线程降级）已处理：
  - A1：`ct_widget_trace.h` 删除未用 `#include "event/ct_thread_affinity.h"`
  - B1：`ct_geometry.h` `intersect`/`united` 补 int 溢出前置条件注释
  - D1：`widget_mark_damage_hot_path_allocates_zero` 补 `dirtyRegion()`/`clearDamage()` 复检 + 计数边界注释
  - D4：`widget_trace_records_and_copy_out_in_order` 用不同 objectId 强化顺序断言
  - D3：跨线程降级（§4.2 release no-op）**无法在单线程测试二进制内注入**（affinity 构造时固定、无公开 re-bind 面）→ 记录为 **UI3 输入映射器的回归项**（§13 O2 关联），不塞假测试
- [x] 本文档 §1–§17 定稿为权威规范（无空节，725 行）

---

## 17. 一句话结论

**UI1 交付 `CtWidgetBase` 核心基座：shared_ptr 弱引用对象树 + 显式状态 + typed 输入门禁 + 单矩形脏区 + 控件轨迹环，纯 C++17 零 Qt 依赖，已实现并通过基线测试，待审计/审查门禁后 `DEPLOYED_OK`。**

---

READY_FOR_REVIEW
