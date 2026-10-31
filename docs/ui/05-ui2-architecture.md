# CRISP UI2 — 绘制基元里程碑架构文档

> 状态：**权威规范（完整）**——§1–§17 全部定稿；§3 类型声明与 §4 接口为 UI2 唯一权威接口规范，编码 agent 无需猜测即可实现；**本文不包含任何 src/ 实现代码**。
> 对应里程碑：UI2（绘制基元），对齐 `docs/ui/03-rewrite-proposal.md` §4.2 与 §5 UI2 行；前置依赖 UI1（`docs/ui/04-ui1-architecture.md`，commit 45f63cc/6e4b7d4，DEPLOYED_OK）。
> 结构模板：`docs/ui/04-ui1-architecture.md` §1–§17；接口锚点：03 §4.2.1 / §4.2.2 / §4.2.3 类声明（本文为权威精化，偏差见 §11）。
> 平台层例外条款（`docs/ui/README.md`）：核心层 `crisp::ui` 纯 C++17 零 Qt/Win32 依赖；`CtPaintBackendGdi` 属平台层（`src/ui/platform/`），允许 GDI。

---

## 1. 范围与目标（Scope & Goals）

### 1.1 做（UI2 交付）

UI2 交付以下能力，构成 UI 子系统的"绘制基元"（对齐 `03-rewrite-proposal.md` §4.2 与 §5 里程碑行）：

1. **CtPainter 状态机 API**：`begin/end` 成对、`save/restore` 状态栈、`translate/clip`、`fillRect/drawText`；**记录式批缓冲**（命令值语义、定容量），`end()` 一次性提交给后端（P4/P10，热路径零堆分配）。
2. **CtPaintBackend 纯接口 + 双后端**：`CtPaintBackendPixelBuffer`（核心层纯 C++17，离线逐像素可测）+ `CtPaintBackendGdi`（平台层，GDI `FillRect`/`DrawTextW`/`BitBlt`，README 例外条款）。
3. **CtBackingStore 双缓冲 + 脏区合批**：每窗口一张完整像素缓冲（创建一次分配、整生命周期复用）+ 只绘脏区并集 + BitBlt 局部 present；`lastFlushBatchCount()` 验收钩子。
4. **新值类型**：`CtColor`、`CtFont`（首版最小集）、`CtPoint`/`CtSize`、字体度量能力接口（后端可选 `fontMetrics(u16string_view)`）。
5. **可观测性**：`CtWidgetPhase::kPainted` 相位在 UI2 首次产生（UI1 已定义未产生，见 04 §2.3）；绘制/合批 trace 记录。

### 1.2 不做（明确归后续里程碑）

| 能力 | 归属 | 依据 |
|------|------|------|
| 富文本/换行/复杂排版 | **明确不做**（UI5+ 亦不做） | P10：`CtLabel` 首版纯文本，字体度量走后端能力接口 |
| 样式委托（CtStyle/CtBasicStyle） | **UI5** | 04 §1.2、03 §4.5 |
| 布局（CtBoxLayout 两遍算法） | **UI4** | 04 §1.2、03 §4.4 |
| 平台窗口双模式（CtPlatformWindow）与输入映射器 | **UI3** | 03 §4.3 |
| 基础控件（CtLabel/CtPushButton/CtLineEdit）交互状态机 | **UI4** | 04 §1.2 |
| 控件级定时（闪烁光标/长按重复） | **UI3+ 随映射器** | 04 §8 裁决 D4/D5 |
| 多矩形脏区列表（有界预分配） | **UI2 演进点**（开放问题 O1） | 04 §2.2 D2：演进不改变公共 API 形状 |

> UI2 内**新增产生** `kPainted` 记录；沿用 UI1 的 `kCreated/kDestroyed/kDamage`（不重复定义）。UI2 不产生 `kFocusIn/kFocusOut`（UI4）。

### 1.3 与 UI1 的衔接（前置输入）

- **脏区来源**：`CtWidgetBase::markDamage(rect)`（widget-local）→ UI3 根分发器坐标转换 → `CtBackingStore::addDamage(region, affected)` 聚合（窗口坐标单矩形并集 + 受影响控件集合，见 §10 D2/§4.3）。
- **几何类型**：`CtRect`（半开区间 `[x, right())`）、`CtRegion`（单矩形并集近似，`add` 对空矩形 no-op）直接复用，**不新建**。
- **线程守门**：绘制必须 UI 线程，沿用 UI1 §4.2（`assertSameThread`：debug abort / release no-op）。
- **轨迹**：`CtWidgetTrace` 环（D3）继续承载 `kPainted` 记录；绘制合批计数经 `lastFlushBatchCount()` 暴露。

---

## 2. 非目标（Non-Goals）

- **不做双缓冲"翻页式"交换**：首版**单缓冲复用**（每窗口一张，`CtBackingStore` 持有，present 只 BitBlt 脏区）——MinGW 8.1 32 位内存受限（03 §7 风险表）。
- **不做绘制指令的跨线程投递**：绘制只能在 UI 线程（§5）；worker 线程不产生绘制指令（UI4 重活经 MT 桥回投 UI 线程）。
- **不做后端自动选择/注册表**：`CtPainter::begin(backend)` 显式指定后端；UI2 无运行期发现。
- **不做文本排版引擎**：无换行/对齐/富文本；`drawText` 单行原样绘制（字体度量决定原点偏移，见 §7 风险）。
- **不做像素格式转换**：PixelBuffer 固定 RGBA8888（`CtPixelFormat::kRgba8888`）；GDI 后端内部转换（平台层职责）。
- **不做 GDI 后端的完整测试**：平台层冒烟（BitBlt 区域与脏区一致）；正确性断言全在 PixelBuffer 层（离线渲染）。

## 3. 新类型（New Types）

> 本文件 §3 的全部类型声明为 **UI2 权威规范**：编码 agent 直接照抄到 `src/ui/ct_paint_types.h`（CtColor/CtPoint/CtSize/CtPixelFormat/CtFont）、`src/ui/ct_paint_cmd.h`（CtPaintCmd/CtPaintCmdType）。头文件命名与 §9 CMake 源列表一致。

### 3.1 crisp::ui::CtColor

```cpp
#pragma once

// ct_paint_types.h — UI2 paint value types (crisp::ui).
// Value semantics; hot-path zero heap allocation everywhere.
// Authoritative spec: docs/ui/05-ui2-architecture.md §3.

#include <cstdint>

namespace crisp::ui {

/// RGBA8888 color (UI2 core; PixelBuffer format kRgba8888).
/// Value semantics; no allocation. Premultiplied-alpha NOT used in UI2
/// (only solid fills — no blending in UI2 scope).
struct CtColor {
    std::uint8_t r = 0;   ///< red 0..255
    std::uint8_t g = 0;   ///< green 0..255
    std::uint8_t b = 0;   ///< blue 0..255
    std::uint8_t a = 255; ///< alpha 0..255 (255 = opaque)

    /// True if the color is fully opaque.
    bool isOpaque() const noexcept { return a == 255; }

    /// Named colors (UI2 minimal set; UI5 style system may extend).
    static CtColor black() noexcept { return CtColor{0, 0, 0, 255}; }
    static CtColor white() noexcept { return CtColor{255, 255, 255, 255}; }
};

/// 2D integer point (pixel coordinates).
struct CtPoint {
    int x = 0;
    int y = 0;
};

/// 2D integer size (width/height >= 0; non-negative by convention).
struct CtSize {
    int width = 0;
    int height = 0;

    /// True if width <= 0 || height <= 0.
    bool isEmpty() const noexcept { return width <= 0 || height <= 0; }
};

/// Pixel buffer format. UI2 core supports exactly ONE format.
enum class CtPixelFormat : std::uint8_t {
    kRgba8888 = 0, ///< 4 bytes/pixel, byte order R,G,B,A (UI2 only format)
};

} // namespace crisp::ui
```

