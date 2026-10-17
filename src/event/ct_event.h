#pragma once

// CtEvent: type-safe value-semantics event.
//
// Part of the CRISP project. Design principles: type safety first, explicit
// ownership, built-in observability. See docs/event-system/04-event-system-redesign.md.

#include <cstdint>
#include <string_view>
#include <tuple>        // (Rev 7, P2-2) enable_if uses std::tuple_element_t
#include <type_traits>
#include <utility>

namespace crisp::event {

/// Lifecycle phases of an event during its journey through the system.
/// (Rev 7, P1-3) Lives in ct_event.h — BOTH ct_dispatch_context.h and
/// ct_trace.h need it; moving it here keeps the include graph acyclic.
/// v0 had 4 phases; we add the asynchronous phases kQueued/kDropped.
enum class CtTracePhase : uint8_t {
    kDispatched, ///< event entered the dispatcher (sync path)
    kFiltered,   ///< event was intercepted by a filter (kFiltered result)
    kDelivered,  ///< event reached a receiver handler
    kObserved,   ///< observer callback fired
    kQueued,     ///< event accepted into a CtQueue (async path, §10)
    kDropped,    ///< event dropped by queue overflow / expired receiver (§10)
};

/// Where an event originated. Renamed from v0's `enum class CtEventSource`
/// so that the name CtEventSource is free for the abstract platform source
/// interface (see ct_event_source.h, §7.12).
enum class CtEventOrigin {
    kSystem,      ///< produced by the framework/platform layer (timers, io...)
    kApplication, ///< produced by application code
};

/// Compile-time metadata for payload type T.
/// The primary template is intentionally INVALID — every payload type MUST
/// specialize it via CRISP_EVENT_TYPE. Forgetting is a compile error, not a
/// runtime surprise.
template <typename T>
struct CtEventTraits {
    static constexpr bool kIsValid = false; ///< must be true in a specialization
    static constexpr std::string_view name = "<unspecialized>";
};

namespace detail {

/// First element of a type pack, or a dummy tag for the EMPTY pack.
/// (M2 fix) Avoids std::tuple_element<0, tuple<>> — MinGW 8.1's libstdc++
/// instantiates it OUTSIDE SFINAE, breaking the CtEvent ctor's enable_if
/// during any std::variant default-constructibility probe. Partial
/// specialization on the empty pack keeps the empty case a well-formed,
/// substitution-friendly type.
struct FirstArgNone {};

template <typename... Args>
struct FirstArg;

template <typename A, typename... Rest>
struct FirstArg<A, Rest...> {
    using type = std::decay_t<A>; ///< decay: handles reference folding (Args&&)
};

template <>
struct FirstArg<> {
    using type = FirstArgNone;
};

/// `Args...` → first arg type, or FirstArgNone for the empty pack.
template <typename... Args>
using firstArgType = typename FirstArg<Args...>::type;

} // namespace detail

/// Type-safe event value. Payload immutable after construction; the
/// dispatch state (accepted) is MUTABLE during delivery (§8.1 Step 6);
/// value semantics.
/// The payload T must have a specialized CtEventTraits (compile-time).
template <typename T>
class CtEvent {
public:
    static_assert(CtEventTraits<T>::kIsValid,
                  "CtEvent<T> requires CRISP_EVENT_TYPE(T, ...)");

    using payload_type = T;

    CtEvent() = delete; ///< events always carry a payload

    /// Construct in place with the given origin (default: application).
    template <typename... Args>
    explicit CtEvent(CtEventOrigin origin, Args&&... args)
        : m_payload(std::forward<Args>(args)...), m_origin(origin) {}

