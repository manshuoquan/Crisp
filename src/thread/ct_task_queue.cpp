// CtTaskQueue: intrusive priority queue (see ct_task_queue.h).

#include "ct_task_queue.h"

namespace crisp::thread {

namespace {

void linkBefore(detail::CtTaskNode* node, detail::CtTaskNode* next) {
    node->prev = next->prev;
    node->next = next;
    next->prev->next = node;
    next->prev = node;
    node->linked = true;
}

void unlinkNode(detail::CtTaskNode* node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = node->next = nullptr;
    node->linked = false;
}

CtTaskBase* nodeOwner(detail::CtTaskNode* node) { return node->owner; }

} // namespace

bool CtTaskQueue::push(const std::shared_ptr<CtTaskBase>& task) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed)
            return false; // closed: reject (caller keeps ownership)
        // Priority insertion: find the insertion point after all tasks with
        // priority >= task's. Fast path: if the queue is empty or the new
        // task's priority is <= the last task's, append at the tail (O(1)).
        if (m_count == 0 || nodeOwner(m_tail.prev)->priority() >= task->priority()) {
            task->node().holder = task;
            task->node().owner = task.get();
            linkBefore(&task->node(), &m_tail);
        } else {
            detail::CtTaskNode* cur = m_head.next;
            while (cur != &m_tail && nodeOwner(cur)->priority() >= task->priority())
                cur = cur->next;
            task->node().holder = task;
            task->node().owner = task.get();
            linkBefore(&task->node(), cur);
        }
        ++m_count;
    }
    m_cv.notify_one();
    return true;
}

std::shared_ptr<CtTaskBase> CtTaskQueue::tryPop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_head.next == &m_tail)
        return nullptr; // empty
    detail::CtTaskNode* node = m_head.next;
    unlinkNode(node);
    --m_count;
    // Move the queue's reference into the caller's shared_ptr; the task
    // node's holder is cleared on unlink (below) so the task can be freed
    // once the caller drops its reference.
    auto task = std::move(node->holder);
    node->holder.reset();
    node->owner = nullptr;
    return task;
}

bool CtTaskQueue::unlink(CtTaskBase* task) {
    std::lock_guard<std::mutex> lock(m_mutex);
    detail::CtTaskNode& n = task->node();
    if (!n.linked)
        return false;
    unlinkNode(&n);
    --m_count;
    n.holder.reset();
    n.owner = nullptr;
    return true;
}

std::size_t CtTaskQueue::size() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count;
}

bool CtTaskQueue::empty() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count == 0;
}

void CtTaskQueue::close() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
    }
    m_cv.notify_all();
}

bool CtTaskQueue::closed() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_closed;
}

void CtTaskQueue::waitForTask() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return m_closed || m_head.next != &m_tail; });
}

void CtTaskQueue::notifyAll() { m_cv.notify_all(); }

} // namespace crisp::thread