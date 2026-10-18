#pragma once

// CtTaskPhase + task-side trace records for the CRISP threading subsystem.
//
// Part of the CRISP threading subsystem (crisp::thread). This enum is
// deliberately INDEPENDENT from crisp::event::CtTracePhase (design doc
// docs/threading/03 §3.4): extending the event enum would change its
// uint8_t value set and affect the event subsystem's ABI/spec. Task-side
// records live in their own ring and are correlated with event-side traces
// by timestamp at the MT2 event bridge.

#include <atomic>
#include <cstdint>
#include <mutex>

#include "event/ct_thread_affinity.h" // crisp::event::CtThreadId

namespace crisp::thread {

/// Task lifecycle phases (MT1, §3.4). Independent from the event trace enum.
enum class CtTaskPhase : uint8_t {
    kSubmitted = 0, ///< task accepted into the pool queue
    kRunning   = 1, ///< picked up by a worker (CAS kSubmitted -> kRunning)
    kCompleted = 2, ///< run() returned without cancellation
    kCancelled = 3, ///< cancelled while queued, or cooperatively while running
};

/// One task-side trace record (§3.4). Fields are filled by the submitting
/// thread (kSubmitted) and the worker thread (kRunning/kCompleted/kCancelled);
/// the two segments are aggregated by taskId at completion.
struct CtTaskRecord {
    CtTaskPhase phase = CtTaskPhase::kSubmitted;
    std::uint64_t taskId = 0;        ///< globally unique task id
    crisp::event::CtThreadId submitThread = 0; ///< submitting thread
    crisp::event::CtThreadId workerThread = 0; ///< executing worker (0 if queued)
    std::uint64_t queuedLatencyUs = 0; ///< submit -> running latency (us)
    std::uint64_t runDurationUs = 0;   ///< running -> completed/cancelled (us)
    bool cancelByOwner = false;        ///< cancellation source (owner/other)
};

/// Globally unique task id (monotonic counter, MT1 §3.4).
using CtTaskId = std::uint64_t;

/// Fixed-capacity ring buffer of task trace records (§3.4).
/// Thread-safe (mutex-guarded; trace writes are NOT the hot path — the
/// zero-allocation hot path is the queue itself). Dequeues on overflow
/// (oldest dropped), keeping the ring bounded.
class CtTaskTrace {
public:
    /// Record one task trace entry. Returns false if the ring is full and
    /// the oldest entry was dropped (overflow).
    bool record(const CtTaskRecord& rec);

    /// Number of records currently stored.
    std::size_t size() const noexcept;

    /// Copy the stored records out (diagnostics / tests).
    std::size_t copyOut(CtTaskRecord* out, std::size_t max) const;

    static constexpr std::size_t kCapacity = 4096; ///< bounded ring (power of 2)

private:
    mutable std::mutex m_mutex;
    std::size_t m_head = 0;               ///< next write slot
    std::size_t m_count = 0;              ///< valid records (<= kCapacity)
    CtTaskRecord m_ring[kCapacity]{};     ///< ring storage
};

} // namespace crisp::thread