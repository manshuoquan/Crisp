#pragma once

// ct_widget_trace.h — widget-side trace (crisp::ui, UI1).
// CtWidgetPhase is deliberately INDEPENDENT from crisp::event::CtTracePhase
// (precedent: crisp::thread::CtTaskPhase, docs/threading/03 §3.4):
// extending the event enum would change its uint8_t value set and affect the
// event subsystem's ABI/spec. Widget records live in their own ring and are
// correlated with event-side traces by timestamp at UI3's input mapper.
// Authoritative spec: docs/ui/04-ui1-architecture.md §2.3-§2.7.

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

/// Fixed-capacity ring buffer of widget trace records (UI1).
///
/// DECISION D3 (authoritative, docs/ui/04-ui1-architecture.md §8):
/// mutex-guarded ring, modeled on crisp::thread::CtTaskTrace (not the SPSC
/// CtTrace from the event system). Rationale: UI thread is the SINGLE writer
/// (record() inside CtWidgetBase::markDamage/ctor/dtor), but diagnostics and
/// tests read from any thread (copyOut) — a plain SPSC ring would make
/// cross-thread reads UB. Trace writes are NOT the zero-allocation hot path
/// (the hot path is markDamage/dirtyRegion itself); the mutex cost is
/// acceptable.
class CtWidgetTrace {
public:
    /// Record one widget trace entry. Returns false if the ring was full
    /// and the oldest entry was dropped (overflow, bounded ring).
    bool record(const CtWidgetRecord& rec);

    /// Number of records currently stored.
    std::size_t size() const noexcept;

    /// Copy stored records out (diagnostics / tests). Returns count copied.
    std::size_t copyOut(CtWidgetRecord* out, std::size_t max) const;

    static constexpr std::size_t kCapacity = 4096; ///< bounded ring (power of 2,
                                                    ///< mirrors CtTaskTrace::kCapacity)

private:
    mutable std::mutex m_mutex;   ///< guards ring (writer: UI thread; readers: any)
    std::size_t m_head = 0;       ///< next write slot
    std::size_t m_count = 0;      ///< valid records (<= kCapacity)
    CtWidgetRecord m_ring[kCapacity]{}; ///< ring storage (no heap)
};

} // namespace crisp::ui
