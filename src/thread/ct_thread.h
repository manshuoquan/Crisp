#pragma once

// CtThread: RAII wrapper around std::thread (C++17, no std::jthread).
//
// Part of the CRISP threading subsystem (crisp::thread). Owns a std::thread
// and guarantees it is joined at destruction (never left joinable). Mirrors
// the event subsystem's "ownership explicit" principle: a CtThread object is
// the single owner of the underlying OS thread.

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>

#include "event/ct_thread_affinity.h" // crisp::event::CtThreadId (reused)

namespace crisp::thread {

/// RAII wrapper owning one std::thread.
///
/// Invariants:
///  - the wrapped thread is always joined at destruction (a joinable thread
///    is never leaked);
///  - move-only: two CtThread objects cannot own the same OS thread;
///  - the thread's OS id lives in a shared atomic control block: the worker
///    writes it, the owner reads it (race-free, audit MUST-4/MUST-5 fix).
///
/// QThread analog: a minimal lifecycle-safe QThread replacement (MT1 scope;
/// no moveToThread / event-loop integration — that belongs to the MT2 event
/// bridge).
class CtThread {
public:
    /// Constructs a detached-from-execution thread handle (no thread).
    CtThread() noexcept = default;

    /// Starts a thread running `fn`; the thread's OS id is recorded in the
    /// shared control block so callers can query which thread ran the
    /// function (cheap compare, race-free).
    template <typename Fn>
    explicit CtThread(Fn&& fn);

    /// Joins the owned thread (if any). Blocking; MUST be called from a
    /// thread that is not the owned one (joining self is UB in std::thread).
    ~CtThread();

    CtThread(CtThread&& other) noexcept;
    CtThread& operator=(CtThread&& other) noexcept;

    CtThread(const CtThread&) = delete;
    CtThread& operator=(const CtThread&) = delete;

    /// True if this object owns a running/completed-not-yet-joined thread.
    bool joinable() const noexcept { return m_thread.joinable(); }

    /// The recorded OS thread id (0 if never started). Atomic read; safe
    /// from any thread after start().
    crisp::event::CtThreadId threadId() const noexcept {
        return m_threadId ? m_threadId->load(std::memory_order_acquire) : 0;
    }

    /// Blocks until the owned thread finishes; joins it. Safe to call once.
    void join();

private:
    /// Shared atomic id: the worker lambda captures the control block (NOT
    /// `this`), so moving/destroying the CtThread object never leaves a
    /// dangling write target (audit MUST-4 fix).
    std::shared_ptr<std::atomic<crisp::event::CtThreadId>> m_threadId;
    std::thread m_thread;
};

template <typename Fn>
CtThread::CtThread(Fn&& fn)
    : m_threadId(std::make_shared<std::atomic<crisp::event::CtThreadId>>(0)) {
    const auto idBlock = m_threadId; // capture the block, not `this`
    // decay the callable: a by-value copy (audit WARN fix — capturing a
    // reference to a caller's left-value callable would dangle if the caller
    // outlives the CtThread).
    using Decayed = std::decay_t<Fn>;
    m_thread = std::thread([idBlock, fn = Decayed(std::forward<Fn>(fn))]() mutable {
        idBlock->store(crisp::event::currentThreadId(), std::memory_order_release);
        fn();
    });
}

} // namespace crisp::thread