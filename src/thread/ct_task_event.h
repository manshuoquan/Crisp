#pragma once

// CtTaskDone<T>: task-completion payload for the thread -> event bridge (MT2).
//
// Part of the CRISP threading subsystem (crisp::thread). Carries the task
// result (value or exception) from the worker thread to the target thread's
// CtEventLoop. The user adds this payload to their own CtEventSet and binds
// a handler; the bridge (below) posts CtEvent<CtTaskDone<T>> to the target
// thread when a future completes (design docs/threading/03 §3.1).
//
// Value semantics; cheap to move (optional<variant<T, exception_ptr>>).

#include <exception>
#include <optional>
#include <variant>

#include "ct_task.h" // CtResult<T>, CtTaskPhase

namespace crisp::thread {

/// Completion payload: the task's result, delivered to the target thread.
/// phase tells whether the task completed or was cancelled. result is
/// populated only for kCompleted (nullopt for kCancelled).
template <typename T>
struct CtTaskDone {
    CtTaskPhase phase = CtTaskPhase::kCompleted; ///< terminal phase
    std::uint64_t taskId = 0;                    ///< originating task id
    std::optional<CtResult<T>> result;           ///< value or exception
};

} // namespace crisp::thread
