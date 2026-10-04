#pragma once

// CtThreadAffinity: named, data-carrying "must run on THIS thread" contract.
//
// Part of the CRISP event subsystem (crisp::event). Turns the "thread
// ownership" concept from a comment into a typed, assertable value. Owned by
// CtReceiver and CtQueue; consulted by CtDispatcher::send/post/postTo.

#include <cassert>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace crisp::event {

/// Opaque, numeric thread id used across the event system.
/// Wraps std::thread::id but stores it as a 64-bit integer so it can be
/// compared, logged and traced cheaply (std::thread::id is not guaranteed
/// to be numeric). 0 == "no thread / unset".
using CtThreadId = std::uint64_t;

/// Convert the current thread's OS id to a CtThreadId.
/// (Rev 3/Rev 4) Platform API: Windows/MinGW uses GetCurrentThreadId()
/// (stable, deterministic, O(1), no mapping table). Cross-platform builds
/// keep the interface; the implementation switches per platform.
inline CtThreadId currentThreadId() noexcept {
#ifdef _WIN32
    return static_cast<CtThreadId>(::GetCurrentThreadId());
#else
    return 0; // platform-specific mapping (M4)
#endif
}

/// A named, data-carrying "must run on THIS thread" contract.
///
/// Owned by CtReceiver and CtQueue. Records the creating thread and exposes
/// two checks:
///  - isOnThread():  cheap, non-fatal test (used by post/postTo routing);
///  - assertSameThread(): fatal in debug builds (used by send / pop / exec).
///
/// In release builds assertSameThread() degrades to a return value so the
/// caller can fail gracefully (see §8.2 归属规则).
class CtThreadAffinity {
public:
    /// Records currentThreadId() at construction time.
    CtThreadAffinity() noexcept : m_threadId(currentThreadId()) {}

    CtThreadAffinity(const CtThreadAffinity&) = delete;
    CtThreadAffinity& operator=(const CtThreadAffinity&) = delete;

    /// The thread recorded at construction.
    CtThreadId thread() const noexcept { return m_threadId; }

    /// True if the calling thread == the recorded thread (cheap compare).
    bool isOnThread() const noexcept { return currentThreadId() == m_threadId; }

    /// Assert the calling thread owns this affinity. Debug: abort. Release:
    /// no-op returning false so callers can degrade to kUnhandled.
    /// Returns true if the calling thread is the owner.
    bool assertSameThread() const noexcept {
        const bool same = (currentThreadId() == m_threadId);
        assert(same && "CtThreadAffinity: thread-affinity violation");
        return same;
    }

    /// Re-bind to the current thread (used when a receiver is deliberately
    /// moved between threads — see §10.2 edge case). Only permitted when no
    /// registrations are pending; callers must guarantee that.
    void rebindToCurrentThread() noexcept { m_threadId = currentThreadId(); }

private:
    CtThreadId m_threadId = 0; ///< 0 = unset (never after ctor)
};

} // namespace crisp::event
