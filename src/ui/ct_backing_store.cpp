// ct_backing_store.cpp — UI2 per-window backing store implementation
// (crisp::ui). Single-buffer reuse (D4): the store holds the backend by
// reference (MUST-A); addDamage merges into ONE bounding rect (D2) and
// collects affected widgets (MUST-2); flush() ONLY presents the dirty
// region and does the kPainted/clearDamage bookkeeping — the drawing
// already happened in painter.end() -> backend.flush (MUST-B frame
// sequence, §4.3).
// Authoritative spec: docs/ui/05-ui2-architecture.md §4.3 / §6.1.

#include "ct_backing_store.h"

#include <chrono>

#include "ct_widget_trace.h"  // CtWidgetPhase / CtWidgetRecord (kPainted)

namespace crisp::ui {

CtBackingStore::CtBackingStore(const CtSize& size, CtPaintBackend& backend)
    : m_backend(backend) {
    // WARN-C ruling: the affected-widget list is a FIXED-CAPACITY
    // preallocated array — one reserve() at construction, reused for the
    // store's whole lifetime (zero allocations on addDamage/flush, §6.1).
    m_affected.reserve(kMaxAffected);
    (void)size;
    // NOTE (MUST-A): the backend owns the pixel buffer; the store holds
    // it by reference and reuses ONE buffer for the window's lifetime
    // (D4). `size` documents the surface the store presents to; the
    // backend's own size() is authoritative for painting.
}

void CtBackingStore::addDamage(
    const CtRegion& damage,
    const std::vector<std::weak_ptr<CtWidgetBase>>& affected) {
    // Merge damage: union of all markDamage rects this frame (D2 single
    // bounding rect). An EMPTY region is a complete no-op — no union,
    // no batchCount bump, no affected collection (audit WARN-I3: an
    // empty damage must not pollute the lastFlushBatchCount hook).
    if (damage.isEmpty())
        return;
    m_dirty.add(damage.bounds);
    ++m_batchCount;
    // Collect the affected widgets (MUST-2 channel): dedup by owner
    // identity; overflow follows D9 discipline (flag + reject excess).
    for (const auto& w : affected)
        addAffected(w);
}

std::size_t CtBackingStore::lastFlushBatchCount() const noexcept {
    return m_batchCount;
}

void CtBackingStore::flush() noexcept {
    // MUST-B frame sequence: the drawing ALREADY happened in
    // painter.end() -> backend.flush(damage). store.flush() only
    // presents the dirty region and does the bookkeeping — it does NOT
    // repaint. J2 (audit): kPainted/clearDamage are written ONLY when
    // present succeeded — a failed present must NOT complete the
    // kDamage -> kPainted pairing (the damage stays unflushed).
    if (!m_dirty.isEmpty()) {
        if (!m_backend.present(m_dirty.bounds)) {
            // Present failed: keep the store-side damage + batch count so
            // the next flush retries; DO NOT write kPainted / clear the
            // widgets (§7.1 pairing precondition). Observable via the
            // retained dirty region + batch count.
            return;
        }
    }
    // Write kPainted on the affected widgets and clear their damage
    // (kDamage -> kPainted pairing, §7.1 / MUST-2).
    for (const auto& w : m_affected) {
        const auto widget = w.lock();
        if (!widget)
            continue; // stale weak ref (widget destroyed): skip
        widget->clearDamage();
        // The kPainted record is written DIRECTLY to the widget's own
        // trace ring (CtWidgetTrace::record) — clearDamage() itself
        // produces no trace; the pairing is completed here.
        CtWidgetRecord rec;
        rec.phase = CtWidgetPhase::kPainted;
        rec.objectId = widget->objectId();
        const auto parent = widget->parent().lock();
        rec.parentObjectId = parent ? parent->objectId() : 0;
        rec.when = std::chrono::steady_clock::now();
        // O5 ruling: kPainted carries no batch count (damageRectCount = 0).
        rec.damageRectCount = 0;
        widget->trace().record(rec);
    }
    // Clear the store-side damage + reset the batch count + reset the
    // overflow flag (WARN-C: bounded, per-frame).
    m_dirty.clear();
    m_batchCount = 0;
    m_overflowed = false;
    m_affected.clear();
}

void CtBackingStore::addAffected(
    const std::weak_ptr<CtWidgetBase>& widget) noexcept {
    if (widget.expired())
        return; // stale weak ref: not collected
    // Dedup by OWNER identity (MUST-2 / UI1 §4.5 precedent): owner_before
    // works even for expired weak_ptrs, so identity is exact.
    for (const auto& existing : m_affected) {
        if (!existing.owner_before(widget) && !widget.owner_before(existing))
            return; // already collected this frame
    }
    // Bounded list (WARN-C): check BEFORE push; beyond kMaxAffected the
    // excess is REJECTED and the store sets the overflow flag (D9
    // discipline, §14 boundary table — never unbounded growth).
    if (m_affected.size() >= kMaxAffected) {
        m_overflowed = true;
        return;
    }
    m_affected.push_back(widget);
}

} // namespace crisp::ui
