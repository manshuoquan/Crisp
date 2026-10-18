#pragma once

// CtThreadPool: shared priority task queue + N worker threads (MT1).
//
// Part of the CRISP threading subsystem (crisp::thread). Implements
// docs/threading/03-rewrite-proposal.md §3.5:
//  - start(N): spawn N workers draining a shared intrusive priority queue;
//  - submit(fn): strong-typed task with cooperative cancellation;
//  - shutdown(): idempotent; workers drain queued tasks then exit; the pool
//    destructor always joins all workers;
//  - default worker count min(hardware_concurrency, 8), configurable via
//    start(N);
//  - explicit queue cap aligned with the event subsystem's kMaxQueued=8192
//    discipline: overflow REJECTS the task (returns nullptr handle) and
//    records the rejection in the task trace.
//
// QThreadPool analog (see docs/threading/01 §3.2). Zero Qt dependency.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "ct_thread.h"
#include "event/ct_thread_affinity.h" // crisp::event::CtThreadId
#include "ct_thread_trace.h"    // CtTaskPhase, CtTaskRecord, CtTaskId
#include "ct_task.h"            // CtTask<T>, CtTaskBase, CtResult<T>, CtCancelToken
#include "ct_task_queue.h"      // CtTaskQueue

namespace crisp::thread {

// Forward: CtTaskHandle stores a raw pool pointer for unlink-on-destruction.
class CtThreadPool;

namespace detail {

/// Global task id counter (monotonic, §3.4).
inline std::atomic<CtTaskId>& taskIdCounter() {
    static std::atomic<CtTaskId> s_counter{0};
    return s_counter;
}

/// Steady-clock microseconds (for queued-latency / run-duration trace).
inline std::uint64_t nowUs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace detail

/// RAII handle to a submitted task. Move-only. Destruction:
///  - if the task is still queued (kSubmitted), unlinks it from the pool
///    queue and marks kCancelled (design §3.2 holder-destructor protocol);
///  - if the task is running (kRunning), requests cooperative cancellation
///    and blocks until it finishes (join semantics);
///  - if done, no-op.
/// The handle keeps the task alive via shared_ptr for the pool's lifetime.
///
/// THREAD CONTRACT (audit WARN): destroying a handle whose task is running
/// BLOCKS until completion. A worker must NEVER destroy such a handle from
/// inside its own task body (it would wait for itself and deadlock). Destroy
/// handles from the owner/submitting thread only.
template <typename T>
class CtTaskHandle {
public:
    CtTaskHandle() noexcept = default;
    ~CtTaskHandle();

    CtTaskHandle(CtTaskHandle&& other) noexcept;
    CtTaskHandle& operator=(CtTaskHandle&& other) noexcept;
    CtTaskHandle(const CtTaskHandle&) = delete;
    CtTaskHandle& operator=(const CtTaskHandle&) = delete;

    /// True if this handle owns a task.
    bool valid() const noexcept { return static_cast<bool>(m_task); }

    /// Request cooperative cancellation (any thread). Idempotent.
    void cancel() noexcept;

    /// Block until the task reaches a terminal state (no busy-wait; the
    /// worker notifies via condition variable on completion). Timeout in
    /// ms (0 = no wait, returns current done-state).
    bool waitForDone(std::uint32_t timeoutMs = UINT32_MAX);

    /// Current task lifecycle phase (delegates to the owned task).
    /// A null handle reports kSubmitted (documented; NOT kCancelled, which
    /// would misleadingly claim the task was cancelled — code-review SUGGEST).
    CtTaskPhase phase() const noexcept {
        return m_task ? m_task->phase() : CtTaskPhase::kSubmitted;
    }

    /// True if the owned task reached a terminal state.
    bool isDone() const noexcept {
        return m_task ? m_task->isDone() : true;
    }

    /// True if cancellation was requested.
    bool isCancelled() const noexcept {
        return m_task ? m_task->cancelled() : false;
    }

    /// Access the owned task (bridge hook; CtFuture registers the completion
    /// notifier through it). Internal use only.
    const std::shared_ptr<CtTask<T>>& task() const noexcept { return m_task; }

    /// The task result. Valid only after waitForDone()/isDone().
    const CtResult<T>& result() const;

