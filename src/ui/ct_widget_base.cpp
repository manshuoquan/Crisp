// ct_widget_base.cpp — crisp::ui core widget base implementation (UI1).
//
// Ownership (P5): shared_ptr tree + weak_ptr back-references. Thread rule:
// mutating operations assert the widget's affinity thread. Damage (P4/D2):
// single-rect union; zero heap allocation on markDamage. Trace (P6/D3):
// kCreated/kDestroyed/kDamage records into the mutex-guarded ring.

#include "ct_widget_base.h"

#include <algorithm>

namespace crisp::ui {

std::shared_ptr<CtWidgetBase> CtWidgetBase::create(
    const std::shared_ptr<CtWidgetBase>& parent) {
    // IMPORTANT (P5): the ctor must NOT call attachTo() — enable_shared_from_this's
    // weak reference is only established AFTER the shared_ptr is constructed, so
    // shared_from_this() inside the ctor throws bad_weak_ptr. The factory builds
    // the shared_ptr first, sets the weak self-reference (the sole
    // enable_shared_from_this base — CtReceiver — is initialized by now), then
    // attaches. NOTE: `new` (not make_shared) is REQUIRED here — the ctor is
    // protected (P5: factories are the only sanctioned construction path), and
    // MinGW 8.1's libstdc++ allocator cannot legally call a protected ctor from
    // make_shared (verified empirically). The raw pointer never escapes: the
    // shared_ptr takes ownership immediately (non-hot path; audit WARN accepted).
    std::shared_ptr<CtWidgetBase> self(new CtWidgetBase(parent));
    self->m_selfWeak = self;
    self->attachTo(parent);
    return self;
}

CtWidgetBase::CtWidgetBase() {
    recordTrace(CtWidgetPhase::kCreated);
}

CtWidgetBase::CtWidgetBase(const std::shared_ptr<CtWidgetBase>& parent)
    : m_parent(parent) {
    recordTrace(CtWidgetPhase::kCreated);
}

CtWidgetBase::~CtWidgetBase() {
    // RAII teardown: trace kDestroyed BEFORE detaching so the record keeps
    // the parent association (P6 trace fidelity: kCreated has the parent,
    // kDestroyed must report the SAME parentObjectId). NOTE: kDestroyed is
    // written into the per-widget ring which becomes unreachable once the
    // destructor completes — its content is NOT readable after destruction
    // (documented limitation; pairing is guaranteed by the record order
    // here, see docs/ui/04-ui1-architecture.md §2.7 acceptance ruling).
    recordTrace(CtWidgetPhase::kDestroyed);
    detachFromParent();
}

void CtWidgetBase::setParent(const std::shared_ptr<CtWidgetBase>& newParent) {
    // Thread rule (WARN fix): debug abort; release degrades to no-op.
    if (!assertSameThread())
        return;
    // Tree invariant guard (P5): reject self-parenting and cycles. Walking
    // up from newParent via the weak chain — if we meet `this`, the new
    // parent is one of our descendants, which would form a weak cycle.
    if (newParent.get() == this)
        return;
    for (auto ancestor = newParent; ancestor; ancestor = ancestor->parent().lock()) {
        if (ancestor.get() == this)
            return; // cycle: newParent is our own descendant
    }
    // Transactional re-parent (WARN fix): the OLD parent list is only
    // mutated AFTER the new attachment succeeded — if emplace_back throws
    // bad_alloc inside attachTo, the tree is unchanged (the widget still
    // belongs to the old parent). Order: lock old parent, attach to new,
    // then remove from old.
    const auto oldParent = m_parent.lock();
    if (newParent && newParent.get() == oldParent.get())
        return; // re-parent to the SAME parent: idempotent no-op (MUST fix —
                // otherwise attachTo duplicates the child entry, then the
                // remove_all below erases them ALL, breaking the tree
                // invariant "parent lists every child exactly once")
    if (!newParent) {
        // Top-level detachment: clear the back-reference, then remove the
        // entry from the old parent's list.
        m_parent.reset();
        if (oldParent) {
            auto& siblings = oldParent->m_children;
            siblings.erase(
                std::remove_if(siblings.begin(), siblings.end(),
                               [this](const std::weak_ptr<CtWidgetBase>& w) {
                                   return !w.owner_before(m_selfWeak) &&
                                          !m_selfWeak.owner_before(w);
                               }),
                siblings.end());
        }
        return;
    }
    attachTo(newParent);          // may throw bad_alloc -> tree unchanged
    if (oldParent) {
        auto& siblings = oldParent->m_children;
        siblings.erase(
            std::remove_if(siblings.begin(), siblings.end(),
                           [this](const std::weak_ptr<CtWidgetBase>& w) {
                               return !w.owner_before(m_selfWeak) &&
                                      !m_selfWeak.owner_before(w);
                           }),
            siblings.end());
    }
}

std::shared_ptr<CtWidgetBase> CtWidgetBase::childAt(int px, int py) const {
    // Reverse z-order walk: the LAST child is topmost (front of the list).
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        const auto child = it->lock();
        if (!child)
            continue; // stale weak ref; pruned on the next attach/detach
        if (child->geometry().contains(px, py))
            return child;
    }
    return nullptr;
}

