#pragma once

// CtTaskQueue: intrusive priority queue for CtThreadPool workers.
//
// Part of the CRISP threading subsystem (crisp::thread). Implements
// docs/threading/03-rewrite-proposal.md §3.2:
//  - intrusive doubly-linked list (nodes embedded in CtTask, zero per-task
//    allocation for the node itself);
//  - priority insertion (higher priority first, FIFO among equals);
//  - O(1) unlink on cancellation;
//  - the queue lock protects ONLY the list structure; workers unlink then
//    release the lock before running the body (lock-order protocol §3.2).
//
// Type-erased: the queue stores CtTaskBase pointers (mixed task types).

#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>

#include "ct_task.h" // CtTaskBase, detail::CtTaskNode

namespace crisp::thread {

/// Intrusive priority queue operating on CtTaskBase nodes. Thread-safe for
/// push / tryPop / unlink; the mutex guards the list structure only.
class CtTaskQueue {
public:
    CtTaskQueue() {
        // Sentinel ring: empty queue = head.next == &tail && tail.prev == &head.
        m_head.next = &m_tail;
        m_tail.prev = &m_head;
    }
    CtTaskQueue(const CtTaskQueue&) = delete;
    CtTaskQueue& operator=(const CtTaskQueue&) = delete;

    /// Push a task. The queue takes a shared reference (the node lives as
    /// long as the task is linked). Returns false if closed (caller keeps
    /// ownership).
    bool push(const std::shared_ptr<CtTaskBase>& task);

    /// Pop the highest-priority pending task, or nullptr if empty.
    /// Returns a shared reference: the caller (worker) holds it across
    /// runVoid + trace recording so the owner handle cannot free the task
    /// while it is running (audit MUST-1 fix).
    std::shared_ptr<CtTaskBase> tryPop();

    /// Unlink a node (cancellation path). Returns true if it was still linked.
    bool unlink(CtTaskBase* task);

    /// Number of linked tasks (approximate; diagnostics).
    std::size_t size() const noexcept;

    /// True if no linked tasks.
    bool empty() const noexcept;

    /// Close the queue: no more pushes accepted; existing tasks drain.
    void close();

    /// True after close().
    bool closed() const noexcept;

    /// Block until a task is available or the queue is closed.
    /// Spurious wakeups allowed (caller re-checks).
    void waitForTask();

    /// Wake all waiters unconditionally (shutdown path).
    void notifyAll();

private:
    detail::CtTaskNode m_head{}; ///< sentinel head; next = first task
    detail::CtTaskNode m_tail{}; ///< sentinel tail; prev = last task
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_closed = false;
    std::size_t m_count = 0; ///< linked count (under m_mutex)
};

} // namespace crisp::thread

