// ct_queue.cpp — non-template async routing machinery (M2).
//
// Part of the CRISP event subsystem (crisp::event). Implements the
// thread→queue lookup table (§10.1 contract #1/#7 routing): CtEventLoop
// registers its queue at setDispatcher time and unregisters at destruction;
// post/postTo look up the target queue here. Non-template mechanisms live
// in .cpp files (§5.3).

#include "ct_queue.h"

#include <unordered_map>

namespace crisp::event::detail {

namespace {

/// Thread→queue mapping (Rev 4, P1-6): a static unordered_map guarded by a
/// mutex. Registration happens on the loop's owning thread; lookups happen
/// on ARBITRARY sender threads — the mutex makes cross-thread routing safe.
std::unordered_map<CtThreadId, std::shared_ptr<CtQueueBase>>& threadQueues() {
    static std::unordered_map<CtThreadId, std::shared_ptr<CtQueueBase>> s_queues;
    return s_queues;
}

std::mutex& threadQueuesMutex() {
    static std::mutex s_mutex;
    return s_mutex;
}

} // namespace (anonymous)

void registerThreadQueue(CtThreadId tid,
                         const std::shared_ptr<CtQueueBase>& queue) {
    std::lock_guard<std::mutex> lock(threadQueuesMutex());
    threadQueues()[tid] = queue;
}

void unregisterThreadQueue(CtThreadId tid) {
    std::lock_guard<std::mutex> lock(threadQueuesMutex());
    threadQueues().erase(tid);
}

bool postErased(CtThreadId targetThread,
                std::unique_ptr<CtAnyEventBase> payload,
                std::weak_ptr<CtReceiver> target) {
    std::shared_ptr<CtQueueBase> queue;
    {
        std::lock_guard<std::mutex> lock(threadQueuesMutex());
        const auto it = threadQueues().find(targetThread);
        if (it == threadQueues().end())
            return false; // no loop on that thread (post side: no trace, §10.1 contract #3)
        queue = it->second;
    }
    // Hold a shared_ptr so the queue stays alive through the push — the
    // loop may be quitting concurrently on its own thread.
    return queue->pushAny(std::move(payload), std::move(target));
}

} // namespace crisp::event::detail
