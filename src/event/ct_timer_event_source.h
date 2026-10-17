#pragma once

// CtTimerEventSource / ManualEventSource: built-in platform event sources.
//
// Part of the CRISP event subsystem (crisp::event). Implements
// docs/event-system/04-event-system-redesign.md §7.12 contract #4. Both carry
// the EventSet type parameter (scheme A, Rev 10 P2-A): poll() asserts the
// queue's eventSetId() matches, downcasts to CtQueue<EventSet>&, then does
// a STRONG-TYPED push — the built-in sources never go through the erased
// pushAny boundary.

#include <chrono>
#include <cstdint>
#include <string_view>

#include "ct_event_source.h"   // CtEventSource
#include "ct_queue.h"          // CtQueueBase, CtQueue
#include "ct_event.h"          // CtEvent, CtEventOrigin
#include "ct_thread_affinity.h"

namespace crisp::event {

/// Payload of a framework timer event (§7.12 contract #4). Must be a member of
/// the EventSet the source is instantiated with (static_assert at use).
struct CtTimerPayload {
    int timerId = 0; ///< timer identity (assigned by the source)
};

} // namespace crisp::event

// Framework-provided traits specialization (§7.2): CtTimerPayload is a
// first-class framework type; applications only need it IN their EventSet.
CRISP_EVENT_TYPE(crisp::event::CtTimerPayload, "Timer");

namespace crisp::event {

/// Built-in periodic timer source. On each poll() past the deadline it
/// enqueues a CtEvent<CtTimerPayload> into the affined queue. Simple
/// poll-based timing (M2): poll() checks a steady_clock deadline; M4 may
/// switch to a platform wait. Not a real-time timer.
template <typename EventSet>
class CtTimerEventSource final : public CtEventSource {
public:
    static_assert(EventSet::template contains<CtTimerPayload>,
                  "CtTimerEventSource<EventSet> requires CtTimerPayload in "
                  "the event set");

    /// Create with an interval and (optional) timer id. Not started until
    /// activate() is called.
    explicit CtTimerEventSource(std::chrono::milliseconds interval,
                                int timerId = 0) noexcept
        : m_interval(interval), m_timerId(timerId) {}

    /// poll: assert queue.eventSetId() == CtTypeKey<EventSet>::value(),
    /// static_cast<CtQueue<EventSet>&>(queue), strong-typed push(...)
    /// (§7.12 contract #4, scheme A).
    void poll(CtQueueBase& queue) override {
        if (!m_active)
            return;

        const auto now = std::chrono::steady_clock::now();
        if (now < m_nextFire)
            return; // not yet

        // EventSet identity assert before the downcast (§7.12 contract #6).
        if (queue.eventSetId() != CtTypeKey<EventSet>::value()) {
            assert(false && "CtTimerEventSource polled against a queue of a "
                            "different EventSet");
            return;
        }

        auto& concrete = static_cast<CtQueue<EventSet>&>(queue);
        CtEvent<CtTimerPayload> event(CtEventOrigin::kSystem,
                                      CtTimerPayload{m_timerId});
        concrete.push(std::weak_ptr<CtReceiver>{}, std::move(event));
        m_nextFire = now + m_interval;
    }

    bool activate() override {
        if (m_active)
            return true; // idempotent
        m_active = true;
        m_nextFire = std::chrono::steady_clock::now() + m_interval;
        return true;
    }

    void deactivate() noexcept override { m_active = false; }

    std::string_view sourceName() const noexcept override { return "CtTimerEventSource"; }

private:
    std::chrono::milliseconds m_interval{};
    int m_timerId = 0;
    bool m_active = false;
    std::chrono::steady_clock::time_point m_nextFire{};
};

/// Manual/test event source: injects one pre-built event per poll() call.
/// Used by tests (§13.3 case 6) and by applications that want to inject
/// events from a non-loop thread through the source interface.
template <typename EventSet>
class ManualEventSource final : public CtEventSource {
public:
    static_assert(EventSet::template contains<CtTimerPayload>,
                  "ManualEventSource<EventSet> requires CtTimerPayload in "
                  "the event set");

    /// Create with the concrete event to enqueue on the next poll().
    /// (An event-set payload must be constructible at this site — use the
    /// timer payload for generality, or a payload the app defines.)
    explicit ManualEventSource(CtEvent<CtTimerPayload> event) noexcept
        : m_event(std::move(event)) {}

    /// poll: assert queue.eventSetId() == CtTypeKey<EventSet>::value(),
    /// static_cast<CtQueue<EventSet>&>(queue), strong-typed push(...).
    /// After one successful injection the source is exhausted (deactivates).
    void poll(CtQueueBase& queue) override {
        if (!m_active || m_injected)
            return;

        if (queue.eventSetId() != CtTypeKey<EventSet>::value()) {
            assert(false && "ManualEventSource polled against a queue of a "
                            "different EventSet");
            return;
        }

        auto& concrete = static_cast<CtQueue<EventSet>&>(queue);
        concrete.push(std::weak_ptr<CtReceiver>{}, std::move(m_event));
        m_injected = true;
        m_active = false; // exhausted after one injection
    }

    bool activate() override {
        if (m_injected)
            return false; // already consumed
        m_active = true;
        return true;
    }

    void deactivate() noexcept override { m_active = false; }

    std::string_view sourceName() const noexcept override { return "ManualEventSource"; }

private:
    CtEvent<CtTimerPayload> m_event; ///< the event to inject (once)
    bool m_active = false;
    bool m_injected = false;
};

} // namespace crisp::event
