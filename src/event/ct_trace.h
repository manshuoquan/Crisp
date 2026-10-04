#pragma once

// CtTrace / CtObserver: built-in observability for the event system.
//
// Part of the CRISP project. Design principle: observability is a first-class
// citizen, not a bolted-on debug hack. See docs/event-system/04-event-system-redesign.md §7.9.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "ct_event.h"              // CtTracePhase (Rev 7, P1-3: moved here from this header)
#include "ct_dispatch_context.h"   // CtDispatchContext (const&)

namespace crisp::event {

/// A single trace record: one step of an event's journey.
/// Strings are compile-time names + numeric ids only — hot path stays
/// allocation-free. `context` is a full snapshot of the dispatch context
/// (phase, receiverId, filterIndex, elapsed) for rich post-hoc analysis.
struct CtTraceRecord {
    std::chrono::steady_clock::time_point when{};
    std::string_view typeName{};      ///< compile-time name of the payload type
    CtTracePhase phase{CtTracePhase::kDispatched};
    uint64_t objectId = 0;            ///< receiver objectId (0 = none)
    uint64_t sequence = 0;            ///< dispatcher-assigned sequence (§8.2; M1: 0 — see §13 note)
    CtDispatchContext context;        ///< full read-only context snapshot
};

/// Per-thread, fixed-capacity ring buffer (lock-free writes, Rev 2 P1-4).
/// SINGLE producer AND SINGLE consumer: the OWNING thread of the
/// CtEventLoop/dispatcher — it is the only thread that may call record(),
/// dump() or clear(). All other threads MUST NOT touch this object.
class CtTrace {
public:
    static constexpr std::size_t kCapacity = 4096; ///< power of two (ring)

    /// Record one dispatch step (hot path, zero allocation, no locks).
    /// Calling thread MUST be the owning thread (contract #1).
    /// `objectId` is a convenience copy of ctx.receiverId() (§7.9 契约, P3-5).
    void record(std::string_view typeName, CtTracePhase phase,
                CtDispatchContext ctx, uint64_t objectId);

    /// Monotonic write counter; increments per record. Never wraps for
    /// ordering checks (64-bit). Same ownership contract as record().
    std::uint64_t sequence() const noexcept;

    /// Number of records captured since construction (monotonic).
    /// Same ownership contract as record().
    std::size_t count() const noexcept;

    /// Read-only access to the retained record at logical index `i`
    /// (0-based, chronological). i < count(). Same ownership contract as
    /// record() — single-threaded diagnostics/tests only.
    /// (M1 test support: lets tests assert the exact phase sequence, §13.1
    /// case 11, without duplicating the ring math in the test binary.)
    const CtTraceRecord& at(std::size_t i) const noexcept;

    /// True if no records have been captured yet.
    bool empty() const noexcept { return m_count.load(std::memory_order_seq_cst) == 0; }

    /// Dump retained records in chronological order (debugging).
    /// Same ownership contract as record() — NO locks, NO extra
    /// synchronization (Rev 2, P1-4: "dump: locks" removed).
    void dump() const;

    /// Clear all retained records (debugging).
    /// Same ownership contract as record().
    void clear();

private:
    static const char* phaseName(CtTracePhase p) noexcept;

    // Single-producer/single-consumer ring: m_head advances by the owning
    // thread only. Memory ordering (Rev 2, P2-11):
    //   - m_head:  store with std::memory_order_release, load with
    //     std::memory_order_acquire;
    //   - m_count: std::memory_order_seq_cst (diagnostics only; cheap).
    CtTraceRecord m_buffer[kCapacity]{};
    std::atomic<std::uint64_t> m_head{0}; ///< next slot to write (release/acquire)
    std::atomic<std::size_t> m_count{0};  ///< records since construction (seq_cst)
    std::uint64_t m_writeSequence = 0;    ///< monotonic record counter (per-record seq)
};

} // namespace crisp::event
