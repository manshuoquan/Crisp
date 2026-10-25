// ct_widget_trace.cpp — CtWidgetTrace bounded ring of widget trace records.
// Mutex-guarded (D3); mirror of crisp::thread::CtTaskTrace implementation.

#include "ct_widget_trace.h"

#include <mutex>

namespace crisp::ui {

bool CtWidgetTrace::record(const CtWidgetRecord& rec) {
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

std::size_t CtWidgetTrace::size() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count;
}

std::size_t CtWidgetTrace::copyOut(CtWidgetRecord* out, std::size_t max) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t n = (m_count < max) ? m_count : max;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t slot = (m_head + i) % kCapacity;
        out[i] = m_ring[slot];
    }
    return n;
}

} // namespace crisp::ui