**契约**：全部值语义、零堆分配；`CtColor` 不引入 alpha 混合（UI2 只有实心填充，无混合；`isOpaque()` 供后端快路径判断）。

### 3.2 crisp::ui::CtFont（首版最小集）

```cpp
/// Font identity (UI2 minimal set). NO metrics here — metrics live on the
/// backend's optional fontMetrics() capability (D7). Font is a value
/// describing family + size; the backend resolves it.
struct CtFont {
    std::uint32_t pixelSize = 14; ///< pixel height (positive)
    /// Family name (UTF-16; empty = backend default). Value semantics;
    /// NOT on the hot path (drawText carries it, resolved by backend).
    std::u16string family;
};

/// Equality: fonts are equal when family + pixelSize match.
bool operator==(const CtFont& a, const CtFont& b) noexcept;
```

**契约**：`CtFont` 是**身份值**（family + pixelSize），不含度量；度量由后端 `fontMetrics()` 能力提供（§4.2）。UI2 首版固定 `pixelSize`，不做 point/device 换算。**热路径纪律（MUST-1）**：`CtFont` 的 `u16string family` 允许构造分配（非记录路径）；记录命令只存 `fontPixelSize` 值 + `family` 的 `u16string_view` 引用（§3.3），**drawText 记录命令不拷贝 family**。

### 3.3 crisp::ui::CtPaintCmd（记录式批缓冲命令）

```cpp
/// One recorded paint command (UI2 record-style command buffer, D1).
/// Discriminated union by CtPaintCmdType; value semantics, POD-sized
/// (u16string_view refs into caller-owned text — NO copy, NO allocation).
/// Authoritative spec: docs/ui/05-ui2-architecture.md §3.3 / §10 D1.
enum class CtPaintCmdType : std::uint8_t {
    kFillRect = 0, ///< fillRect(rect, color)
    kDrawText = 1, ///< drawText(origin, text, font, color)
};

struct CtPaintCmd {
    CtPaintCmdType type = CtPaintCmdType::kFillRect;
    CtRect rect;              ///< kFillRect: target rect (device coords)
    CtPoint origin;           ///< kDrawText: baseline origin
    CtColor color;            ///< both: fill / text color
    // kDrawText only: font identity WITHOUT owning the family string.
    // pixelSize is a value; family is a u16string_view REF into
    // caller-owned storage (same lifetime discipline as `text`) — the
    // command must NOT copy the family (zero-allocation hot path, MUST-1
    // ruling: see docs/ui/05-ui2-architecture.md §3.3 / §10 D1).
    std::uint32_t fontPixelSize = 14;
    std::u16string_view family; ///< refs caller-owned storage (may be empty)
    std::u16string_view text;   ///< kDrawText only; refs caller-owned storage
};
```

**契约**：
- **记录式**：绘制调用只 append 一条 `CtPaintCmd` 到定容量命令缓冲（`kMaxPaintCmds = 8192`，仿 `kMaxQueued` 纪律），**零堆分配**（`u16string_view` 引用调用方文本与 family，不拷贝）。
- **font 表示（MUST-1 定案，方案 a）**：命令内 font = `fontPixelSize`（值）+ `family`（`u16string_view` 引用，生命周期与 `text` 相同，均指向调用方在 `drawText` 调用期间存活的对象）。后端执行时经 `CtFont{pixelSize, family}` 构造完整 font 解析。**drawText 记录命令零堆分配**（§8.2 补 `painter_draw_text_hot_path_allocates_zero` 用例）。
- **坐标**：命令坐标在 `begin` 时已由 CtPainter 应用 translate/clip 变换（记录**设备坐标**，见 §4.1 决策 D1/D3）——命令本身不含变换栈。
- 缓冲满 → `end()` 前溢出：UI2 定案为**拒绝后续命令并置 `kOverflow` 标志**（`end()` 仍 flush 已记录命令；标志可观测，见 §14）。

### 3.4 crisp::ui::CtPaintMetrics（字体度量能力结果）

```cpp
/// Text metrics result from the backend's optional fontMetrics() capability
/// (D7). Fixed values in the PixelBuffer backend (testable off-screen);
/// GDI backend uses GetTextExtentPoint32W.
struct CtPaintMetrics {
    int width = 0;  ///< advance width (px)
    int height = 0; ///< line height (px)
};
```

**契约**：度量是**后端能力**（非 `CtFont` 自带）；PixelBuffer 后端**钉死固定值**：`charWidth=8`、`lineHeight=16`、`width = text.size() × 8`（WARN-F/SUGGEST-1 定案，供测试断言），GDI 后端真实度量（`GetTextExtentPoint32W`）。

## 4. 完整接口（Complete Interfaces）

> 本节为 UI2 权威接口规范：与 `src/ui/ct_painter.h`、`src/ui/ct_paint_backend.h`、`src/ui/ct_backing_store.h` 对应。§4.1/§4.2/§4.3 类声明基于 03 §4.2.1/4.2.2/4.2.3 锚点精化（偏差见 §11）。

### 4.1 CtPainter 状态机 API

