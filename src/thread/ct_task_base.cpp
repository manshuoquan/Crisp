// CtTaskBase: completion-notifier mechanism (MT2 bridge hook).
//
// The notifier is fired at most once, on the worker thread, right after the
// terminal store — the event bridge wraps it in a postTo() to the target
// thread (design docs/threading/03 §3.1 rule 1: the worker never calls user
// code directly, it only posts).

#include "ct_task.h"

namespace crisp::thread {

void CtTaskBase::setCompletionNotifier(CtCompletionNotifier notifier) {
    // MUST fix (audit recheck): a notifier registered AFTER the task reached
    // terminal state must still fire — on the calling thread. The worker's
    // fireCompletion() unconditionally marks m_completionFired, so a naive
    // "if fired return" would silently drop late registrations (breaking the
    // bridge's "wire an existing future" contract and the fast-task
    // submit->register race). At most one fire is preserved: if the worker
    // already fired, m_completionFired is true and this registers nothing.
    bool runNow = false;
    {
        std::lock_guard<std::mutex> lock(m_completionMutex);
        if (m_completionFired)
            return; // worker already fired a (possibly different) notifier
        if (isDone()) {
            // Task terminal but notifier not yet fired (worker may be about
            // to call fireCompletion, or this thread sees terminal first):
            // mark fired and run THIS notifier on the calling thread.
            m_completionFired = true;
            runNow = true;
        } else {
            m_completion = std::move(notifier); // normal path: store for worker
        }
    }
    if (runNow && notifier) {
        // Mirror fireCompletion's exception policy (audit recheck WARN):
        // a throwing notifier on the late-registration path is also a
        // contract violation — swallow rather than propagate to the caller.
        try {
            notifier();
        } catch (...) {
        }
    }
}

void CtTaskBase::fireCompletion() {
    // Worker-side, AFTER the terminal store. At most one fire. If no
    // notifier is registered yet (fast task, registration still racing),
    // do NOT mark fired: a late setCompletionNotifier on the (already
    // terminal) task then fires immediately on its calling thread (audit
    // MUST fix — marking fired unconditionally silently dropped late
    // registrations).
    CtCompletionNotifier toRun;
    {
        std::lock_guard<std::mutex> lock(m_completionMutex);
        if (m_completionFired)
            return;
        if (!m_completion)
            return; // no notifier registered: leave unfired for a late one
        m_completionFired = true;
        toRun = std::move(m_completion);
    }
    if (toRun) {
        // audit W3: a throwing notifier (e.g. bad_alloc in the bridge's
        // postTo path) must NOT escape into workerLoop and terminate the
        // worker. Notifiers are cheap-and-non-blocking by contract; an
        // exception is treated as a contract violation and swallowed.
        try {
            toRun();
        } catch (...) {
            // Swallow: the delivery failure is recorded by the bridge (W1)
            // or by the task-side trace. Never propagate into the worker.
        }
    }
}

void CtTaskBase::fireCompletionFromOwnerCancel() {
    // Owner-cancel path (audit MUST): mirror fireCompletion — move the
    // notifier out under the lock, mark fired, invoke outside the lock with
    // exceptions swallowed. Called AFTER tryCancelFromSubmitted() succeeded
    // (task terminal kCancelled, worker will skip it).
    CtCompletionNotifier toRun;
    {
        std::lock_guard<std::mutex> lock(m_completionMutex);
        if (m_completionFired)
            return;
        if (!m_completion)
            return; // nothing wired; nothing to fire, no cycle to break
        m_completionFired = true;
        toRun = std::move(m_completion);
    }
    if (toRun) {
        try {
            toRun();
        } catch (...) {
            // Contract violation (notifiers must be cheap/non-blocking):
            // swallow — the bridge records delivery failure itself.
        }
    }
}

} // namespace crisp::thread
