#pragma once

// ct_painter.h — UI2 painter state machine (crisp::ui).
// Record-style command buffer (D1); begin/end pair; save/restore stack.
// Authoritative spec: docs/ui/05-ui2-architecture.md §4.1 / §10.

#include <cstddef>
#include <cstdint>

#include "ct_paint_cmd.h"      // CtPaintCmd / CtPaintCmdType / kMaxPaintCmds
#include "ct_paint_types.h"    // CtColor / CtFont / CtPoint / CtRect
#include "ct_paint_backend.h"  // CtPaintBackend
#include "ct_widget_trace.h"   // CtWidgetTrace / CtWidgetPhase (kPaintState sink)

namespace crisp::ui {

/// One transform+clip stack entry (D3: the transform is applied to
/// commands AT RECORD TIME — the entry only stores the current translate
/// and clip used for that application, so save/restore can roll back).
struct CtPaintState {
    int tx = 0;        ///< current translate dx (device coords)
    int ty = 0;        ///< current translate dy (device coords)
    CtRect clip;       ///< current clip rect (device coords)
    bool clipSet = false; ///< true once clip() was called — distinguishes
                          ///< "no clip" (draw anywhere on the surface) from
                          ///< "clip(empty)" (nothing draws, §4.1/§14)
};

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

    /// The backend targeted by the active session (null when idle).
    /// Test/observation hook for the frame sequence.
    CtPaintBackend* backend() const noexcept { return m_backend; }

private:
    /// Append one command to the bounded buffer; overflow follows the D9
    /// discipline (set overflowed, reject the command). Returns false
    /// when the command was rejected.
    bool appendCmd(const CtPaintCmd& cmd) noexcept;

    /// Apply the current translate to a device-coordinate rect.
    CtRect translated(const CtRect& rect) const noexcept;

    /// Apply the current translate to a device-coordinate point.
    CtPoint translated(const CtPoint& p) const noexcept;

    /// Intersect `rect` with the current clip (record-time clipping, D3).
    CtRect clipped(const CtRect& rect) const noexcept;

    /// Record a kPaintState trace entry on the sink (WARN-A): save push
    /// (stack depth after push) / restore pop (depth after pop).
    void tracePaintState() noexcept;

    CtPaintBackend* m_backend = nullptr;      ///< active backend (null when idle)
    CtSize m_surfaceSize;                     ///< cached at begin() (PA5: clipped()
                                              ///< must NOT re-query size() — the
                                              ///< thread-guard abort would move from
                                              ///< begin() into fillRect)
    CtPaintCmd m_cmds[kMaxPaintCmds];         ///< record-style buffer
    std::size_t m_cmdCount = 0;               ///< valid commands
    bool m_overflowed = false;                ///< overflow flag (reset at end)
    CtWidgetTrace* m_sink = nullptr;          ///< optional trace sink (not owned, WARN-A)
    CtPaintState m_state;                     ///< current transform+clip
    static constexpr std::size_t kMaxStackDepth = 32; ///< WARN-1/WARN-D bound
    CtPaintState m_stack[kMaxStackDepth];     ///< fixed-capacity value-semantic stack
    std::size_t m_stackDepth = 0;             ///< valid entries (<= kMaxStackDepth)

    // NOTE (WARN-2): CtPainter is INTENDED to live on the heap or as a
    // per-window member — m_cmds[kMaxPaintCmds] is ~0.5MB; do NOT stack-
    // allocate in deep call chains (MinGW 8.1 1MB default stack).
};

/// Whether a widget phase is the UI2 painter stack-depth marker.
/// (Helper for tests; the phase itself is defined in ct_widget_trace.h.)
inline bool isPaintStatePhase(CtWidgetPhase phase) noexcept {
    return phase == CtWidgetPhase::kPaintState;
}

} // namespace crisp::ui