```cpp
#pragma once

// ct_painter.h — UI2 painter state machine (crisp::ui).
// Record-style command buffer (D1); begin/end pair; save/restore stack.
// Authoritative spec: docs/ui/05-ui2-architecture.md §4.1 / §10.

#include <cstdint>

#include "ct_paint_cmd.h"      // CtPaintCmd / CtPaintCmdType / kMaxPaintCmds
#include "ct_paint_types.h"    // CtColor / CtFont / CtPoint / CtRect
#include "ct_paint_backend.h"  // CtPaintBackend

namespace crisp::ui {

/// Painter state machine. begin() targets a CtPaintBackend; end() flushes.
/// Explicit begin/end (Qt's begin-on-demand is a silent-footgun source).
class CtPainter {
public:
    /// Optional trace sink (WARN-A ruling): when non-null, save/restore
    /// stack-depth changes are recorded as CtPaintState records on the
    /// given widget trace; when null, only stackDepth() is observable.
    /// The sink is NOT owned by the painter.
    explicit CtPainter(CtWidgetTrace* sink = nullptr) noexcept : m_sink(sink) {}

    /// Begin painting on `backend`. Returns false if the backend refused
    /// (wrong thread / already active / zero-size surface). A session
    /// must be ended with end() before begin() can be called again.
    bool begin(CtPaintBackend& backend) noexcept;

    /// End painting: flush batched primitives to the backend. Idempotent
    /// (safe to call when not active). Clears the command buffer and
    /// resets the overflow flag (WARN-5 ruling).
    void end() noexcept;

    /// Whether a painting session is active (begin returned true, end not
    /// yet called).
    bool isActive() const noexcept;

    /// Push a copy of the current transform+clip state onto the stack.
    /// Full stack (kMaxStackDepth) -> debug assert / release no-op + sets
    /// overflowed() (WARN-1 ruling, same discipline as D9).
    void save();
    /// Pop the top state; no-op when the stack is empty (documented).
    void restore();

    /// The current stack depth (trace hook for save/restore balance).
    std::size_t stackDepth() const noexcept;

    /// Translate subsequent drawing by (dx, dy) (device coords).
    void translate(int dx, int dy) noexcept;

    /// Clip subsequent drawing to `rect` (intersected with current clip,
    /// device coords). Empty rect => nothing draws.
    void clip(const CtRect& rect) noexcept;

    /// Fill `rect` with a solid color (the only UI2 fill form).
    /// Appends a kFillRect command (translated/clipped).
    void fillRect(const CtRect& rect, const CtColor& color) noexcept;

    /// Draw `text` at `origin` (baseline origin, device coords) using the
    /// font's metrics backend. Appends a kDrawText command (zero-alloc:
    /// font family + text are stored as u16string_view refs, MUST-1).
    void drawText(const CtPoint& origin, const std::u16string_view& text,
                  const CtFont& font, const CtColor& color) noexcept;

    /// Number of commands recorded this session (test hook).
    std::size_t commandCount() const noexcept;

    /// True if the command buffer overflowed (kMaxPaintCmds exceeded) or
    /// the save stack overflowed (kMaxStackDepth). Reset at end().
    bool overflowed() const noexcept;

private:
    CtPaintBackend* m_backend = nullptr;      ///< active backend (null when idle)
    CtPaintCmd m_cmds[kMaxPaintCmds];         ///< record-style buffer
    std::size_t m_cmdCount = 0;               ///< valid commands
    bool m_overflowed = false;                ///< overflow flag (reset at end)
    // transform+clip stack (bounded, value semantics)
    // stack entries hold current translate + clip; kMaxStackDepth = 32
    // (WARN-1/WARN-D). Declared below (fixed-capacity member array).
    CtWidgetTrace* m_sink = nullptr; ///< optional trace sink (not owned, WARN-A)
    static constexpr std::size_t kMaxStackDepth = 32;
    // m_stack[kMaxStackDepth] / m_stackDepth defined in ct_painter.cpp.
    // NOTE (WARN-2): CtPainter is INTENDED to live on the heap or as a
    // per-window member — m_cmds[kMaxPaintCmds] is ~0.5MB; do NOT stack-
    // allocate in deep call chains (MinGW 8.1 1MB default stack).
};


} // namespace crisp::ui
```

**关键契约**：
- **`begin/end` 成对**；`end()` 幂等；未 `begin` 的绘制调用是 no-op（记录在 §14）。
- **`save/restore` 成对**；栈深记入 trace（§7.2）；`restore` 空栈 no-op（文档化）；save 满栈（`kMaxStackDepth=32`）debug assert / release no-op + 置 overflowed（WARN-1）。
- **记录式批缓冲**：绘制调用 append `CtPaintCmd`（值语义零堆分配，`u16string_view` 不拷贝）；`end()` 一次性把命令交给后端 `flush(damage)`。
- **变换/裁剪在记录时应用**（D3）：`translate` 修改当前变换，`clip` 与当前裁剪求交；`fillRect/drawText` 记录**设备坐标**命令（已平移/已裁剪）。
- **溢出**：命令缓冲满（`kMaxPaintCmds = 8192`）→ 置 `m_overflowed`，后续命令拒绝；`end()` 仍 flush 已记录命令；**`overflowed()` 在 `end()` 复位**（WARN-5）；`save` 栈满同样置位（WARN-1）。
- **线程守门（WARN-6/WARN-E）**：CtPainter 自身**不持有** `CtThreadAffinity`——守门经后端**传递**：`begin()` 首调 `backend.size()`（后端 `size()` 入口断言亲和，§4.2 锚点），非 UI 线程 → `size()` 断言失败（debug abort / release 返回空尺寸）→ `begin()` 返回 false → inactive → 后续绘制调用 no-op（§5.1/§14）。

### 4.2 CtPaintBackend 抽象（GDI + PixelBuffer）

```cpp
#pragma once

// ct_paint_backend.h — UI2 paint backend interface (crisp::ui).
// Core-layer PixelBuffer backend is pure C++17 (testable off-screen);
// GDI backend (platform layer) targets a window's backing store.
// Authoritative spec: docs/ui/05-ui2-architecture.md §4.2.

#include "ct_paint_types.h"  // CtSize / CtColor / CtFont / CtPaintMetrics
#include "ct_geometry.h"     // CtRect / CtRegion

namespace crisp::ui {

/// Paint backend: the surface a CtPainter draws onto.
class CtPaintBackend {
public:
    virtual ~CtPaintBackend() = default;

    /// Surface size in pixels; zero size refuses begin(). Thread guard
    /// anchor (WARN-E): asserts the calling thread owns the backend
    /// (debug abort / release returns empty size — the painter's begin()
    /// then fails and the session degrades to no-op).
    virtual CtSize size() const noexcept = 0;

    /// Commit the batched primitives of one paint pass (called by
    /// CtPainter::end). `damage` = the dirty region to repaint (single
    /// rect, D2). Must be thread-affined to the UI thread.
    virtual void flush(const CtRegion& damage) = 0;

    /// Blit the backing store region to the window (double buffer).
    /// `rect` = the flushed dirty rect (present only that).
    /// MUST-C ruling: PixelBuffer backend present = NO-OP + region sanity
    /// check (the buffer IS the store — no window); GDI backend BitBlt's
    /// the dirty rect from its internal memory DC to the window.
    virtual void present(const CtRect& rect) = 0;

    /// OPTIONAL text metrics capability (D7). Returns false if the backend
    /// does not support text (no drawText). PixelBuffer: fixed values;
    /// GDI: GetTextExtentPoint32W.
    virtual bool fontMetrics(const CtFont& font, const std::u16string_view& text,
                             CtPaintMetrics& out) const noexcept = 0;
};

} // namespace crisp::ui
```

**契约**：
- `flush(damage)` 只重绘脏区（D2 单矩形并集）；`present(rect)` 只 Blit 该脏区。
- **线程亲和**：`flush`/`present` 必须在 UI 线程（后端持有 `CtThreadAffinity`，违规 debug abort / release no-op——沿用 UI1 §4.2）。
- `fontMetrics` 是**能力接口**（可选返回 false）：PixelBuffer 支持（固定值），GDI 支持（真实度量）。

#### 4.2.1 CtPaintBackendPixelBuffer（核心层，可测）

```cpp
/// Core-layer pixel buffer backend: off-screen RGBA8888 buffer (the
/// buffer IS the store — present is a no-op + region check, §4.2.1 /
/// MUST-C). All drawing-correctness assertions happen HERE (offline
/// render).
/// Authoritative spec: docs/ui/05-ui2-architecture.md §4.2.1.
class CtPaintBackendPixelBuffer final : public CtPaintBackend {
public:
    explicit CtPaintBackendPixelBuffer(const CtSize& size);
    ~CtPaintBackendPixelBuffer() override = default;

    CtSize size() const noexcept override;

    /// Execute the recorded commands into the pixel buffer (the "real"
    /// drawing; damage is a hint — UI2 fills the whole surface once).
    void flush(const CtRegion& damage) override;

    /// Present: NO-OP for the PixelBuffer backend (the buffer IS the
    /// store — WARN-4 ruling). Asserts the rect is within size() (region
    /// sanity; the caller's present-consistency is verified via the
    /// store's flush bookkeeping, not a copy).
    void present(const CtRect& rect) override;

    bool fontMetrics(const CtFont& font, const std::u16string_view& text,
                     CtPaintMetrics& out) const noexcept override;

    /// Read pixel at (x,y) — test/assert hook (no bounds check; caller
    /// keeps within size()).
    CtColor pixel(int x, int y) const noexcept;

    /// The raw RGBA8888 backing array (test hook).
    const std::uint8_t* data() const noexcept;

private:
    CtSize m_size;                            ///< surface size
    std::vector<std::uint8_t> m_pixels;       ///< RGBA8888, size*4 bytes
    // (CtThreadAffinity member; thread guard in flush/present)
};
```