void CtWidgetBase::setGeometry(const CtRect& rect) {
    // Thread rule (WARN fix): assert + degrade. Debug aborts; release
    // returns false and the call degrades to a no-op (CtThreadAffinity
    // contract, event-system §7.14 precedent).
    if (!assertSameThread())
        return;
    // §3.4 normalization: negative width/height normalize to zero.
    m_geometry = CtRect{rect.x, rect.y,
                        rect.width > 0 ? rect.width : 0,
                        rect.height > 0 ? rect.height : 0};
}

void CtWidgetBase::setVisible(bool visible) noexcept {
    if (!assertSameThread())
        return;
    m_state.visible = visible;
}

void CtWidgetBase::setEnabled(bool enabled) noexcept {
    if (!assertSameThread())
        return;
    m_state.enabled = enabled;
}

void CtWidgetBase::dispatchInput(
    crisp::event::CtEvent<crisp::input::CtMousePayload>& event) {
    if (!assertSameThread())
        return;
    // Disabled widgets reject input (§3.6 / §3.10 contract).
    if (!m_state.enabled) {
        event.ignore();
        return;
    }
    // UI1: no typed-handler registration surface yet (the §3.10 field
    // consumption contract and the CtDispatcher-based registration are
    // being finalized); dispatch is a no-op carrier that records nothing.
    // UI4 wires the typed handlers via CtReceiver::bindHandler.
}

void CtWidgetBase::dispatchInput(
    crisp::event::CtEvent<crisp::input::CtKeyPayload>& event) {
    if (!assertSameThread())
        return;
    if (!m_state.enabled) {
        event.ignore();
        return;
    }
}

void CtWidgetBase::markDamage(const CtRect& rect) noexcept {
    // Thread rule: debug abort; release degrades to no-op (cross-thread
    // damage would silently corrupt the dirty region).
    if (!assertSameThread())
        return;
    // O(1) union into the single bounding box (D2); zero heap allocation on
    // the union itself; the kDamage trace takes one D3-approved lock.
    m_dirty.add(rect);
    ++m_damageRectCount;
    recordTrace(CtWidgetPhase::kDamage, m_damageRectCount);
}

void CtWidgetBase::clearDamage() noexcept {
    if (!assertSameThread())
        return;
    m_dirty.clear();
    m_damageRectCount = 0;
}

std::uint64_t CtWidgetBase::objectId() const noexcept {
    return crisp::event::CtReceiver::objectId();
}

crisp::event::CtThreadId CtWidgetBase::affinityThread() const noexcept {
    return crisp::event::CtReceiver::affinityThread();
}

void CtWidgetBase::recordTrace(CtWidgetPhase phase, std::uint32_t damageRectCount) {
    CtWidgetRecord rec;
    rec.phase = phase;
    rec.objectId = objectId();
    const auto parent = m_parent.lock();
    rec.parentObjectId = parent ? parent->objectId() : 0;
    rec.when = std::chrono::steady_clock::now();
    rec.damageRectCount = damageRectCount;
    m_trace.record(rec);
}

void CtWidgetBase::attachTo(const std::shared_ptr<CtWidgetBase>& newParent) {
    if (!newParent) {
        // Top-level detachment: m_parent cleared by the CALLER's cleanup
        // (setParent transactional path removes the old list entry first);
        // here we just clear the back-reference. An empty m_parent with a
        // residual old-list entry cannot happen through setParent.
        m_parent.reset();
        return;
    }
    // Contract guard: the factories set m_selfWeak before any tree surgery.
    // A default-constructed widget (empty m_selfWeak) reaching here is a
    // P5 violation — the weak entry would be a zombie (debug abort).
    assert(!m_selfWeak.expired() && "attachTo: m_selfWeak not set by factory");
    // Order matters: push into the parent's list FIRST, then set m_parent.
    // vector::emplace_back has the STRONG exception guarantee — if it
    // throws bad_alloc, the parent's list is unchanged and m_parent is
    // still unset: the tree stays consistent (no half-attached state).
    newParent->m_children.emplace_back(m_selfWeak);
    m_parent = newParent;
}

void CtWidgetBase::detachFromParent() {
    const auto parent = m_parent.lock();
    if (!parent)
        return;
    // Contract guard: owner_before identity comparison needs a NON-EMPTY
    // m_selfWeak (the factories set it). NOTE: `!m_selfWeak.expired()` would
    // be WRONG here — expired() == (use_count == 0), which is exactly true
    // while the destructor runs (the last strong reference is gone). The
    // guard must test "has a control block", i.e. differs from a default
    // (empty) weak_ptr — owner_before is valid even for EXPIRED weak_ptrs
    // that still hold a control block.
    const std::weak_ptr<CtWidgetBase> empty;
    assert((empty.owner_before(m_selfWeak) || m_selfWeak.owner_before(empty)) &&
           "detachFromParent: m_selfWeak not set by factory");
    // Remove this child from the parent's z-order list. Identity match via
    // OWNER comparison against m_selfWeak: unlike `w.lock().get() == this`,
    // owner_before is valid even for EXPIRED weak_ptrs — the destructor runs
    // after the last strong reference is gone, when lock() returns empty for
    // this very entry (a `lock().get()` predicate would leave a permanent
    // zombie entry, violating the P5 RAII detach contract).
    auto& siblings = parent->m_children;
    siblings.erase(
        std::remove_if(siblings.begin(), siblings.end(),
                       [this](const std::weak_ptr<CtWidgetBase>& w) {
                           return !w.owner_before(m_selfWeak) &&
                                  !m_selfWeak.owner_before(w);
                       }),
        siblings.end());
    m_parent.reset();
}

} // namespace crisp::ui
