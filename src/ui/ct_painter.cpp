// ct_painter.cpp — UI2 painter state machine implementation (crisp::ui).
// Record-style command buffer (D1): fillRect/drawText append CtPaintCmd
// values (u16string_view refs — zero heap allocation, MUST-1); the
// transform+clip are applied AT RECORD TIME (D3); end() hands the batch
// to the backend's flush(damage) in one pass.
// Authoritative spec: docs/ui/05-ui2-architecture.md §4.1 / §10.

#include "ct_painter.h"

#include <cassert>
#include <chrono>
#include <climits> // INT_MAX (PA2: text-length overflow guard)

namespace crisp::ui {

bool CtPainter::begin(CtPaintBackend& backend) noexcept {
    // Already active: refuse without overwriting the current session
    // (§14 boundary table).
    if (m_backend)
        return false;
    // The backend's size() is the thread-guard anchor (WARN-E): a
    // foreign-thread call asserts in debug and returns an EMPTY size in
    // release — the session then degrades to no-op (the §8.2
    // begin-rejected-on-foreign-thread test).
    const CtSize surface = backend.size();
    if (surface.isEmpty())
        return false;
    m_backend = &backend;
    m_surfaceSize = surface; // PA5: cached — clipped() uses this, never
                             // re-queries backend.size() (thread-guard
                             // anchor stays in begin() only)
    m_cmdCount = 0;
    m_overflowed = false;
    m_stackDepth = 0;
    m_state = CtPaintState{};
    return true;
}

void CtPainter::end() noexcept {
    // Idempotent: end() without a session is a documented no-op (§14).
    if (!m_backend)
        return;
    CtPaintBackend* backend = m_backend;
    m_backend = nullptr;
    // The session's dirty region: the union of every recorded command's
    // bounds (device coordinates, D3). UI2 uses ONE bounding rect (D2).
    // The kDrawText damage uses the SAME w=size*8, h=16 metrics as the
    // record-time clip check (PA4 audit MUST: a 1x1 text damage would
    // underestimate the dirty region for any damage-respecting backend).
    CtRegion damage;
    for (std::size_t i = 0; i < m_cmdCount; ++i) {
        const CtPaintCmd& cmd = m_cmds[i];
        if (cmd.type == CtPaintCmdType::kFillRect)
            damage.add(cmd.rect);
        else
            damage.add(CtRect{cmd.origin.x, cmd.origin.y,
                              static_cast<int>(cmd.text.size()) * 8, 16});
    }
    // Hand the recorded batch to the backend through the INTERFACE
    // (PA4: submitCommands — no dynamic_cast, no friend channel; any
    // backend receives the batch uniformly). The buffer stays valid
    // until end() returns (synchronous execute).
    backend->submitCommands(m_cmds, m_cmdCount);
    // One flush pass = the frame sequence (MUST-3: painter.end() ->
    // backend.flush(damage)).
    backend->flush(damage);
    // WARN-5 ruling: end() clears the buffer and RESETS the overflow
    // flag; the session is over.
    m_cmdCount = 0;
    m_overflowed = false;
    m_stackDepth = 0;
}

bool CtPainter::isActive() const noexcept {
    return m_backend != nullptr;
}

void CtPainter::save() {
    // Full stack (kMaxStackDepth): debug assert / release no-op + set
    // overflowed (WARN-1 ruling, same discipline as D9).
    if (!m_backend)
        return; // not in a session: no-op (§14)
    if (m_stackDepth >= kMaxStackDepth) {
        assert(false && "CtPainter::save: save stack overflow");
        m_overflowed = true;
        return;
    }
    m_stack[m_stackDepth] = m_state;
    ++m_stackDepth;
    tracePaintState(); // WARN-A: stack-depth changes are observable
}

void CtPainter::restore() {
    if (!m_backend)
        return; // not in a session: no-op (§14)
    if (m_stackDepth == 0)
        return; // empty stack: documented no-op (§14)
    --m_stackDepth;
    m_state = m_stack[m_stackDepth];
    tracePaintState();
}

std::size_t CtPainter::stackDepth() const noexcept {
    return m_stackDepth;
}

void CtPainter::translate(int dx, int dy) noexcept {
    if (!m_backend)
        return; // no-op outside a session (§14)
    m_state.tx += dx;
    m_state.ty += dy;
}

void CtPainter::clip(const CtRect& rect) noexcept {
    if (!m_backend)
        return; // no-op outside a session (§14)
    // Clip is intersected with the current clip (D3 / §4.1 contract).
    // FIRST clip() call (clipSet false): adopt `rect` directly — the
    // default CtRect{} (all-zero) is NOT a valid "full surface" clip, so
    // intersecting with it would wrongly empty the clip. Subsequent
    // calls intersect (clipSet true). An EMPTY rect still sets clipSet
    // (clip(empty) = nothing draws, §4.1/§14).
    if (!m_state.clipSet)
        m_state.clip = rect;
    else
        m_state.clip = m_state.clip.intersect(rect);
    m_state.clipSet = true;
}

void CtPainter::fillRect(const CtRect& rect, const CtColor& color) noexcept {
    if (!m_backend)
        return; // no-op outside a session (§14)
    const CtRect devRect = clipped(translated(rect));
    if (devRect.isEmpty())
        return; // empty after translate+clip: nothing to draw
    CtPaintCmd cmd;
    cmd.type = CtPaintCmdType::kFillRect;
    cmd.rect = devRect;
    cmd.color = color;
    appendCmd(cmd);
}

void CtPainter::drawText(const CtPoint& origin, const std::u16string_view& text,
                         const CtFont& font, const CtColor& color) noexcept {
    if (!m_backend)
        return; // no-op outside a session (§14)
    const CtPoint devOrigin = translated(origin);
    // Clip against the current clip using the fixed UI2 text metrics
    // (charWidth=8, lineHeight=16): the command is only recorded when
    // any part of its cell falls inside the clip.
    // PA2 (audit MUST): reject absurd text lengths BEFORE the int
    // multiply — text.size() * 8 must fit int (device coords are int).
    // A text wider than INT_MAX/8 pixels can never draw anyway.
    if (text.size() > static_cast<std::size_t>(INT_MAX / 8))
        return; // silently reject: unbounded text is a caller bug
    const int w = static_cast<int>(text.size()) * 8;
    const CtRect textRect{devOrigin.x, devOrigin.y, w, 16};
    if (clipped(textRect).isEmpty())
        return;
    CtPaintCmd cmd;
    cmd.type = CtPaintCmdType::kDrawText;
    cmd.origin = devOrigin;
    cmd.color = color;
    // MUST-1: the command stores the pixel size as a value and BOTH the
    // family and the text as u16string_view REFS into caller-owned
    // storage — no copy, zero heap allocation (record path).
    cmd.fontPixelSize = font.pixelSize;
    cmd.family = font.family;
    cmd.text = text;
    appendCmd(cmd);
}

std::size_t CtPainter::commandCount() const noexcept {
    return m_cmdCount;
}

bool CtPainter::overflowed() const noexcept {
    return m_overflowed;
}

bool CtPainter::appendCmd(const CtPaintCmd& cmd) noexcept {
    // Overflow: reject the command and set the flag (D9). The already
    // recorded commands remain valid — end() still flushes them.
    if (m_cmdCount >= kMaxPaintCmds) {
        m_overflowed = true;
        return false;
    }
    m_cmds[m_cmdCount] = cmd;
    ++m_cmdCount;
    return true;
}

CtRect CtPainter::translated(const CtRect& rect) const noexcept {
    return CtRect{rect.x + m_state.tx, rect.y + m_state.ty, rect.width,
                  rect.height};
}

CtPoint CtPainter::translated(const CtPoint& p) const noexcept {
    return CtPoint{p.x + m_state.tx, p.y + m_state.ty};
}

CtRect CtPainter::clipped(const CtRect& rect) const noexcept {
    // No clip() call yet: clip to the FULL SURFACE (device coords, D3).
    // PA5: uses the size CACHED at begin() — never re-queries
    // backend->size() here (a foreign-thread call would abort in debug;
    // the thread guard belongs to begin() alone, §5.1/§14).
    if (!m_state.clipSet)
        return rect.intersect(CtRect{0, 0, m_surfaceSize.width,
                                     m_surfaceSize.height});
    // clip() was called: intersect with the current clip. An EMPTY clip
    // (clip(empty) or a degenerate intersection) yields an empty rect —
    // nothing draws (§4.1/§14).
    return rect.intersect(m_state.clip);
}

void CtPainter::tracePaintState() noexcept {
    // WARN-A ruling: when a trace sink is injected, save/restore stack-
    // depth changes are recorded as kPaintState records — an INDEPENDENT
    // phase (never reuses kDamage, so the kDamage->kPainted pairing is
    // never polluted, §7.2 / §8.2 pairing-purity test). No sink -> only
    // stackDepth() is observable.
    if (!m_sink)
        return;
    CtWidgetRecord rec;
    rec.phase = CtWidgetPhase::kPaintState;
    rec.objectId = 0;   ///< painter is not a widget: neutral identity
    rec.parentObjectId = 0;
    rec.when = std::chrono::steady_clock::now();
    rec.damageRectCount = static_cast<std::uint32_t>(m_stackDepth);
    m_sink->record(rec);
}

} // namespace crisp::ui