**契约**：`flush` 执行记录命令（fillRect 写像素、drawText 用固定度量写像素）；`present` 为 **no-op + 区域校验**（缓冲即存储，MUST-C 定案）；`pixel(x,y)` 供测试逐像素断言（§8）。

#### 4.2.2 CtPaintBackendGdi（平台层，冒烟）

```cpp
/// Platform-layer GDI backend (README exception clause): draws recorded
/// commands into a window DC via FillRect/DrawTextW, presents via BitBlt.
/// Authoritative spec: docs/ui/05-ui2-architecture.md §4.2.2.
class CtPaintBackendGdi final : public CtPaintBackend {
public:
    explicit CtPaintBackendGdi(void* hwnd);   ///< HWND (opaque void*)
    ~CtPaintBackendGdi() override = default;

    CtSize size() const noexcept override;
    /// Execute the recorded commands into an INTERNAL memory DC (WARN-4:
    /// the buffer is owned by the backend, per D4); present BitBlt's the
    /// dirty rect from that DC to the window.
    void flush(const CtRegion& damage) override;
    void present(const CtRect& rect) override;
    bool fontMetrics(const CtFont& font, const std::u16string_view& text,
                     CtPaintMetrics& out) const noexcept override;

private:
    void* m_hwnd = nullptr;   ///< window handle (platform layer)
    // internal memory DC + pixel buffer owned by the backend (D4 carrier)
    // (platform-layer members; defined in ct_paint_backend_gdi.cpp)
};
```

**契约**：平台层冒烟——`flush` 用 `FillRect`/`DrawTextW` 画脏区到窗口 DC；`present` 用 `BitBlt` 把 backing store 脏区 Blit 到窗口（区域与脏区一致断言见 §8）。

### 4.3 CtBackingStore 双缓冲 + 脏区合批

```cpp
#pragma once

// ct_backing_store.h — UI2 per-window backing store (crisp::ui).
// QBackingStore model: one full pixel buffer + damage batching + partial
// flush. Authoritative spec: docs/ui/05-ui2-architecture.md §4.3.
// DECISION D2: UI2 aggregates widget-local markDamage into ONE window
// bounding rect; lastFlushBatchCount() counts merges this frame.

#include "ct_paint_backend.h"  // CtPaintBackend
#include "ct_geometry.h"       // CtRect / CtRegion
#include <cstddef>

namespace crisp::ui {

/// Per-top-level-window backing store (QBackingStore model):
/// one full pixel buffer + damage batching + BitBlt partial flush.
class CtBackingStore {
public:
    /// One buffer, reused for the window's lifetime (created once).
    /// `backend` is the surface this store presents to (held by
    /// reference, NOT owned — MUST-A ruling; the backend owns its
    /// pixel buffer; see §5.3).
    explicit CtBackingStore(const CtSize& size, CtPaintBackend& backend);

    /// The surface backend (frame sequence entry: painter.begin(store.backend())).
    CtPaintBackend& backend() noexcept { return m_backend; }
    const CtPaintBackend& backend() const noexcept { return m_backend; }

    /// Merge damage: union of all markDamage rects this frame (single
    /// bounding rect, D2). `damage` is in window (root) coordinates.
    /// `affected` (optional): the widgets whose damage was merged THIS
    /// call (weak refs; MUST-2 ruling) — the store retains them so
    /// flush() can write kPainted + call clearDamage() on exactly the
    /// widgets that were repainted. May be empty (pure region merge).
    void addDamage(const CtRegion& damage,
                   const std::vector<std::weak_ptr<CtWidgetBase>>& affected = {});

    /// Number of damage rects merged this frame (batch test hook, §8).
    std::size_t lastFlushBatchCount() const noexcept;

    /// Present only the dirty region (BitBlt/memcpy on the backend), then
    /// clear the store's damage, reset the batch count, write kPainted on
    /// the affected widgets and call their clearDamage(). Does NOT paint
    /// — the drawing already happened in painter.end() -> backend.flush
    /// (MUST-B frame sequence, §4.3).
    void flush() noexcept;

private:
    CtPaintBackend& m_backend;    ///< the surface (held by ref, not owned)
    CtRegion m_dirty;             ///< accumulated window damage (D2)
    std::size_t m_batchCount = 0; ///< merges this frame (test hook)
    // affected widgets accumulated across addDamage calls this frame
    // (weak refs; dedup by owner identity — MUST-2 channel).
    // Fixed-capacity preallocated array (WARN-C ruling): kMaxAffected = 64;
    // overflow follows D9 discipline (flag + reject excess; §14).
    std::vector<std::weak_ptr<CtWidgetBase>> m_affected; // reserved once (ctor)
};

} // namespace crisp::ui
```

**关键契约**：
- **每窗口一张完整像素缓冲**，创建时一次分配、整生命周期复用（无 per-frame 分配）。
- **只 present 脏区并集（MUST-B）**：`flush()` 内部 = **仅 `present(damage)`**（BitBlt/memcpy）→ 清 store 侧 damage → 复位 batchCount → 对受影响控件写 kPainted + 调 clearDamage。**绘制已在 `painter.end()` → `backend.flush(damage)` 完成，`store.flush()` 不重复绘制**（帧序见下）。
- **`addDamage` 坐标**：窗口（根）坐标；widget-local 损伤在 UI3 根分发器经坐标转换后聚合（UI2 提供 `addDamage` 入口，聚合责任在 UI3 输入映射器/根窗口，见 §10 D2）。
- **`lastFlushBatchCount()`**：本帧聚合的 markDamage 次数（验收钩子：N 次 markDamage → 1 次 flush 时 batchCount == N，§8 用例 `backing_store_batches_n_damage_into_one_flush`）。
- **kPainted 通道（MUST-2）**：`addDamage` 携带受影响控件弱引用集合（按 owner 身份去重，`owner_before` 双向比对——UI1 §4.5 先例），`flush()` 后对每个受影响控件写 `kPainted` 并调用其 `clearDamage()`——实现"kDamage → kPainted 配对"（§7.1）。
- **构造/后端访问（MUST-A）**：`CtBackingStore(size, backend&)` 持后端**引用**（非拥有，后端拥有像素缓冲）；`backend()` 访问器供帧序 `painter.begin(store.backend())`。

**一帧调用序列（MUST-3，权威帧序）**：

```cpp
// UI 线程，每帧一次：
// 1) 控件损伤：CtWidgetBase::markDamage(rect)（widget-local，04 §3.7）
//    → UI3 根分发器坐标转换 → store.addDamage(region, {affectedWidgets})
// 2) 绘制：painter.begin(store.backend())
//          → fillRect/drawText（记录命令，零堆分配）
//          → painter.end()（触发 backend.flush(damage) 执行命令）
// 3) 呈现：store.flush() —— 仅 present 脏区（BitBlt/memcpy）+ 清 store 侧
//    damage/复位 batchCount + 对受影响控件写 kPainted + 调 clearDamage()
```