    /// Task id.
    CtTaskId id() const noexcept;

private:
    friend class CtThreadPool;
    /// Pool-side construction: handle owns the shared task and a weak ref
    /// to the pool (to unlink on destruction while queued).
    explicit CtTaskHandle(std::shared_ptr<CtTask<T>> task,
                          CtThreadPool* pool)
        : m_task(std::move(task)), m_pool(pool) {}

    std::shared_ptr<CtTask<T>> m_task; ///< sole owner via shared_ptr (address stable)
    CtThreadPool* m_pool = nullptr;    ///< unlink target (valid while pool lives)
};

/// Worker thread state (internal, pool-owned).
struct CtWorker {
    std::thread thread; ///< the worker thread
};

/// The shared thread pool (MT1 §3.5). Owns the queue + workers.
///
/// LIFETIME CONTRACT (audit WARN-3): the pool MUST outlive every
/// CtTaskHandle it produced. Destroying a handle while the pool is being
/// concurrently destroyed is UB (the handle's unlink path dereferences the
/// pool). Sequential use is safe: the pool destructor drains the queue, so
/// every task reaches a terminal state before the pool goes away.
class CtThreadPool {
public:
    /// Creates a pool with NO workers; call start(N) to begin accepting.
    CtThreadPool() = default;
    ~CtThreadPool(); ///< always shutdown() + join all workers

    CtThreadPool(const CtThreadPool&) = delete;
    CtThreadPool& operator=(const CtThreadPool&) = delete;

    /// Spawn N worker threads draining the shared queue. Idempotent: a
    /// second call while running is a no-op (including concurrent calls —
    /// the whole spawn+commit holds m_submitMutex). N=0 uses the default cap
    /// (min(hardware_concurrency, 8)). Restart after shutdown() is not
    /// supported (asserts in debug).
    ///
    /// EXCEPTION CONTRACT: if spawning throws (system_error/bad_alloc), the
    /// pool is left permanently unusable (the queue is closed, workers that
    /// were already spawned are joined and exited) — same semantics as
    /// "restart not supported" (code-review SUGGEST).
    void start(std::size_t n = 0);

    /// True after start() and before shutdown().
    bool running() const noexcept { return m_running.load(std::memory_order_acquire); }

    /// Submit a task that returns T. Returns a handle owning the task
    /// (cancellation + wait + result). Returns an INVALID handle (nullptr
    /// task) if the pool is not running or the queue is full (rejected;
    /// recorded in the task trace as kDropped with cancelByOwner=false —
    /// see §3.5 overflow discipline / CtTaskPhase enum).
    template <typename T, typename Fn>
    CtTaskHandle<T> submit(Fn&& fn, std::uint64_t priority = 0);

    /// Submit a cooperative task: `fn` receives a CtCancelToken and can
    /// poll isCancelled() (design §3.3). Pass `std::in_place` as the third
    /// argument to select this overload.
    template <typename T, typename Fn>
    CtTaskHandle<T> submit(Fn&& fn, std::uint64_t priority, std::in_place_t);

    /// Shutdown: stop accepting new tasks, let workers drain queued tasks,
    /// then join all workers. Idempotent.
    void shutdown();

    /// Current number of queued (not yet started) tasks.
    std::size_t queuedCount() const noexcept { return m_queue.size(); }

    /// Number of live worker threads. Locked read (safe vs concurrent
    /// shutdown clearing the vector — audit WARN fix).
    std::size_t workerCount() const noexcept {
        std::lock_guard<std::mutex> lock(m_submitMutex);
        return m_workers.size();
    }

    /// Queue capacity (overflow rejects beyond this). Default 8192 (aligned
    /// with crisp::event CtQueue kMaxQueued discipline, docs §5 risk table).
    std::size_t maxQueued() const noexcept { return kMaxQueued; }

    /// Task trace ring (MT1 §3.4); copy records out for diagnostics/tests.
    std::size_t copyTaskTrace(CtTaskRecord* out, std::size_t max) const {
        return m_trace.copyOut(out, max);
    }

    /// Direct access to the task trace ring (bridge failure records, audit
    /// W1: the bridge installs itself as a failure sink).
    CtTaskTrace& taskTrace() noexcept { return m_trace; }

