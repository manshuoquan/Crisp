#pragma once

// CtTask<T>: strong-typed, cancellable unit of work for CtThreadPool.
//
// Part of the CRISP threading subsystem (crisp::thread). Implements
// docs/threading/03-rewrite-proposal.md §3.2/§3.3:
//  - intrusive doubly-linked node embedded in the task (zero heap per task
//    for the node itself);
//  - atomic state machine kSubmitted -> kRunning -> kCompleted/kCancelled;
//  - cooperative cancellation via CtCancelToken + RAII owner handle;
//  - strong-typed result variant<T, std::exception_ptr> (replaces
//    QRunnable's void* type erasure, §1.3 principle #1).
//
// A CtTask is NON-movable and NON-copyable: its address is the node address
// (design §5 risk table). Tasks are owned via shared_ptr<CtTask<T>>.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <variant>

#include "event/ct_thread_affinity.h" // crisp::event::CtThreadId (reused)
#include "ct_thread_trace.h"    // CtTaskPhase, CtTaskRecord, CtTaskId

namespace crisp::thread {

// Forward: CtTaskNode stores a base pointer + shared holder.
class CtTaskBase;

/// Result of a CtTask: either the value T or the exception that escaped run().
/// Strong-typed replacement for QRunnable's implicit member-field results.
template <typename T>
using CtResult = std::variant<T, std::exception_ptr>;

/// Cooperative cancellation token handed to a running task. The task polls
/// isCancelled() at safe points and returns early if requested. No forced
/// thread kill (design §3.3 discipline).
///
/// LIFETIME (code-review SUGGEST): the token is only valid DURING the task
/// body's execution (it points at the task's own cancellation flag). It must
/// never escape run() (no storing, no capture into other threads).
class CtCancelToken {
public:
    explicit CtCancelToken(const std::atomic<bool>* cancelled) noexcept
        : m_cancelled(cancelled) {}

    /// True if the owning future/handle requested cancellation.
    bool isCancelled() const noexcept {
        return m_cancelled && m_cancelled->load(std::memory_order_relaxed);
    }

private:
    const std::atomic<bool>* m_cancelled = nullptr;
};

namespace detail {

/// Intrusive doubly-linked list node embedded in every CtTask (§3.2).
/// Owned by the task itself; the queue links/unlinks nodes without any
/// per-task heap allocation. The queue keeps a shared_ptr in `holder` for
/// as long as the node is linked (audit MUST-1: worker holds a reference
/// across runVoid + trace so the owner handle cannot free a running task).
struct CtTaskNode {
    CtTaskNode* prev = nullptr;
    CtTaskNode* next = nullptr;
    bool linked = false;              ///< true while the node is in the pool queue
    CtTaskBase* owner = nullptr;      ///< owning task (node -> task on pop/unlink)
    std::shared_ptr<CtTaskBase> holder; ///< queue-side reference (linked only)
};

} // namespace detail

/// Type-erased task interface: the pool and queue need only lifecycle + node
/// access + a run entry point. Concrete CtTask<T> derives from this.
class CtTaskBase {
public:
    virtual ~CtTaskBase() = default;

    virtual std::uint64_t priority() const noexcept = 0;
    virtual CtTaskId id() const noexcept = 0;
    virtual CtTaskPhase phase() const noexcept = 0;
    virtual bool isDone() const noexcept = 0;
    virtual bool cancelled() const noexcept = 0;
    virtual void requestCancel() noexcept = 0;
    virtual detail::CtTaskNode& node() noexcept = 0;

    /// Run the body with a cooperative token and store the result.
    /// Worker-side only (asserted). Called AFTER tryStart() succeeded.
    virtual void runVoid() = 0;

    /// Transition kSubmitted -> kRunning (CAS). Worker-side only.
    virtual bool tryStart() noexcept = 0;

    /// Transition kSubmitted -> kCancelled (CAS). Owner-handle side: succeeds
    /// only if the worker has not yet started the task (audit MUST-1: if the
    /// worker already CAS'd to kRunning, the handle must NOT overwrite the
    /// terminal state — it falls back to join semantics).
    virtual bool tryCancelFromSubmitted() noexcept = 0;

    virtual void finishCompleted() noexcept = 0;
    virtual void finishCancelled() noexcept = 0;

    /// Submit timestamp (us, steady clock) — set by the pool on submit so
    /// the worker can compute queued latency (§3.4). Base-level storage.
    std::uint64_t submitTimeUs = 0;

    /// Submitting thread OS id — set by the pool on submit for trace.
    crisp::event::CtThreadId submitThreadId = 0;
};

/// A cancellable unit of work with strong-typed result. Non-movable,
/// non-copyable; owned via shared_ptr (address == node address).
template <typename T>
class CtTask final : public CtTaskBase {
public:
    /// `fn` is the task body. The task is SUBMITTED (not queued) until pushed.
    template <typename Fn>
    explicit CtTask(Fn&& fn, std::uint64_t priority = 0)
        : m_fn([fn = std::decay_t<Fn>(std::forward<Fn>(fn))](CtCancelToken&) mutable {
              return CtResult<T>(fn());
          }),
          m_priority(priority) {}