> `painter.end()` 把命令一次性交给 `backend.flush(damage)`（§4.1）；`store.flush()` **不重复绘制**——只 present 并做 kPainted/clearDamage 收尾（MUST-B 定案）。此序列写入 §8.2 用例 `backing_store_batches_n_damage_into_one_flush` 的断言时序。

## 5. 线程与所有权（Threading & Ownership）

### 5.1 绘制 UI 线程守门（沿用 UI1 §4.2）

- 所有绘制操作（`CtPainter::begin/end/fillRect/drawText`、`CtPaintBackend::flush/present`、`CtBackingStore::addDamage/flush`）必须在 **UI 线程**执行。
- 守门方式与 UI1 一致：`assertSameThread()`——debug abort / release 降级 no-op（§14 边界）。
- worker 线程**绝不产生绘制指令**；UI4 重活经 MT 桥（`CtTaskEventBridge`）回投 UI 线程后再绘制。

### 5.2 CtPaintBackend 线程亲和

- `CtPaintBackend` 持有 `CtThreadAffinity`（构造时绑定创建线程）；`flush/present/fontMetrics` 入口断言。
- `CtPaintBackendPixelBuffer` 与 `CtPaintBackendGdi` 同规则（后端 = 表面，必须 UI 线程访问）。

### 5.3 所有权与生命周期

- **`CtPainter`**：值语义工具对象（**可栈上构造，限浅调用帧**；深调用链必须堆分配或成员化——`m_cmds[8192]` ≈0.5MB，WARN-2/WARN-B）；持有后端**裸指针引用**（非拥有，后端生命周期由调用方保证）；命令缓冲是成员（无堆分配）；可选注入 trace sink（不拥有，WARN-A）。
- **`CtPaintBackendPixelBuffer`**：拥有像素缓冲（`std::vector<uint8_t>`，创建时分配一次）；UI2 无多后端共享。
- **`CtBackingStore`**：持有后端**引用**（`CtPaintBackend&`，非拥有）；每顶层窗口一个，生命周期与窗口绑定。
- **无裸 new/delete**：全部 RAII / 值语义；PixelBuffer 缓冲用 `std::vector`（非热路径分配，构造时一次）。

### 5.4 单缓冲复用（D4，对齐 03 §7 风险表）

- **首版单缓冲**：每窗口一张完整像素缓冲，`CtBackingStore` 持有，创建时一次分配、整生命周期复用；**不做翻页式双缓冲交换**（MinGW 8.1 32 位内存受限）。
- `present(damage)` 只 Blit 脏区（部分刷新，不整屏拷贝）——**GDI 后端 BitBlt**；PixelBuffer 后端 present 为 no-op（缓冲即存储，MUST-C）。

---

## 6. 零分配保证（Zero-Allocation Guarantees）

### 6.1 零分配路径清单

| 路径 | 是否零堆分配 | 依据 |
|------|-------------|------|
| `CtPainter::fillRect/drawText`（记录命令） | ✅ **零堆分配** | D1 记录式命令缓冲：append `CtPaintCmd`（值语义；`text` 与 `family` 均 `u16string_view` 引用，**不拷贝**——MUST-1 方案 a），定容量数组成员 |
| `CtPainter::translate/clip/save/restore` | ✅ 零堆分配 | 值语义栈（定容量） |
| `CtPainter::end()` → 后端 `flush(damage)` | ✅ 零堆分配 | 命令缓冲直接遍历执行；damage 单矩形 |
| `CtBackingStore::addDamage` | ✅ 零堆分配（构造时 reserve） | D2 单矩形并集（O(1)）；`m_affected` 定容量数组构造时一次 reserve，**先查 `size() < kMaxAffected` 再 push，超限拒绝（D9 纪律）**——绝不无界增长（WARN-C/SUGGEST-1 定案） |
| `CtBackingStore::flush()` | ✅ 零堆分配 | **仅 present 脏区** + 清 damage/复位 batchCount + kPainted/clearDamage（绘制已在 painter.end()→backend.flush 完成，MUST-B） |
| `CtPaintBackendPixelBuffer::flush`（写像素） | ✅ 零堆分配 | 直接写 RGBA 数组 |
| `CtPaintBackendPixelBuffer::present` | ✅ 零堆分配 | **no-op + 区域校验**（缓冲即存储，MUST-C 定案；无窗口可 Blit） |
| `CtPaintBackendPixelBuffer::pixel/data`（测试钩子） | ✅ 零堆分配 | 访问器 |
| 后端构造（缓冲分配） | ⬜ 允许一次 | 创建时分配，整生命周期复用（非热路径） |
| `CtFont`（含 u16string） | ⬜ 允许 | 非热路径（后端解析用）；绘制命令不拷贝 |

**限定**：零分配声明限于**绘制热路径**（记录命令 → flush → present）；`CtFont` 的 `u16string` 构造允许分配（不在记录路径上，`drawText` 收 `u16string_view` 引用）。

### 6.2 验收方法（operator new 计数）

- 仿 UI1 §5.2/§6.3：测试二进制（`tests/ui/test_ct_painter.cpp` 等）定义全局计数版 `operator new`，预热后武装。
- **热路径用例**：`painter_fill_rect_hot_path_allocates_zero`——构造 PixelBuffer 后端 + CtPainter（预热），武装计数后 `fillRect`×N + `end()` + `flush`，断言 `allocs == 0`。
- **合批用例**：`backing_store_batches_n_damage_into_one_flush`——N 次 `addDamage` → 1 次 `flush`，断言 `lastFlushBatchCount() == N` 且 `flush` 后 `batchCount` 复位（§8）。

---

## 7. 可观测性与 Trace（Observability & Tracing）

### 7.1 kPainted 相位（UI1 已定义，UI2 首次产生）

- `CtWidgetPhase::kPainted`（值 3，04 §2.3 已定义）在 **UI2 首次产生**：`CtBackingStore::flush()` 成功完成一次绘制/呈现后，对每个受损控件记录 `kPainted`。
- 记录经 `CtWidgetTrace`（D3 互斥环）写入；`CtWidgetRecord` 复用（`phase=kPainted`，`damageRectCount` 置 0——kPainted 记录无损伤计数语义）。
- **成对**：`kDamage`（markDamage）→ `kPainted`（flush 后）配对，供 UI3 输入映射器按时间戳关联（对齐 threading `CtTaskTrace` 关联约定）。
- **新增相位 `kPaintState`（值 6，WARN-A 定案）**：`CtWidgetPhase` 扩展第 7 个值——`CtPainter` 的 save/restore 栈深变化经 sink 记录用（**独立相位，不复用 `kDamage`**，避免污染 kDamage→kPainted 配对）。UI1 04 §2.3 枚举随之扩展（UI2 起生效；04 文档同步更新）。

### 7.2 绘制/合批 trace

- `CtPainter::save/restore` 栈深变化记入 trace（可观测栈平衡：`stackDepth()` 钩子 + 轨迹）。
- **trace 落点（WARN-A 定案）**：CtPainter 经**可选注入的 `CtWidgetTrace*` sink**（构造参数，默认 nullptr，不拥有）记录栈深变化。注入时：save 入栈/restore 出栈各记一条 **`kPaintState` 相位**记录（新增独立相位，扩展 `CtWidgetPhase`，见 §7.1 附注——**不复用 `kDamage`**，避免污染 kDamage→kPainted 配对）；未注入时仅 `stackDepth()` 钩子可观测。
- `CtBackingStore::lastFlushBatchCount()` 暴露本帧合批数（验收钩子）；kPainted 走既有 `CtWidgetTrace`（§7.1，经 MUST-2 的 affected 通道）。