    /// Unlink a queued task (owner-handle destruction path, §3.2).
    /// Public: CtTaskHandle's destructor calls this via its pool pointer.
    bool unlinkQueued(CtTaskBase* task) { return m_queue.unlink(task); }

    /// Record an owner-side cancellation trace (handle destructor / cancel()
    /// unlink path, §3.4: cancelled tasks must be traceable).
    void recordOwnerCancellation(CtTaskBase* task) {
        CtTaskRecord rec;
        rec.phase = CtTaskPhase::kCancelled;
        rec.taskId = task->id();
        rec.submitThread = task->submitThreadId;
        rec.workerThread = 0;
        rec.queuedLatencyUs = 0;
        rec.runDurationUs = 0;
        rec.cancelByOwner = true;
        m_trace.record(rec);
    }

private:
    /// Worker main loop: pop -> CAS kSubmitted->kRunning -> runVoid ->
    /// finishCompleted/finishCancelled; records task trace segments.
    void workerLoop();

    /// Record a task-side trace entry (phase + timings). Aggregates the
    /// submit-side and worker-side segments by taskId (§3.4).
    void recordTaskTrace(CtTaskBase* task, CtTaskPhase phase,
                         crisp::event::CtThreadId thread,
                         std::uint64_t latencyUs, std::uint64_t runUs,
                         bool cancelByOwner);

    static constexpr std::size_t kMaxQueued = 8192; ///< overflow reject cap