    /// Task body with cooperative-token overload.
    template <typename Fn>
    CtTask(Fn&& fn, std::uint64_t priority, std::in_place_t /*token-aware*/)
        : m_fn([fn = std::decay_t<Fn>(std::forward<Fn>(fn))](CtCancelToken& tok) mutable {
              return CtResult<T>(fn(tok));
          }),
          m_priority(priority) {}

    CtTask(const CtTask&) = delete;
    CtTask& operator=(const CtTask&) = delete;
    CtTask(CtTask&&) = delete;
    CtTask& operator=(CtTask&&) = delete;
    ~CtTask() override = default;

    // ---- CtTaskBase ----
    std::uint64_t priority() const noexcept override { return m_priority; }
    CtTaskId id() const noexcept override { return m_taskId; }

    CtTaskPhase phase() const noexcept override {
        return static_cast<CtTaskPhase>(m_phase.load(std::memory_order_acquire));
    }

    bool isDone() const noexcept override {
        const auto p = phase();
        return p == CtTaskPhase::kCompleted || p == CtTaskPhase::kCancelled;
    }

    bool cancelled() const noexcept override {
        return m_cancelled.load(std::memory_order_acquire);
    }

    void requestCancel() noexcept override {
        m_cancelled.store(true, std::memory_order_release);
    }

    detail::CtTaskNode& node() noexcept override {
        m_node.owner = this; // back-pointer for node -> task (queue pops)
        return m_node;
    }

    void runVoid() override {
        assert(phase() == CtTaskPhase::kRunning &&
               "CtTask::runVoid must be called only by the worker after CAS");
        CtCancelToken token(&m_cancelled);
        try {
            m_result = m_fn(token); // fn returns CtResult<T> (variant)
        } catch (...) {
            m_result = CtResult<T>(std::current_exception());
        }
    }
    bool tryStart() noexcept override {
        CtTaskPhase expected = CtTaskPhase::kSubmitted;
        return m_phase.compare_exchange_strong(
            expected, CtTaskPhase::kRunning, std::memory_order_acq_rel);
    }

    bool tryCancelFromSubmitted() noexcept override {
        CtTaskPhase expected = CtTaskPhase::kSubmitted;
        return m_phase.compare_exchange_strong(
            expected, CtTaskPhase::kCancelled, std::memory_order_acq_rel);
    }

    void finishCompleted() noexcept override {
        {
            std::lock_guard<std::mutex> lock(m_doneMutex);
            m_phase.store(CtTaskPhase::kCompleted, std::memory_order_release);
        }
        m_doneCv.notify_all();
    }

    void finishCancelled() noexcept override {
        {
            std::lock_guard<std::mutex> lock(m_doneMutex);
            m_phase.store(CtTaskPhase::kCancelled, std::memory_order_release);
        }
        m_doneCv.notify_all();
    }

    /// Block until a terminal state (kCompleted/kCancelled), with timeout.
    /// Returns true if the task is done when the call returns. Uses the
    /// done condition variable (no busy-wait, design §3.1 form-B discipline).
    bool waitForDone(std::uint32_t timeoutMs = UINT32_MAX) {
        std::unique_lock<std::mutex> lock(m_doneMutex);
        if (timeoutMs == UINT32_MAX) {
            m_doneCv.wait(lock, [this] { return isDone(); });
        } else {
            m_doneCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                              [this] { return isDone(); });
        }
        return isDone();
    }

    // ---- typed accessors (consumer side) ----

    /// The task result: value or exception. Only valid when phase() ==
    /// kCompleted (asserts otherwise) AND while the owning handle/task is
    /// alive (code-review SUGGEST-2: the returned reference points into the
    /// task's own storage). No default-construction requirement on T
    /// (code-review WARN-3 fix).
    const CtResult<T>& result() const noexcept {
        assert(m_result.has_value() &&
               "CtTask::result before completion");
        return *m_result;
    }

    void setTaskId(CtTaskId id) noexcept { m_taskId = id; }

private:
    std::function<CtResult<T>(CtCancelToken&)> m_fn; ///< erased body
    std::atomic<CtTaskPhase> m_phase{CtTaskPhase::kSubmitted};
    std::atomic<bool> m_cancelled{false};
    std::optional<CtResult<T>> m_result; ///< worker-written (code-review WARN-3: no T default-construction requirement)
    detail::CtTaskNode m_node; ///< intrusive node (address stable)
    std::uint64_t m_priority = 0;
    CtTaskId m_taskId = 0;
    mutable std::mutex m_doneMutex;      ///< guards wait/notify (completion)
    std::condition_variable m_doneCv;    ///< completion notification (§3.1 B)
};

} // namespace crisp::thread