### 7.3 溢出可观测

- 命令缓冲溢出：`CtPainter::overflowed()` 暴露标志；`end()` 仍 flush 已记录命令；`kOverflow` 情况在 §14 边界表与 §16 风险记录。

## 8. 测试计划（Test Plan）

### 8.1 测试文件布局

```
tests/ui/
├── CMakeLists.txt                # ui_tests 目标扩展（§9）
├── main.cpp                      # 注册全部用例（CtTestRegistry）
├── test_ct_geometry.cpp          # UI1 既有（CtRect/CtRegion）
├── test_ct_widget_trace.cpp      # UI1 既有（CtWidgetTrace）
├── test_ct_widget_base.cpp       # UI1 既有（CtWidgetBase）
├── test_ct_paint_types.cpp       # UI2：CtColor/CtPoint/CtSize/CtFont/CtPaintCmd
├── test_ct_painter.cpp           # UI2：状态机/记录式缓冲/save-restore/clip/零分配
├── test_ct_paint_backend_pixel.cpp # UI2：PixelBuffer 逐像素断言/字体度量
└── test_ct_backing_store.cpp     # UI2：合批/单缓冲/flush/零分配
```

- 复用事件 `test_harness.h`（手写 test double，禁止 mock 框架）；GDI 后端冒烟放平台层测试（`tests/ui/platform/`，UI2 可选）。

### 8.2 测试用例清单（对齐 03 §5 UI2 行）

| 用例 | 覆盖契约 | 状态 |
|------|---------|------|
| `paint_color_value_semantics` | CtColor 值语义/RGBA/opaque | 待实现 |
| `paint_font_identity_and_metrics_capability` | CtFont 身份 + fontMetrics 能力（PixelBuffer 固定值：charWidth=8、lineHeight=16、width=text.size()×8——SUGGEST-1 钉死） | 待实现 |
| `painter_begin_end_pairs` | begin 返回 false（后端拒绝/零尺寸/重复 begin）；end 幂等；**end() 后 overflowed() 复位**（SUGGEST-2） | 待实现 |
| `painter_save_restore_stack_balance` | save/restore 栈平衡断言（stackDepth 钩子）；空栈 restore no-op；满栈（32）overflowed 置位（WARN-1） | 待实现 |
| `painter_translate_applies_to_fill_rect` | translate 后 fillRect 记录设备坐标（D3） | 待实现 |
| `painter_clip_intersects_correctly` | clip 与当前裁剪求交；空裁剪后 fillRect 不产生可见像素 | 待实现 |
| `painter_fill_rect_records_command` | fillRect append kFillRect 命令；commandCount 增长 | 待实现 |
| `painter_draw_text_records_command` | drawText append kDrawText（u16string_view 引用不拷贝） | 待实现 |
| `painter_fill_rect_hot_path_allocates_zero` | **§6.2 零堆分配验收**（fillRect 记录 + end + flush，operator new 计数） | 待实现 |
| `painter_draw_text_hot_path_allocates_zero` | **MUST-1 验收**：drawText 记录命令（font family + text 均 u16string_view 引用）零堆分配 | 待实现 |
| `pixel_backend_fill_rect_writes_pixels` | **PixelBuffer 逐像素断言**（离线渲染正确性） | 待实现 |
| `pixel_backend_present_is_noop_asserts_rect` | present no-op + 区域校验（WARN-4） | 待实现 |
| `pixel_backend_font_metrics_fixed_values` | PixelBuffer 固定度量（charWidth=8、lineHeight=16、width=size×8） | 待实现 |
| `backing_store_batches_n_damage_into_one_flush` | **N 次 addDamage → 1 次 flush**（lastFlushBatchCount == N；flush 后复位）——按 §4.3 帧序断言（MUST-3） | 待实现 |
| `backing_store_flush_writes_kpainted_and_clears_damage` | **MUST-2 验收**：flush 后受影响控件出现 kPainted 记录、其 dirtyRegion 清空（kDamage→kPainted 配对） | 待实现 |
| `backing_store_flush_clears_damage` | flush 成功后脏区清空（并入上例） | 待实现 |
| `backing_store_hot_path_allocates_zero` | addDamage + flush 全程零堆分配 | 待实现 |
| `gdi_backend_smoke_presents_dirty_rect` | GDI 冒烟：内部内存 DC flush 画脏区，present BitBlt 区域与脏区一致（平台层可选） | 待实现 |
| `painter_begin_rejected_on_foreign_thread` | **线程守门验收**（WARN-E/SUGGEST）：非 UI 线程 begin 返回 false（经后端 size() 断言失败）→ 后续绘制 no-op（§5.1/§14） | 待实现 |
| `backing_store_flush_kpainted_pair_has_no_paint_state` | **配对纯净性**（SUGGEST）：kDamage→kPainted 配对内无 `kPaintState` 假记录（sink 栈深记录不污染配对，WARN-A） | 待实现 |

### 8.3 零分配计数测试细节

- 计数重载只在测试二进制内生效（仿 UI1 §6.3）。
- 武装时机：预热后（构造后端 + CtPainter + 首次 flush 完成，`u16string`/`vector` 等构造分配在计数前）。
- 断言：`fillRect`×N + `end` + `flush` 后 `g_allocCount == 0`；并配合 `commandCount`/`stackDepth` 验证正确性。

---

## 9. CMake 与模块结构（Build & Module Layout）

### 9.1 src/ui/CMakeLists.txt（UI2 扩展）

```cmake
add_library(crisp_ui STATIC
    ct_geometry.cpp
    ct_widget_trace.cpp
    ct_widget_base.cpp
    ct_paint_types.cpp        # UI2: CtFont operators (others inline)
    ct_painter.cpp            # UI2: state machine + command buffer
    ct_paint_backend_pixel.cpp # UI2: PixelBuffer backend
    ct_backing_store.cpp      # UI2: backing store
    # platform layer (UI2 optional, README exception clause):
    # ct_paint_backend_gdi.cpp
)

target_include_directories(crisp_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>)

target_compile_features(crisp_ui PUBLIC cxx_std_17)
target_link_libraries(crisp_ui PUBLIC crisp::event)   # CtReceiver/CtThreadAffinity
add_library(crisp::ui ALIAS crisp_ui)
```

- **核心层**：`ct_paint_types.cpp`（仅 CtFont 的 operator== 等非 inline 实现）、`ct_painter.cpp`、`ct_paint_backend_pixel.cpp`、`ct_backing_store.cpp`——**零 Qt/Win32**。
- **平台层**：`ct_paint_backend_gdi.cpp`（注释掉的源列表项，UI2 交付时可含，README 例外条款允许 GDI）。
- 头文件：`ct_paint_types.h`、`ct_paint_cmd.h`、`ct_painter.h`、`ct_paint_backend.h`、`ct_backing_store.h`。

### 9.2 顶层 CMakeLists.txt

- 无需新增子目录（`src/ui` 与 `tests/ui` 已在 UI1 接入）；UI2 仅扩展源文件列表与测试目标。

### 9.3 tests/ui/CMakeLists.txt（UI2 扩展）