    /// In-place payload construction from the event set/variant path.
    /// (Rev 5, N-8 + Rev 6, P2-4) enable_if disambiguates from the
    /// origin-first overload for BOTH single- and multi-arg forms: the
    /// generic overload is excluded whenever the FIRST argument is a
    /// CtEventOrigin, so CtEvent<T>(CtEventOrigin::kApplication[, args...])
    /// always resolves to the origin-first overload and never forwards the
    /// origin into T's constructor.
    ///
    /// (M2 fix) The original enable_if used
    /// `std::tuple_element_t<0, std::tuple<Args...>>`, which MinGW 8.1's
    /// libstdc++ INSTANTIATES even when `sizeof...(Args) >= 1` is false —
    /// `tuple_element<0, tuple<>>` triggers its static_assert OUTSIDE the
    /// SFINAE context, so any `std::variant` default-constructibility probe
    /// (is_default_constructible<CtEvent<T>> instantiates this ctor with
    /// `Args = {}`) fails to compile. detail::firstArgType avoids
    /// instantiating tuple_element for empty packs via partial
    /// specialization.
    template <typename... Args,
              typename = std::enable_if_t<!(
                  sizeof...(Args) >= 1 &&
                  std::is_same_v<detail::firstArgType<Args...>, CtEventOrigin>)>>
    explicit CtEvent(Args&&... args)
        : m_payload(std::forward<Args>(args)...) {}

    CtEvent(const CtEvent&) = default;
    CtEvent& operator=(const CtEvent&) = default;
    // Conditional noexcept (audit SUGGEST fix): an unconditional noexcept
    // move would std::terminate if the payload's move throws. Condition on
    // the payload's move noexcept-ness.
    CtEvent(CtEvent&&) noexcept(std::is_nothrow_move_constructible_v<T>) = default;
    CtEvent& operator=(CtEvent&&) noexcept(std::is_nothrow_move_assignable_v<T>) = default;

    /// The strongly-typed payload (read-only view).
    const T& payload() const noexcept { return m_payload; }

    /// The origin of the event (renamed from `source`).
    CtEventOrigin origin() const noexcept { return m_origin; }

    /// Monotonic sequence number assigned at dispatch time (see §8.2).
    uint64_t sequence() const noexcept { return m_sequence; }

    /// Optional user-defined ordering hint; 0 means "no hint" (see §10.3).
    uint64_t priority() const noexcept { return m_priority; }

    /// Set the ordering hint BEFORE posting/sending (Rev 6, P2-1: the
    /// constructors forward only to T's payload ctor, so a public setter is
    /// the application-facing way to produce a non-zero priority that §10.3
    /// queue ordering can act on. Must be called before dispatch/queue push;
    /// the dispatcher/queue stamp it into CtPendingEvent at push time).
    void setPriority(uint64_t prio) noexcept { m_priority = prio; }

    /// Mark the event as accepted (consumed) — called by a handler during
    /// delivery (§8.1 Step 6). v0 API preserved (§7.3 契约 #6).
    void accept() { accepted = true; }

    /// Mark the event as not accepted — v0 API preserved.
    void ignore() { accepted = false; }

    /// The accepted flag (v0 kept as a PUBLIC member — see 契约 #6).
    bool accepted = false;

private:
    // (Rev 5, P2-2 / Rev 7, P1-1) §8.1/§8.2 Step 3's event.setSequence(...)
    // executes inside CtDispatcher<EventSet>::send/broadcast — a TEMPLATE
    // DERIVED class. C++ friendship does NOT inherit: CtDispatcherBase being
    // a friend does NOT extend to CtDispatcher<EventSet>, so the templated
    // dispatcher MUST be a friend too. CtQueue's push path also stamps
    // sequence into the pending event. setPriority is PUBLIC (Rev 6, P2-1);
    // only setSequence stays private (the producer must never forge a
    // sequence number).
    friend class CtDispatcherBase;
    template <typename> friend class CtDispatcher;
    template <typename> friend class CtQueue;

    /// Assign the dispatch sequence number (dispatcher/queue only; §8.2).
    void setSequence(uint64_t seq) noexcept { m_sequence = seq; }

    T m_payload; ///< value semantics, no heap allocation
    CtEventOrigin m_origin{CtEventOrigin::kApplication};
    uint64_t m_sequence = 0; ///< assigned by the dispatcher, not the producer
    uint64_t m_priority = 0; ///< ordering hint for queues (public setPriority)
};

/// Result of a synchronous dispatch call.
enum class CtDispatchResult {
    kHandled,    ///< at least one handler accepted the event
    kUnhandled,  ///< no handler matched or none accepted
    kFiltered,   ///< a filter intercepted the event (§8.1 Step 5)
    kNoReceiver, ///< broadcast found no live receiver (informational)
};

} // namespace crisp::event
