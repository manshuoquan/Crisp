// CtThread: RAII wrapper around std::thread (see ct_thread.h).

#include "ct_thread.h"

namespace crisp::thread {

CtThread::~CtThread() {
    // Ownership-explicit principle: never leave a joinable thread behind.
    if (m_thread.joinable())
        m_thread.join();
}

CtThread::CtThread(CtThread&& other) noexcept
    : m_threadId(std::move(other.m_threadId)), m_thread(std::move(other.m_thread)) {}

CtThread& CtThread::operator=(CtThread&& other) noexcept {
    if (this != &other) {
        if (m_thread.joinable())
            m_thread.join();
        m_thread = std::move(other.m_thread);
        m_threadId = std::move(other.m_threadId);
    }
    return *this;
}

void CtThread::join() {
    if (m_thread.joinable())
        m_thread.join();
    // Keep m_threadId: the recorded OS id remains queryable after join.
}

} // namespace crisp::thread