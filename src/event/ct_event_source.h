#pragma once

// CtEventSource: abstract platform event source (M2 async core).
//
// Part of the CRISP event subsystem (crisp::event). Implements
// docs/event-system/04-event-system-redesign.md §7.12. A source produces
// CtEvents and injects them into a CtQueue — NEVER into a dispatcher or
// receiver directly (§4.1 layering rule #3). Qt analog:
// QAbstractEventDispatcher / QTimer / QSocketNotifier.
//
// NOTE: the v0 enum `CtEventSource` was renamed `CtEventOrigin` (§7.3);
// this class owns the name from here on.

#include <cstdint>
#include <functional>
#include <string_view>

#include "ct_queue.h"             // CtQueueBase (poll signature)
#include "ct_thread_affinity.h"   // CtThreadId

namespace crisp::event {

/// Abstract platform event source. A source produces CtEvents and injects
/// them into a CtQueue (never into a dispatcher or receiver directly).
class CtEventSource {
public:
    virtual ~CtEventSource() = default;

    /// One shot: check for pending platform events; enqueue any found.
    /// Called by CtEventLoop each iteration (polling model, M2).
    virtual void poll(CtQueueBase& queue) = 0;

    /// Arm/activate the source (e.g. start a timer, watch a socket).
    /// Returns false if activation is not possible on this platform.
    virtual bool activate() = 0;

    /// Deactivate; stop producing events.
    virtual void deactivate() noexcept = 0;

    /// The thread this source must run on (usually the loop's thread).
    CtThreadId affinityThread() const noexcept { return m_affinityThread; }

    /// Short human-readable source name for traces.
    virtual std::string_view sourceName() const noexcept = 0;

protected:
    /// (Rev 12, P2-B) Set the affinity thread; called by CtEventLoop::
    /// attachSource on success (§7.11). Derived sources may also call it if
    /// they know their thread in advance.
    void setAffinityThread(CtThreadId tid) noexcept { m_affinityThread = tid; }

    friend class CtEventLoop; ///< (Rev 12, P2-B) attachSource sets affinity

    CtThreadId m_affinityThread = 0; ///< set by the loop when attaching
};

} // namespace crisp::event
