// ct_trace.cpp — non-template implementation of CtTrace (no-lock ring).
//
// Part of the CRISP event subsystem (crisp::event). §5.3: non-template
// mechanisms live in .cpp files; the consumer interface is unchanged.

#include "ct_trace.h"

#include <cstdio>

namespace crisp::event {

void CtTrace::record(std::string_view typeName, CtTracePhase phase,
                     CtDispatchContext ctx, uint64_t objectId) {
    // Single-producer/single-consumer ring; calling thread is the owner.
    const std::uint64_t slot = m_head.load(std::memory_order_relaxed) & (kCapacity - 1);
    m_buffer[slot] = CtTraceRecord{
        std::chrono::steady_clock::now(), typeName, phase, objectId,
        m_writeSequence, ctx};
    ++m_writeSequence;
    // Publish the slot contents BEFORE advancing the head (release).
    m_head.store(m_head.load(std::memory_order_relaxed) + 1,
                 std::memory_order_release);
    m_count.fetch_add(1, std::memory_order_seq_cst);
}

std::uint64_t CtTrace::sequence() const noexcept {
    // Same ownership contract as record(): owner thread only.
    return m_count.load(std::memory_order_seq_cst);
}

const CtTraceRecord& CtTrace::at(std::size_t i) const noexcept {
    // Owner-thread diagnostics: index i is the i-th record written since
    // construction. The ring may have wrapped, so map the logical index
    // through the retained window (capacity at most kCapacity).
    const std::uint64_t head = m_head.load(std::memory_order_acquire);
    const std::size_t total = m_count.load(std::memory_order_seq_cst);
    const std::uint64_t start = total > kCapacity ? total - kCapacity : 0;
    return m_buffer[(start + i) & (kCapacity - 1)];
}

std::size_t CtTrace::count() const noexcept {
    return m_count.load(std::memory_order_seq_cst);
}

void CtTrace::dump() const {
    // Same ownership contract as record(): owner thread only, no locks.
    const std::uint64_t head = m_head.load(std::memory_order_acquire);
    const std::uint64_t total = m_count.load(std::memory_order_seq_cst);
    const std::uint64_t start = total > kCapacity ? total - kCapacity : 0;
    for (std::uint64_t i = start; i < total; ++i) {
        const CtTraceRecord& r = m_buffer[i & (kCapacity - 1)];
        std::printf("[%s] phase=%-10s obj=%-4llu seq=%-4llu\n",
                    r.typeName.data(), phaseName(r.phase),
                    static_cast<unsigned long long>(r.objectId),
                    static_cast<unsigned long long>(r.sequence));
    }
}

void CtTrace::clear() {
    m_head.store(0, std::memory_order_release);
    m_count.store(0, std::memory_order_seq_cst);
    m_writeSequence = 0;
}

const char* CtTrace::phaseName(CtTracePhase p) noexcept {
    switch (p) {
    case CtTracePhase::kDispatched: return "dispatch";
    case CtTracePhase::kFiltered:   return "filtered";
    case CtTracePhase::kDelivered:  return "delivered";
    case CtTracePhase::kObserved:   return "observed";
    case CtTracePhase::kQueued:     return "queued";
    case CtTracePhase::kDropped:    return "dropped";
    }
    return "unknown";
}

} // namespace crisp::event
