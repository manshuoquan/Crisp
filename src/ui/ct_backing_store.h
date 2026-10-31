#pragma once

// ct_backing_store.h — UI2 per-window backing store (crisp::ui).
// QBackingStore model: one full pixel buffer + damage batching + partial
// flush. Authoritative spec: docs/ui/05-ui2-architecture.md §4.3.
// DECISION D2: UI2 aggregates widget-local markDamage into ONE window
// bounding rect; lastFlushBatchCount() counts merges this frame.

#include <cstddef>
#include <memory>
#include <vector>

#include "ct_paint_backend.h"  // CtPaintBackend
#include "ct_geometry.h"       // CtRect / CtRegion
#include "ct_widget_base.h"    // CtWidgetBase (affected weak refs)

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

    /// True once an addDamage call was rejected because the affected
    /// widget list reached kMaxAffected (D9 discipline: bounded, flag +
    /// reject excess, §14 boundary table). Reset at flush().
    bool overflowed() const noexcept { return m_overflowed; }

private:
    /// Append one affected weak ref to the bounded list (dedup by owner
    /// identity, MUST-2); rejects beyond kMaxAffected (D9 discipline).
    void addAffected(const std::weak_ptr<CtWidgetBase>& widget) noexcept;

    CtPaintBackend& m_backend;    ///< the surface backend (NOT owned, MUST-A)
    CtRegion m_dirty;             ///< accumulated window damage (D2)
    std::size_t m_batchCount = 0; ///< merges this frame (test hook)
    bool m_overflowed = false;    ///< affected-list overflow flag (D9)
    static constexpr std::size_t kMaxAffected = 64; ///< WARN-C bound

    /// Affected widgets accumulated across addDamage calls this frame
    /// (weak refs; dedup by owner identity — MUST-2 channel).
    /// Fixed-capacity preallocated array (WARN-C ruling): kMaxAffected = 64;
    /// overflow follows D9 discipline (flag + reject excess; §14).
    std::vector<std::weak_ptr<CtWidgetBase>> m_affected; // reserved once (ctor)
};

} // namespace crisp::ui
