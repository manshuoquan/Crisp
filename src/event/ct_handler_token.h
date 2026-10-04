#pragma once

// CtHandlerToken: move-only RAII handle for a registration in a CtDispatcher.
//
// Part of the CRISP event subsystem (crisp::event). Destroying the token
// unregisters the handler. Tokens are move-only; copying is impossible
// because two owners cannot share one registration.

#include <atomic>
#include <cstdint>
#include <memory>

// (Rev 7, P1-4) detail::CtTokenState holds std::weak_ptr<CtDispatcherBase>
// BEFORE any declaration of that name appears in this header (the friend
// declaration inside CtHandlerToken comes later). A forward declaration is
// required for the weak_ptr member to compile.
namespace crisp::event {
class CtDispatcherBase;
}

namespace crisp::event {

/// Globally unique registration id (monotonic counter, see §6.1).
using CtHandlerId = std::uint64_t;

namespace detail {

/// Shared state linking a handler token to its owning dispatcher.
/// Created on registration; alive=false once the dispatcher is gone.
/// (Rev 2, P2-10) `owner` is a weak_ptr: a token destroyed on an arbitrary
/// thread lock()s it before calling unregisterHandler, so the dispatcher
/// stays alive through the unregister call.
struct CtTokenState {
    std::atomic<bool> alive{true};         ///< false once the dispatcher is destroyed
    std::weak_ptr<CtDispatcherBase> owner; ///< owning dispatcher (weak, Rev 2)
    CtHandlerId id = 0;                    ///< registration id (global)
};

} // namespace detail

/// Move-only RAII handle for a registration in a CtDispatcher.
///
/// Invariants:
///  - at most one live token per registration (no copies);
///  - a token outliving its dispatcher is safe: the dispatcher's destructor
///    flips alive=false, the token's destructor then skips unregister;
///  - destroying a token unregisters the handler atomically (COW publish).
class CtHandlerToken {
public:
    CtHandlerToken() noexcept = default; ///< null token
    ~CtHandlerToken() noexcept;          ///< unregisters if still alive

    /// Transfers ownership; the source becomes a null token.
    CtHandlerToken(CtHandlerToken&& other) noexcept : m_state(std::move(other.m_state)) {}

    /// (Rev 12, P3-2) Move assignment: if the target already owns a LIVE
    /// registration, unregister it FIRST (equivalent to destroy-then-
    /// move-construct), then take over the source's state.
    CtHandlerToken& operator=(CtHandlerToken&& other) noexcept;

    CtHandlerToken(const CtHandlerToken&) = delete; ///< one owner only
    CtHandlerToken& operator=(const CtHandlerToken&) = delete;

    /// True if this token currently owns a live registration.
    bool valid() const noexcept { return static_cast<bool>(m_state); }

    /// The unique registration id (0 for a null token).
    CtHandlerId id() const noexcept { return m_state ? m_state->id : 0; }

private:
    // (Rev 8, P1-2) Friendship does NOT inherit: the non-empty token is
    // constructed inside CtDispatcher<EventSet>::addHandler/addFilter/
    // addObserver (template derived class, §7.8), so the TEMPLATED dispatcher
    // must be a friend too — exactly the same gap fixed for CtEvent in §7.3.
    friend class CtDispatcherBase;
    template <typename> friend class CtDispatcher;

    /// Construct a token owning `state` (dispatcher/registration path only;
    /// application code cannot create a live token — the ctor is private).
    explicit CtHandlerToken(std::shared_ptr<detail::CtTokenState> state) noexcept
        : m_state(std::move(state)) {}

    std::shared_ptr<detail::CtTokenState> m_state; ///< shared with dispatcher
};

/// Registration priority for filters (and future queue ordering, §10.3).
/// Filters with HIGHER priority run FIRST; equal priority keeps
/// registration order (stable by CtHandlerId). M1 handlers: this value is
/// stored in the table (§6.5) but does NOT reorder handler delivery —
/// handlers always run in registration order (v0 semantics preserved).
enum class CtHandlerPriority : int {
    kHigh   = 100,
    kNormal = 0,
    kLow    = -100,
};

} // namespace crisp::event
