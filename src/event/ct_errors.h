#pragma once

// CtEventError: unified, type-safe error expression for the event system.
//
// Part of the CRISP event subsystem (crisp::event). Dispatch RESULT
// (CtDispatchResult, §7.3) is informational; ERRORS (category + message) are
// exceptional. The two are never mixed (CtDispatchResult::kNoReceiver is
// information, not a kReceiverGone error).

#include <cstdint>
#include <string>
#include <string_view>

namespace crisp::event {

/// Error categories for the event system. Extensible via kOther.
enum class CtEventErrorKind : uint8_t {
    kNone,          ///< no error (default)
    kTypeNotInSet,  ///< T not in the dispatcher's EventSet (programming error)
    kReceiverGone,  ///< weak_ptr<CtReceiver> expired before delivery
    kQueueOverflow, ///< CtQueue dropped an event (kDropped trace)
    kWrongThread,   ///< thread-affinity violation (assert failed in release)
    kNoQueue,       ///< no CtEventLoop/CtQueue for the target thread
    kSourceFailure, ///< CtEventSource failed to activate/poll
    kOther,         ///< application-defined failure
};

/// Value-type error descriptor. Cheap to copy; never on the hot path
/// (errors are exceptional).
struct CtEventError {
    CtEventErrorKind kind{CtEventErrorKind::kNone};
    std::string message;   ///< human-readable detail (may be empty)
    uint64_t sequence = 0; ///< related event sequence (§8.2)

    /// True iff kind != kNone.
    explicit operator bool() const noexcept { return kind != CtEventErrorKind::kNone; }
};

/// Convenience alias for "wrong-thread" errors: the old CtAffinityError name
/// maps to CtEventError{kind = kWrongThread}.
using CtAffinityError = CtEventError;

/// Convenience alias for "no target queue" errors: the old CtNoQueueError
/// name maps to CtEventError{kind = kNoQueue}.
using CtNoQueueError = CtEventError;

/// Free helper: build an error quickly.
inline CtEventError makeError(CtEventErrorKind kind, std::string_view message) {
    return CtEventError{kind, std::string(message)};
}

} // namespace crisp::event
