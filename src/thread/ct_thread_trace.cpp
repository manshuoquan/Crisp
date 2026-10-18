// CtTaskTrace: bounded ring of task trace records (see ct_thread_trace.h).

#include "ct_thread_trace.h"

#include <mutex>

namespace crisp::thread {

bool CtTaskTrace::record(const CtTaskRecord& rec) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_count == kCapacity) {
        // Overflow: drop the OLDEST (head points at it) and reuse the slot.
        m_ring[m_head] = rec;
        m_head = (m_head + 1) % kCapacity;
        return false; // dropped one
    }
    const std::size_t slot = (m_head + m_count) % kCapacity;
    m_ring[slot] = rec;
    ++m_count;
    return true;
}

std::size_t CtTaskTrace::size() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count;
}

std::size_t CtTaskTrace::copyOut(CtTaskRecord* out, std::size_t max) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t n = (m_count < max) ? m_count : max;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t slot = (m_head + i) % kCapacity;
        out[i] = m_ring[slot];
    }
    return n;
}

} // namespace crisp::thread
