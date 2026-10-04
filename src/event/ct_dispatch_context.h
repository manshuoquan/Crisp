#pragma once

// CtDispatchContext: immutable, read-only context describing one dispatch step.
//
// Part of the CRISP event subsystem (crisp::event). Filled by CtDispatcher
// during dispatch; handed to observers and trace. Observers MUST NOT mutate
// it (const) — observation cannot affect outcome.

#include <chrono>
#include <cstdint>
#include <cstddef>

#include "ct_event.h" // CtEventOrigin, CtTracePhase (Rev 7, P1-3: CtTracePhase lives here now)

// (Rev 7, P1-3) NO #include "ct_trace.h" here — ct_trace.h includes THIS
// header (CtTraceRecord holds a CtDispatchContext by value). CtTracePhase
// has moved into ct_event.h so both headers get it from a single common
// source; the include graph is now acyclic (ctx -> event, trace -> ctx).

namespace crisp::event {

/// Immutable, read-only context describing one dispatch step.
/// Filled by CtDispatcher during dispatch; handed to observers and trace.
/// Observers MUST NOT mutate it (const) — observation cannot affect outcome.
///
/// (Rev 2, P1-2) The default constructor is PRIVATE so application code can
/// never fabricate a context ("应用不可伪造" holds); the dispatcher and the
/// trace ring are friends. Because the default constructor still EXISTS
/// (private), CtTraceRecord (§7.9) remains a default-constructible
/// aggregate and `m_buffer[kCapacity]{}` value-initialization compiles.
class CtDispatchContext {
public:
    CtDispatchContext(const CtDispatchContext&) = default; ///< cheap value snapshot
    CtDispatchContext& operator=(const CtDispatchContext&) = default;

    /// The pipeline phase this step corresponds to (see §7.9).
    CtTracePhase phase() const noexcept { return m_phase; }

    /// objectId of the target receiver (0 for broadcast / not yet known).
    uint64_t receiverId() const noexcept { return m_receiverId; }

    /// 1-based ordinal of the filter being evaluated (0 when not a filter step).
    uint64_t filterIndex() const noexcept { return m_filterIndex; }

    /// Elapsed time of this dispatch step (steady clock, measured internally).
    std::chrono::steady_clock::duration elapsed() const noexcept { return m_elapsed; }

    /// Number of live handlers for this event type in the current snapshot.
    std::size_t handlerCount() const noexcept { return m_handlerCount; }

private:
    // Filled only by CtDispatcher (friend) — application code never builds one.
    // (Rev 2, P1-2) Private default ctor: exists so CtTraceRecord can hold
    // and value-initialize it, but is inaccessible to application code.
    CtDispatchContext() noexcept = default;

    friend class CtTrace;
    friend class CtDispatcherBase;
    template <typename> friend class CtDispatcher;
    template <typename> friend class CtQueue; ///< records async phases during drain/overflow (§7.10 契约 #6)

    CtTracePhase m_phase = CtTracePhase::kDispatched;
    uint64_t m_receiverId = 0;
    uint64_t m_filterIndex = 0;
    std::chrono::steady_clock::duration m_elapsed{};
    std::size_t m_handlerCount = 0;
};

} // namespace crisp::event
