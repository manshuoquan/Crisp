#pragma once

// CtFuture<T>: cancellable future with completion-callback registration (MT2).
//
// Part of the CRISP threading subsystem (crisp::thread). Implements
// docs/threading/03-rewrite-proposal.md §3.1 (form A onThread + form B
// waitReady) and §4 MT2 acceptance:
//  - cancel() / isCancelled(): cooperative cancellation (delegates to the
//    task's CtCancelToken flag);
//  - waitReady(): block until terminal (no busy-wait; completion CV);
//  - onThread(threadId, cb): register a completion callback delivered to the
//    target thread's CtEventLoop via the event bridge (ct_task_event.h).
//
// The future registers a completion NOTIFIER on the task; the worker fires
// it (once) at terminal state, and the event bridge wraps the callback in a
// postTo() to the target thread — the worker never calls user code directly.

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "event/ct_thread_affinity.h" // crisp::event::CtThreadId (reused)
#include "ct_task.h"            // CtResult<T>, CtTaskPhase, CtCompletionNotifier
#include "ct_thread_pool.h"     // CtTaskHandle<T> (ownership)

namespace crisp::thread {

/// Cancellable task future (MT2 §3.1). Wraps a CtTaskHandle<T> and adds
/// completion-callback registration. Move-only.
template <typename T>
class CtFuture {
public:
    CtFuture() noexcept = default;

    /// Adopt a task handle from the pool.
    explicit CtFuture(CtTaskHandle<T> handle) : m_handle(std::move(handle)) {}

    CtFuture(CtFuture&&) = default;
    CtFuture& operator=(CtFuture&&) = default;
    CtFuture(const CtFuture&) = delete;
    CtFuture& operator=(const CtFuture&) = delete;
    ~CtFuture() = default;

    /// True if this future owns a task.
    bool valid() const noexcept { return m_handle.valid(); }

    /// Request cooperative cancellation (any thread). Idempotent.
    void cancel() noexcept { m_handle.cancel(); }

    /// True if cancellation was requested.
    bool isCancelled() const noexcept { return m_handle.isCancelled(); }

    /// Current task phase.
    CtTaskPhase phase() const noexcept { return m_handle.phase(); }

    /// Block until terminal (form B; condition-variable wait, no busy-wait).
    /// Returns true if the future reached a terminal state.
    bool waitReady(std::uint32_t timeoutMs = UINT32_MAX) {
        return m_handle.waitForDone(timeoutMs);
    }

    /// The task result (value or exception). Valid only after waitReady().
    const CtResult<T>& result() const { return m_handle.result(); }

    /// Task id.
    std::uint64_t id() const noexcept { return m_handle.id(); }

    /// Register a completion notifier (bridge hook, §3.4). Fired at most
    /// once, on the WORKER thread, at terminal state. If the task is already
    /// terminal, fires immediately on the calling thread.
    /// NOTE: low-level hook for the event bridge; application code should
    /// use the bridge's onThread() (which wraps this + postTo).
    void setNotifier(CtCompletionNotifier notifier);

    /// Access the underlying task handle (bridge hook; internal use only).
    const CtTaskHandle<T>& handle() const noexcept { return m_handle; }

private:
    CtTaskHandle<T> m_handle;
};

// ---- implementation ----

template <typename T>
void CtFuture<T>::setNotifier(CtCompletionNotifier notifier) {
    if (!m_handle.valid())
        return;
    // Delegate to the task's completion-notifier slot (fires on the worker
    // at terminal, or immediately if already terminal — the task handles the
    // already-terminal case internally).
    m_handle.task()->setCompletionNotifier(std::move(notifier));
}

} // namespace crisp::thread