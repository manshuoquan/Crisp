# CRISP UI1 核心基座架构设计（crisp::ui）

> 状态：**草稿（骨架）**——本文档正在由 Architecture Designer 用 edit 补全为权威规范（§1–§7 已就位，§8–§17 补全中）。
> 本文为 UI1 里程碑的唯一权威接口规范，编码 agent 无需猜测即可实现；**不包含任何 src/ 实现代码**。

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

### 3.1 类声明（完整、可直接照抄）

### 3.2 工厂：createWidget<T> / create

### 3.3 树操作：parent / children / setParent / childAt

### 3.4 几何：geometry / setGeometry / size / position

### 3.5 状态：setVisible / setEnabled / isVisible / isEnabled / focusable / hasFocus

### 3.6 输入：dispatchInput / hitTest

### 3.7 脏区：markDamage / dirtyRegion / clearDamage

### 3.8 可观测：record / copyOut

### 3.9 与 CtReceiver 的关系（裁决：继承）

### 3.10 字段消费契约（输入载荷消费/忽略明细）

---

## 4. 线程与所有权契约

### 4.1 UI 线程 = 主线程（单线程模型）

### 4.2 assertSameThread 守门

### 4.3 跨线程一律 postTo

### 4.4 所有权：shared_ptr 树 + weak_ptr 子引用 + RAII 析构

### 4.5 析构顺序与弱引用防悬垂

---

## 5. 关键路径零堆分配

### 5.1 零分配路径清单

### 5.2 验收方法（operator new 计数）

---

## 6. 测试计划（UI1 验收）

### 6.1 测试文件布局

### 6.2 测试用例清单（对应 §5 里程碑行）

### 6.3 零分配计数测试细节

---

## 7. CMake 集成

### 7.1 src/ui/CMakeLists.txt

### 7.2 顶层 CMakeLists.txt 修改

### 7.3 tests/ui/CMakeLists.txt

---

## 8. 裁决点汇总（Decision Log）

---

## 9. 与 03-rewrite-proposal.md 的偏差声明

---

## 10. 依赖与包含关系

---

## 11. 生命周期与可观测性细节

---

## 12. 边界情况与契约细化

---

## 13. 开放问题（留给架构审查）

---

## 14. 风险与约束

---

## 15. 实施顺序（Implementation Order）

---

## 16. 验收清单（UI1 完成定义）

---

## 17. 一句话结论

---

READY_FOR_REVIEW