```cmake
add_executable(ui_tests
    main.cpp
    test_ct_geometry.cpp
    test_ct_widget_trace.cpp
    test_ct_widget_base.cpp
    test_ct_paint_types.cpp
    test_ct_painter.cpp
    test_ct_paint_backend_pixel.cpp
    test_ct_backing_store.cpp)

target_include_directories(ui_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../event)

target_link_libraries(ui_tests PRIVATE crisp::ui crisp::event)
add_test(NAME ui_tests COMMAND ui_tests)
```

## 10. 决策日志（Decision Log D1–Dn）

| 编号 | 裁决 | 结论（权威） | 依据/落点 |
|------|------|-------------|----------|
| **D1** | 绘制缓冲结构 | **记录式命令缓冲**（`CtPaintCmd` 判别联合 + 定容量 `kMaxPaintCmds=8192`，值语义零堆分配；命令内 font = `fontPixelSize` 值 + `family` `u16string_view` 引用，MUST-1 方案 a） | §3.3/§4.1；热路径零堆分配 + `end()` 一次性提交；对齐"记录式、值语义"（04 §3 六原则"热路径零堆分配"） |
| **D2** | 脏区聚合责任 | **widget-local markDamage → 窗口单矩形并集**；`CtBackingStore::addDamage` 收窗口坐标 + **受影响控件弱引用集合**（MUST-2）；聚合责任在 UI3 根分发器（坐标转换），UI2 提供入口 | §1.3/§4.3/§5.4；UI1 D2 单矩形并集直接复用；UI3 才有点击/输入路由；kPainted 配对通道 |
| **D3** | 变换/裁剪时机 | **记录时应用**（translate/clip 在命令 append 前作用于设备坐标），命令不含变换栈 | §4.1；命令纯设备坐标 → 后端执行无状态；`save/restore` 只保存变换+裁剪栈 |
| **D4** | 双缓冲策略 | **首版单缓冲复用**（每窗口一张，创建一次分配，present 只 Blit 脏区） | §5.4；MinGW 8.1 32 位内存受限（03 §7 风险表）；不做翻页交换 |
| **D5** | 像素格式 | **RGBA8888 唯一格式**（`CtPixelFormat::kRgba8888`） | §3.1；UI2 无混合、无格式转换；GDI 内部转换平台层 |
| **D6** | 字体度量 | **后端能力接口**（可选 `fontMetrics()`，PixelBuffer 固定值/GDI 真实值） | §3.4/§4.2；首版不做复杂排版（P10），度量可测 |
| **D7** | 文本范围 | **单行原样绘制**（无换行/对齐/富文本），`drawText(origin, text, font, color)` | §1.2/§2；`CtLabel` 首版纯文本 |
| **D8** | kPainted 产生 | **UI2 首次产生**（`CtBackingStore::flush` 成功后对受损控件记录）；经既有 `CtWidgetTrace` 环 | §7.1；UI1 已定义未产生（04 §2.3） |
| **D9** | 命令溢出语义 | **置 `overflowed` 标志 + 拒绝后续命令**；`end()` 仍 flush 已记录；标志可观测 | §4.1/§7.3；有界缓冲纪律（仿 `kMaxQueued=8192`） |

> 决策已在 §3/§4/§5/§7 正文标注 `DECISION Dn` 标记，编码 agent 照抄即落地。

---

## 11. 与 03 §4.2 的偏差（Deviations from 03 §4.2）

| 03 §4.2 草案 | UI2 定稿偏差 | 原因 |
|--------------|-------------|------|
| `CtPaintBackend::fontMetrics` 未在 §4.2.2 声明（仅在 §7 风险表提及） | **明确为能力接口**（§4.2，返回 false = 不支持） | 首版文本度量必须可测（PixelBuffer 固定值）；能力接口避免虚函数虚设 |
| 03 未定 `CtPaintCmd` 结构 | **记录式命令缓冲定案**（§3.3/§10 D1）；命令内 font 存 `fontPixelSize` 值 + `family` `u16string_view` 引用（MUST-1 方案 a） | 热路径零堆分配 + `end()` 一次性提交；drawText 记录命令不拷贝 family |
| 03 `CtBackingStore` 未定聚合责任 | **widget→窗口聚合归 UI3**（§10 D2）；UI2 只提供 `addDamage` 入口（含 affected 控件集合，MUST-2） | UI3 才有根分发/坐标转换（04 §3.6） |
| 03 `CtPainter::save/restore` 未定栈实现 | **定容量值语义栈**（`kMaxStackDepth=32`）+ `stackDepth()` 钩子 + 满栈 overflowed（WARN-1） | 可观测栈平衡（trace 记录） |
| 03 未定溢出语义 | **overflowed 标志 + 拒绝后续**，`end()` 复位（§10 D9/WARN-5） | 有界缓冲纪律 |
| 03 未定一帧调用序列 | **§4.3 权威帧序**（markDamage → addDamage → painter.begin/绘制/end → store.flush 仅 present + kPainted/clearDamage）（MUST-3） | 消除"store.flush 重复绘制"歧义 |
| 05 §4.2 `present` 声明为 `void` | **实现改为 `bool`**（审计 J2 定案：present 成功为 kPainted/clearDamage 配对前置条件，失败不配对且保留 damage 供重试）——接口头注释已登记，本节登记偏差 | 代码审查 SUGGEST-1；`ct_paint_backend.h` |
| 05 §4.2 仅 `flush(damage)` 单入口 | **实现新增 `submitCommands(cmds, count)`**（审计 PA4 定案：记录命令批经接口投递，替代 dynamic_cast + friend 通道）——接口头注释已登记 | 代码审查 SUGGEST-1；`ct_paint_backend.h` |
| 05 §4.1 无 `backend()` 访问器 | **实现新增 `CtPainter::backend()` 测试钩子** + `CtPaintState` struct 声明（transform+clip 栈条目） | 代码审查 SUGGEST-2；`ct_painter.h` |

> 其余（CtPainter begin/end、CtPaintBackend size/flush/present、CtBackingStore 单缓冲+脏区合批+BitBlt、双后端、平台例外）与 03 §4.2 一致。

---

## 12. 依赖与前置（Dependencies & Prerequisites）

- **复用（UI1）**：`CtRect`/`CtRegion`（几何，04 §2.1/§2.2）、`CtWidgetPhase::kPainted`（04 §2.3）、`CtWidgetTrace`（D3 环，04 §2.7）、`CtWidgetBase::markDamage/dirtyRegion/clearDamage`（04 §3.7）、`CtThreadAffinity`/`assertSameThread`（04 §4.2）。
- **复用（事件）**：`CtThreadAffinity`/`CtThreadId`（crisp::event）；UI2 不直接依赖 `CtDispatcher`/`CtEventLoop`（绘制无事件）。
- **不依赖**：Qt（核心层）、Win32/GDI（核心层；平台层 `CtPaintBackendGdi` 允许）、`crisp::thread`（UI2 无并行；UI4 控件重活才用 MT 桥）。
- **构建**：`crisp_ui` 静态库链接 `crisp::event`（PUBLIC）；测试链接 `crisp::ui crisp::event` + 事件 `test_harness.h`。

---

## 13. 生命周期与状态机（Lifecycle & State Machines）

- **CtPainter 状态机**：`idle → active（begin 成功）→ idle（end）`；未 begin 的绘制调用 no-op；重复 begin 返回 false；end 幂等。
- **save/restore 栈**：栈深 0 = 基准态；`save` 压入当前变换+裁剪；`restore` 弹出（空栈 no-op）；栈深经 `stackDepth()`/trace 可观测。
- **CtBackingStore 生命周期**：构造（**绑定后端引用**——后端构造时分配像素缓冲一次，MUST-A/§5.3）→ 每帧 `addDamage`（累积）→ `flush`（仅 present + 清 damage + 复位 batchCount + kPainted/clearDamage）→ 析构（**不拥有缓冲**，仅随引用失效；缓冲随后端析构释放）。
- **kPainted 记录**：`flush` 成功后对受损控件写 `kPainted`（与 `kDamage` 成对）；环满丢旧（D3 语义）。

