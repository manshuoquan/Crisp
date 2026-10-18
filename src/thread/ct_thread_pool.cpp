// CtThreadPool: worker loop, lifecycle, task trace (see ct_thread_pool.h).

#include "ct_thread_pool.h"

#include <cassert>
#include <chrono>

namespace crisp::thread {

void CtThreadPool::start(std::size_t n) {
    // MUST-1 fix: check->spawn->commit ALL under m_submitMutex. Two threads
    // calling start() concurrently can therefore not both spawn and swap
    // (the second sees running==true and returns). Spawning does not join,
    // so holding the lock across spawn cannot re-introduce the original
    // deadlock (which only happened when JOINING un-closed workers under the
    // lock).
    std::lock_guard<std::mutex> lock(m_submitMutex);
    if (m_running.load(std::memory_order_acquire))
        return; // idempotent: already running, no-op (also concurrent case)
    assert(!m_queue.closed() &&
           "CtThreadPool: restart after shutdown is not supported");
    if (m_queue.closed())
        return;

    if (n == 0) {
        const auto hw = std::thread::hardware_concurrency();
        n = (hw == 0) ? 1 : (hw < 8 ? hw : 8); // default cap: min(hw, 8)
    }
    std::vector<CtWorker> spawned;
    spawned.reserve(n);
    try {
        for (std::size_t i = 0; i < n; ++i)
            spawned.emplace_back(CtWorker{std::thread([this] { workerLoop(); })});
    } catch (...) {
        // MUST-2 fix: close the queue FIRST so the already-spawned workers
        // (blocked in waitForTask) wake up and exit; THEN join them. Without
        // close(), join would hang forever. After this, the pool is
        // permanently unusable (same semantics as "restart not supported").
        m_queue.close();
        for (auto& w : spawned)
            if (w.thread.joinable())
                w.thread.join();
        throw;
    }

    // Commit under the same lock: nothing can have raced us (MUST-1).
    m_running.store(true, std::memory_order_release);
    m_workers.swap(spawned);
}

void CtThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_submitMutex);
        if (!m_running.exchange(false, std::memory_order_acq_rel))
            return; // idempotent: not running
        m_queue.close(); // stop accepting; workers drain then exit
    }
    // Move the workers out under the SAME lock that workerCount() uses, so
    // the vector is never read/written unsynchronized (audit MUST fix: the
    // previous version cleared m_workers outside the lock — a data race with
    // a concurrent workerCount()).
    std::vector<CtWorker> workers;
    {
        std::lock_guard<std::mutex> lock(m_submitMutex);
        workers.swap(m_workers);
    }
    for (auto& w : workers)
        if (w.thread.joinable())
            w.thread.join();
}

CtThreadPool::~CtThreadPool() { shutdown(); }

void CtThreadPool::workerLoop() {
    const crisp::event::CtThreadId self = crisp::event::currentThreadId();
    for (;;) {
        std::shared_ptr<CtTaskBase> task;
        {
            m_queue.waitForTask();
            task = m_queue.tryPop();
            if (!task) {
                if (m_queue.closed())
                    return; // drained + closed: exit
                continue;   // spurious wakeup: re-wait
            }
        }
        // CAS kSubmitted -> kRunning (may fail if the owner unlinked +
        // cancelled concurrently — then skip it).
        if (!task->tryStart())
            continue;
        const std::uint64_t tStart = detail::nowUs();
        const std::uint64_t latencyUs = tStart - task->submitTimeUs;
        // kRunning trace record (code-review WARN-2): an in-flight task must
        // be distinguishable from a still-queued one (pain point #7).
        {
            CtTaskRecord running;
            running.phase = CtTaskPhase::kRunning;
            running.taskId = task->id();
            running.submitThread = task->submitThreadId;
            running.workerThread = self;
            running.queuedLatencyUs = latencyUs;
            running.runDurationUs = 0;
            running.cancelByOwner = false;
            m_trace.record(running);
        }
        task->runVoid();
        const std::uint64_t tEnd = detail::nowUs();
        // Cooperative cancellation: if requested while running, the terminal
        // state is kCancelled (design §3.3) regardless of body observation.
        const bool wasCancelled = task->cancelled();
        if (wasCancelled)
            task->finishCancelled();
        else
            task->finishCompleted();
        // MUST-1: snapshot the record BEFORE releasing the task reference —
        // finishCompleted/finishCancelled notify the owner handle, which may
        // drop its reference and free the task right after.
        CtTaskRecord rec;
        rec.phase = task->phase();
        rec.taskId = task->id();
        rec.submitThread = task->submitThreadId;
        rec.workerThread = self;
        rec.queuedLatencyUs = latencyUs;
        rec.runDurationUs = tEnd - tStart;
        rec.cancelByOwner = wasCancelled;
        m_trace.record(rec);
        // task (shared_ptr) released here; safe: record is a snapshot.
    }
}

void CtThreadPool::recordTaskTrace(CtTaskBase* task, CtTaskPhase phase,
                                   crisp::event::CtThreadId thread,
                                   std::uint64_t latencyUs,
                                   std::uint64_t runUs, bool cancelByOwner) {
    CtTaskRecord rec;
    rec.phase = phase;
    rec.taskId = task->id();
    rec.submitThread = task->submitThreadId;
    rec.workerThread = thread;
    rec.queuedLatencyUs = latencyUs;
    rec.runDurationUs = runUs;
    rec.cancelByOwner = cancelByOwner;
    m_trace.record(rec);
}

} // namespace crisp::thread