    CtTaskQueue m_queue;                        ///< shared priority queue
    std::vector<CtWorker> m_workers;             ///< worker threads
    std::atomic<bool> m_running{false};         ///< accepting submits
    mutable std::mutex m_submitMutex;           ///< guards start/shutdown/submit (mutable: workerCount is const)
    CtTaskTrace m_trace;                        ///< task trace ring (MT1 §3.4)
};

// ---- CtTaskHandle implementation (needs pool unlink on destruction) ----

template <typename T>
CtTaskHandle<T>::~CtTaskHandle() {
    if (!m_task)
        return;
    // MUST-1 fix: the queue holds a shared reference while the task is
    // linked, and the worker holds one across runVoid + trace, so this
    // destructor cannot free a task that is queued or running — at worst it
    // cancels a still-queued task (CAS kSubmitted->kCancelled) and unlinks
    // it; if the worker already started it, we fall back to join semantics.
    const auto p = m_task->phase();
    if (p == CtTaskPhase::kSubmitted) {
        // CAS kSubmitted -> kCancelled: succeeds only if the worker has NOT
        // yet started. On success, unlink from the queue (owner-side, §3.2).
        if (m_task->tryCancelFromSubmitted()) {
            // Owner-cancel path: fire the notifier here (audit MUST — the
            // worker will skip the task and never fire it), breaking the
            // task->notifier->task cycle and delivering the kCancelled done
            // event to any wired future.
            m_task->fireCompletionFromOwnerCancel();
            if (m_pool) {
                m_pool->unlinkQueued(m_task.get());
                m_pool->recordOwnerCancellation(m_task.get());
            }
        } else {
            // Worker won the CAS (kRunning): join semantics.
            m_task->requestCancel();
            m_task->waitForDone(UINT32_MAX);
        }
    } else if (p == CtTaskPhase::kRunning) {
        // Running: cooperative cancel + join semantics (wait for completion).
        m_task->requestCancel();
        m_task->waitForDone(UINT32_MAX); // blocks until terminal state
    }
}

template <typename T>
CtTaskHandle<T>::CtTaskHandle(CtTaskHandle&& other) noexcept
    : m_task(std::move(other.m_task)), m_pool(other.m_pool) {
    other.m_pool = nullptr; // MUST-2: move must transfer the pool too
}

template <typename T>
CtTaskHandle<T>& CtTaskHandle<T>::operator=(CtTaskHandle&& other) noexcept {
    if (this != &other) {
        // True copy-and-swap: `other` is moved into tmp; then we swap so that
        // `this` keeps the incoming task and tmp (which now holds OUR old
        // task) destructs at scope end — running the cancel/unlink/join
        // protocol on the old task (audit WARN fix: the previous "move out of
        // tmp" version silently abandoned the old task).
        CtTaskHandle<T> tmp(std::move(other));
        std::swap(m_task, tmp.m_task);
        std::swap(m_pool, tmp.m_pool);
    }
    return *this;
}

template <typename T>
void CtTaskHandle<T>::cancel() noexcept {
    if (!m_task)
        return;
    // Cooperative cancellation. For a still-queued task, also try to
    // transition kSubmitted -> kCancelled and unlink it so the worker will
    // skip it (design §3.2 "cancelled/unlinked tasks are skipped"). If the worker already
    // started it, the running task cooperatively observes the flag.
    if (m_task->phase() == CtTaskPhase::kSubmitted &&
        m_task->tryCancelFromSubmitted()) {
        // Owner-cancel path: fire the notifier (audit MUST — worker skips
        // the task, so this is the only chance to deliver kCancelled and
        // break the task->notifier->task cycle).
        m_task->fireCompletionFromOwnerCancel();
        if (m_pool) {
            m_pool->unlinkQueued(m_task.get());
            m_pool->recordOwnerCancellation(m_task.get());
        }
        return;
    }
    m_task->requestCancel();
}

template <typename T>
bool CtTaskHandle<T>::waitForDone(std::uint32_t timeoutMs) {
    if (!m_task)
        return true; // null handle is "done"
    return m_task->waitForDone(timeoutMs);
}

template <typename T>
const CtResult<T>& CtTaskHandle<T>::result() const {
    assert(m_task && "CtTaskHandle::result on null handle");
    return m_task->result();
}

template <typename T>
CtTaskId CtTaskHandle<T>::id() const noexcept {
    return m_task ? m_task->id() : 0;
}

// ---- CtThreadPool::submit ----

template <typename T, typename Fn>
CtTaskHandle<T> CtThreadPool::submit(Fn&& fn, std::uint64_t priority) {
    std::lock_guard<std::mutex> lock(m_submitMutex);
    if (!m_running.load(std::memory_order_acquire) || m_queue.size() >= kMaxQueued)
        return CtTaskHandle<T>{}; // invalid (rejected/not running)

    auto task = std::make_shared<CtTask<T>>(std::forward<Fn>(fn), priority);
    task->setTaskId(++detail::taskIdCounter());
    task->submitTimeUs = detail::nowUs();
    task->submitThreadId = crisp::event::currentThreadId();
    // kSubmitted trace (submit side).
    recordTaskTrace(task.get(), CtTaskPhase::kSubmitted,
                    crisp::event::currentThreadId(), 0, 0, false);
    if (!m_queue.push(task)) {
        // Overflow/closed: reject — record kDropped (audit recheck WARN:
        // kDropped covers "rejected (overflow)" per the enum comment, not
        // kCancelled which implies owner/collaborative cancellation).
        recordTaskTrace(task.get(), CtTaskPhase::kDropped,
                        crisp::event::currentThreadId(), 0, 0, false);
        return CtTaskHandle<T>{};
    }
    return CtTaskHandle<T>{task, this};
}

template <typename T, typename Fn>
CtTaskHandle<T> CtThreadPool::submit(Fn&& fn, std::uint64_t priority,
                                     std::in_place_t) {
    std::lock_guard<std::mutex> lock(m_submitMutex);
    if (!m_running.load(std::memory_order_acquire) || m_queue.size() >= kMaxQueued)
        return CtTaskHandle<T>{}; // invalid (rejected/not running)

    auto task = std::make_shared<CtTask<T>>(std::forward<Fn>(fn), priority,
                                            std::in_place);
    task->setTaskId(++detail::taskIdCounter());
    task->submitTimeUs = detail::nowUs();
    task->submitThreadId = crisp::event::currentThreadId();
    recordTaskTrace(task.get(), CtTaskPhase::kSubmitted,
                    crisp::event::currentThreadId(), 0, 0, false);
    if (!m_queue.push(task)) {
        // Overflow/closed: reject — kDropped (see first overload).
        recordTaskTrace(task.get(), CtTaskPhase::kDropped,
                        crisp::event::currentThreadId(), 0, 0, false);
        return CtTaskHandle<T>{};
    }
    return CtTaskHandle<T>{task, this};
}

} // namespace crisp::thread