---

## 14. 边界与错误处理（Edge Cases & Error Handling）

| 边界情况 | 契约 |
|----------|------|
| `begin()` 于零尺寸后端 | 返回 false（后端 `size().isEmpty()`） |
| `begin()` 重复（active 中） | 返回 false（不覆盖当前会话） |
| `end()` 未 begin（幂等） | no-op（安全） |
| 未 begin 调用 `fillRect/drawText/save/restore` | no-op（§4.1 契约；记录在文档） |
| `restore()` 空栈 | no-op（文档化，不 abort） |
| `addDamage` 的 `m_affected` 超 `kMaxAffected=64` | 拒绝超额控件（**不 push**），store 侧置溢出标志（D9 纪律，可观测；SUGGEST-1） |
| 命令缓冲溢出（>8192） | 置 `overflowed()`，拒绝后续命令；`end()` 仍 flush 已记录；**`overflowed()` 在 `end()` 复位**（WARN-5） |
| `clip(空矩形)` | 后续 fillRect/drawText 产生零可见像素（裁剪求交为空） |
| `translate` 导致坐标溢出 | 文档前置条件：UI2 设备坐标在窗口尺寸内（int 内，同 04 §2.1 契约） |
| 跨线程绘制（非 UI 线程） | `assertSameThread`：debug abort / release no-op（§5.1） |
| `CtPaintBackendGdi` HWND 无效 | 构造失败/冒烟断言（平台层，UI2 可选） |
| `addDamage(空矩形)` | `CtRegion::add` no-op（04 §2.2 契约） |
| `fontMetrics` 不支持（返回 false） | `drawText` 调用方应回退（UI2 测试后端均支持） |

---

## 15. 开放问题（Open Questions）

| # | 问题 | 状态 |
|---|------|------|
| O1 | 多矩形脏区列表（有界预分配）演进 | **留 UI2 演进**：单矩形并集（D2）已定；演进不改变公共 API 形状（04 §2.2 裁决） |
| O2 | GDI 后端完整测试（非冒烟） | UI2 仅冒烟（BitBlt 区域与脏区一致）；完整像素断言全在 PixelBuffer 层 |
| O3 | 字体度量真实值（GDI）与 PixelBuffer 固定值的对齐 | 留 UI3（平台窗口落地后统一） |
| O4 | 命令缓冲溢出后 UI2 的可见降级策略（局部重绘 vs 全量） | UI2 定案为标志可观测 + flush 已记录；全量回退随 UI3 |
| O5 | kPainted 记录是否需携带合批计数 | UI2 定案不携带（`damageRectCount=0`）；如需经 `lastFlushBatchCount()` 关联 |

---

## 16. 风险与缓解（Risks & Mitigations）

| 风险 | 约束/对策 |
|------|----------|
| MinGW 8.1 32 位内存受限 | 单缓冲复用（D4）；命令缓冲定容量（D1）；无 per-frame 分配 |
| 字体度量缺失导致文本绘制错位 | `fontMetrics` 能力接口（D6）+ PixelBuffer 固定值测试；GDI 真实值留 UI3 |
| 记录式缓冲溢出 | overflowed 标志可观测（D9）；§15 O4 留 UI3 全量回退 |
| GDI 平台层依赖 | README 例外条款；核心层零 Qt/Win32；UI2 GDI 为可选冒烟 |
| 变换/裁剪状态错误导致绘制错位 | 记录时应用（D3）+ save/restore 栈平衡断言（§8） |
| 跨线程绘制腐蚀缓冲 | §5.1 守门（debug abort / release no-op） |

---

## 17. 实施顺序与验收清单（Implementation Order & Acceptance Checklist）

### 17.1 实施顺序

1. **§3 类型落地**：`ct_paint_types.h/.cpp`（CtColor/CtPoint/CtSize/CtPixelFormat/CtFont/CtPaintMetrics）→ `ct_paint_cmd.h`（CtPaintCmd/CtPaintCmdType）。
2. **§7.1 相位扩展**：`ct_widget_trace.h` 的 `CtWidgetPhase` 增加 `kPaintState = 6`（WARN-A/SUGGEST-3；同步 04 §2.3 枚举）。
3. **§4 后端落地**：`ct_paint_backend.h`（纯接口 + size() 守门锚点）→ `ct_paint_backend_pixel.h/.cpp`（PixelBuffer，核心层可测）。
4. **§4.1 CtPainter**：`ct_painter.h/.cpp`（状态机 + 记录式缓冲 + save/restore 栈 + translate/clip + fillRect/drawText + sink）。
5. **§4.3 CtBackingStore**：`ct_backing_store.h/.cpp`（构造器 size+backend& + backend() + 单缓冲 + 合批 + flush 仅 present + kPainted/clearDamage）。
6. **测试**：`tests/ui/` 新增 4 个测试文件（§8.1），含零分配计数 + 合批 + 逐像素 + 守门 + 配对纯净性。
7. **CMake**：扩展 `src/ui/CMakeLists.txt` + `tests/ui/CMakeLists.txt`（§9）。
8. **门禁**：代码审计子agent → 修复 → 代码审查 → ctest 全绿。
9. **验收**：§17.2 清单逐项确认 → `DEPLOYED_OK`。

### 17.2 验收清单（UI2 完成定义）

- [ ] §3 类型与 §4 接口与 `src/ui/` 实现一致（权威规范回写核对）
- [ ] `CtWidgetPhase` 扩展 `kPaintState=6` 落地（`ct_widget_trace.h` + 04 §2.3 同步，SUGGEST-3）
- [ ] 全部 §8.2 用例通过（`tests/ui/` 全绿）
- [ ] `painter_fill_rect_hot_path_allocates_zero` 通过（§6.2 零堆分配验收）
- [ ] `painter_draw_text_hot_path_allocates_zero` 通过（MUST-1：drawText 记录命令零堆分配）
- [ ] `backing_store_batches_n_damage_into_one_flush` 通过（N 次 markDamage → 1 次 flush，lastFlushBatchCount == N，§4.3 帧序）
- [ ] `backing_store_flush_writes_kpainted_and_clears_damage` 通过（MUST-2：kDamage→kPainted 配对 + clearDamage）
- [ ] `pixel_backend_fill_rect_writes_pixels` 通过（PixelBuffer 逐像素正确性）
- [ ] ctest 全量 7/7 通过（event / thread / ui / qt_bridge / dual_run / dual_compare）
- [ ] 代码审计子agent 无 [MUST] 残留
- [ ] 代码审查通过（与本文档设计一致性核对）
- [ ] 本文档 §1–§17 定稿为权威规范（无空节）

---

## 结论（Conclusion）

**UI2 交付绘制基元：`CtPainter` 记录式状态机 + `CtPaintBackend`（PixelBuffer 可测/GDI 平台层）+ `CtBackingStore` 单缓冲脏区合批，纯 C++17 核心层零 Qt/Win32 依赖，热路径零堆分配，`kPainted` 可观测——待审计/审查门禁后 `DEPLOYED_OK`。**

---

READY_FOR_REVIEW
