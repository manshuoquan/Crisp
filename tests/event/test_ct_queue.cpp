// Unit tests for the M2 async core (CtQueue, CtEventLoop, sources, post).
//
// Part of the CRISP event subsystem (crisp::event). Covers §13.3 contracts:
// queue ordering determinism, affinity assertions, post accept semantics,
// broadcast/directed pending records, the timer source, and the manual
// source. Hand-written test doubles only (NO mock framework).

#include "event/ct_queue.h"
#include "event/ct_event_loop.h"
#include "event/ct_timer_event_source.h"
#include "event/ct_dispatcher.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "test_harness.h"

namespace {
using namespace crisp::event;

struct MousePayload {
    int x = 0;
    int y = 0;
};

struct KeyPayload {
    int key = 0;
};

} // namespace (anonymous)

CRISP_EVENT_TYPE(MousePayload, "Mouse");
CRISP_EVENT_TYPE(KeyPayload, "Key");

namespace {
using namespace crisp::event;

using TestEventSet = CtEventSet<MousePayload, KeyPayload, CtTimerPayload>;
using TestDispatcher = CtDispatcher<TestEventSet>;
using TestQueue = CtQueue<TestEventSet>;

class TestReceiver : public CtReceiver {
public:
    int mouseCalls = 0;
    int keyCalls = 0;
    int timerCalls = 0;
};

std::shared_ptr<TestReceiver> makeReceiver() {
    return createReceiver<TestReceiver>();
}

// --- queue ordering determinism (§13.3 #1) ---------------------------------

TEST_CASE(m2_queue_same_priority_keeps_insertion_order)
{
    TestQueue queue;

    // Same priority (0 = no hint): FIFO order must be preserved.
    CtEvent<MousePayload> ev1(CtEventOrigin::kApplication, MousePayload{1, 0});
    CtEvent<MousePayload> ev2(CtEventOrigin::kApplication, MousePayload{2, 0});
    CtEvent<MousePayload> ev3(CtEventOrigin::kApplication, MousePayload{3, 0});

    EXPECT_EQ(queue.push(std::weak_ptr<CtReceiver>{}, std::move(ev1)), true);
    EXPECT_EQ(queue.push(std::weak_ptr<CtReceiver>{}, std::move(ev2)), true);
    EXPECT_EQ(queue.push(std::weak_ptr<CtReceiver>{}, std::move(ev3)), true);
    EXPECT_EQ(queue.size(), 3u);

    CtPendingEventBase* pe = nullptr;
    EXPECT_EQ(queue.front(pe), true);
    EXPECT_EQ(pe->sequence(), 1u);

    auto* any1 = static_cast<TestEventSet::any_type*>(pe->payloadPtr());
    auto* m1 = std::get_if<CtEvent<MousePayload>>(any1);
    EXPECT_TRUE(m1 != nullptr);
    EXPECT_EQ(m1->payload().x, 1);

    queue.popFront(pe);
    EXPECT_EQ(queue.front(pe), true);
    auto* m2 = std::get_if<CtEvent<MousePayload>>(
        static_cast<TestEventSet::any_type*>(pe->payloadPtr()));
    EXPECT_EQ(m2->payload().x, 2);

    queue.popFront(pe);
    EXPECT_EQ(queue.front(pe), true);
    auto* m3 = std::get_if<CtEvent<MousePayload>>(
        static_cast<TestEventSet::any_type*>(pe->payloadPtr()));
    EXPECT_EQ(m3->payload().x, 3);

    queue.popFront(pe);
    EXPECT_EQ(queue.front(pe), false); // empty
}

TEST_CASE(m2_queue_higher_priority_pops_first)
{
    TestQueue queue;

    CtEvent<MousePayload> low(CtEventOrigin::kApplication, MousePayload{10, 0});
    low.setPriority(1);   // lower hint
    CtEvent<MousePayload> high(CtEventOrigin::kApplication, MousePayload{20, 0});
    high.setPriority(100); // higher hint
    CtEvent<MousePayload> mid(CtEventOrigin::kApplication, MousePayload{15, 0});
    mid.setPriority(50);

    queue.push(std::weak_ptr<CtReceiver>{}, std::move(low));
    queue.push(std::weak_ptr<CtReceiver>{}, std::move(high));
    queue.push(std::weak_ptr<CtReceiver>{}, std::move(mid));

    CtPendingEventBase* pe = nullptr;
    auto xOf = [&]() {
        auto* any = static_cast<TestEventSet::any_type*>(pe->payloadPtr());
        auto* m = std::get_if<CtEvent<MousePayload>>(any);
        return m ? m->payload().x : -1;
    };

    // Expected order: high(20) → mid(15) → low(10).
    EXPECT_EQ(queue.front(pe), true);
    EXPECT_EQ(xOf(), 20);
    queue.popFront(pe);

    EXPECT_EQ(queue.front(pe), true);
    EXPECT_EQ(xOf(), 15);
    queue.popFront(pe);

    EXPECT_EQ(queue.front(pe), true);
    EXPECT_EQ(xOf(), 10);
    queue.popFront(pe);

    EXPECT_EQ(queue.front(pe), false);
}

// --- affinity assertion (§13.3 #2) -----------------------------------------

TEST_CASE(m2_queue_front_on_wrong_thread_asserts_fails_release_degrades)
{
    // Release (NDEBUG) build: assertSameThread returns false, front degrades.
    TestQueue queue;
    CtPendingEventBase* pe = nullptr;

    bool otherThreadCalledFront = false;
    std::thread t([&] {
        otherThreadCalledFront = queue.front(pe); // wrong thread (release: false)
    });
    t.join();

    // In a release build front() returns false on the wrong thread.
    // In a debug build the assert aborts — this test is only meaningful
    // under NDEBUG; both behaviors are acceptable (the assertion is the
    // contract). We assert the release contract here.
    EXPECT_EQ(otherThreadCalledFront, false);
}

// --- post accept semantics (§13.3 #3) --------------------------------------

TEST_CASE(m2_post_accepts_into_loop_queue_and_drains_to_handler)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    auto receiver = makeReceiver();

    CtHandlerToken token = dispatcher->addHandler<MousePayload>(
        receiver, [&](CtEvent<MousePayload>& ev) {
            receiver->mouseCalls++;
            ev.accept();
        });

    CtEventLoop loop;
    loop.attachToCurrentThread(); // THIS thread hosts the loop
    loop.setDispatcher(dispatcher);

    // post runs on the same thread here; the queue is registered by
    // setDispatcher, so post must be accepted.
    CtEvent<MousePayload> ev(CtEventOrigin::kApplication, MousePayload{7, 9});
    const CtDispatchResult r = dispatcher->post(receiver, std::move(ev));
    EXPECT_EQ(r, CtDispatchResult::kHandled);

    // Drain manually (exec would block): loop.exec processes one batch and
    // returns (m_quit was never set, but the queue drains in the first
    // iteration and no sources block — exec returns after one iteration
    // because there is nothing left to do... exec() loops until quit; we
    // must quit it from another thread or use drainOnce-like behavior).
    // Simplest deterministic path: quit after one drain via a thread.
    // For the unit test, drive the queue directly through the loop's queue()
    // and the dispatcher's dispatchAny is exercised by exec.
    EXPECT_EQ(receiver->mouseCalls, 0); // not yet drained

    // Drain via the loop on THIS thread with a short exec window.
    std::thread quitThread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        loop.quit();
    });
    loop.exec();
    quitThread.join();

    EXPECT_EQ(receiver->mouseCalls, 1);
    EXPECT_EQ(loop.queue().size(), 0u);
}

TEST_CASE(m2_post_without_loop_returns_unhandled)
{
    // A dispatcher with NO loop attached: routing finds no queue → kUnhandled.
    auto dispatcher = std::make_shared<TestDispatcher>();
    auto receiver = makeReceiver();

    CtEvent<MousePayload> ev(CtEventOrigin::kApplication, MousePayload{1, 1});
    const CtDispatchResult r = dispatcher->post(receiver, std::move(ev));
    EXPECT_EQ(r, CtDispatchResult::kUnhandled);
}

// --- broadcast/directed pending flag (§13.3 #4) ----------------------------

TEST_CASE(m2_pending_broadcast_flag_fixed_at_push)
{
    TestQueue queue;

    // Directed (non-empty target): isBroadcast() must be false.
    auto receiver = makeReceiver();
    CtEvent<MousePayload> directed(CtEventOrigin::kApplication, MousePayload{1, 1});
    queue.push(std::weak_ptr<CtReceiver>(receiver), std::move(directed));

    CtPendingEventBase* pe = nullptr;
    EXPECT_EQ(queue.front(pe), true);
    EXPECT_EQ(pe->isBroadcast(), false);
    EXPECT_TRUE(!pe->target().expired());
    queue.popFront(pe);

    // Broadcast (empty target): isBroadcast() must be true.
    CtEvent<MousePayload> broadcast(CtEventOrigin::kApplication, MousePayload{2, 2});
    queue.push(std::weak_ptr<CtReceiver>{}, std::move(broadcast));

    EXPECT_EQ(queue.front(pe), true);
    EXPECT_EQ(pe->isBroadcast(), true);
    EXPECT_TRUE(pe->target().expired()); // empty weak_ptr
    queue.popFront(pe);
}

TEST_CASE(m2_expired_receiver_dropped_not_delivered)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    CtEventLoop loop;
    loop.attachToCurrentThread();
    loop.setDispatcher(dispatcher);

    int calls = 0;
    CtHandlerToken token;
    {
        auto receiver = makeReceiver();
        token = dispatcher->addHandler<MousePayload>(
            receiver, [&](CtEvent<MousePayload>&) { ++calls; });

        CtEvent<MousePayload> ev(CtEventOrigin::kApplication, MousePayload{1, 1});
        EXPECT_EQ(dispatcher->post(receiver, std::move(ev)), CtDispatchResult::kHandled);
    } // receiver destroyed before drain

    std::thread quitThread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        loop.quit();
    });
    loop.exec();
    quitThread.join();

    // Directed + expired target: dispatchAny records kDropped, handler not called.
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(loop.queue().size(), 0u);
}

// --- timer source (§13.3 #5) -----------------------------------------------

TEST_CASE(m2_timer_source_injects_on_poll)
{
    // Attach the source through the loop (sets affinity, §7.12 contract #5).
    TestQueue queue;
    auto source = std::make_shared<CtTimerEventSource<TestEventSet>>(
        std::chrono::milliseconds(1), 42);
    CtEventLoop loop;
    loop.attachToCurrentThread();
    EXPECT_EQ(loop.attachSource(source), true);

    EXPECT_EQ(source->activate(), true);

    // poll() after the deadline enqueues one timer event (simulate the
    // loop's exec iteration by calling poll directly on the affined queue).
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    source->poll(queue);

    CtPendingEventBase* pe = nullptr;
    EXPECT_EQ(queue.front(pe), true);
    auto* any = static_cast<TestEventSet::any_type*>(pe->payloadPtr());
    auto* te = std::get_if<CtEvent<CtTimerPayload>>(any);
    EXPECT_TRUE(te != nullptr);
    EXPECT_EQ(te->payload().timerId, 42);
    EXPECT_EQ(te->origin(), CtEventOrigin::kSystem);
    queue.popFront(pe);

    source->deactivate();
    // Deactivated: poll() enqueues nothing.
    source->poll(queue);
    EXPECT_EQ(queue.front(pe), false);
}

// --- ManualEventSource (§13.3 #6) ------------------------------------------

TEST_CASE(m2_manual_source_injects_exactly_once)
{
    TestQueue queue;
    auto source = std::make_shared<ManualEventSource<TestEventSet>>(
        CtEvent<CtTimerPayload>(CtEventOrigin::kApplication, CtTimerPayload{99}));
    CtEventLoop loop;
    loop.attachToCurrentThread();
    EXPECT_EQ(loop.attachSource(source), true);

    EXPECT_EQ(source->activate(), true);
    source->poll(queue);

    CtPendingEventBase* pe = nullptr;
    EXPECT_EQ(queue.front(pe), true);
    auto* any = static_cast<TestEventSet::any_type*>(pe->payloadPtr());
    auto* te = std::get_if<CtEvent<CtTimerPayload>>(any);
    EXPECT_TRUE(te != nullptr);
    EXPECT_EQ(te->payload().timerId, 99);
    queue.popFront(pe);

    // Second poll: source exhausted, nothing more.
    source->poll(queue);
    EXPECT_EQ(queue.front(pe), false);
}

// --- manual source end-to-end through the loop (§13.3 #6) -------------------

TEST_CASE(m2_manual_source_drains_through_loop_to_handler)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    auto receiver = makeReceiver();

    int timerCalls = 0;
    auto tTimer = dispatcher->addHandler<CtTimerPayload>(
        receiver, [&](CtEvent<CtTimerPayload>& ev) {
            ++timerCalls;
            ev.accept();
        });

    // Attach a manual source that injects ONE timer event, then run the
    // loop: the source's poll() runs inside exec(), the event lands in the
    // queue, and the drain dispatches it to the handler.
    CtEventLoop loop;
    loop.attachToCurrentThread();
    loop.setDispatcher(dispatcher);

    auto source = std::make_shared<ManualEventSource<TestEventSet>>(
        CtEvent<CtTimerPayload>(CtEventOrigin::kApplication, CtTimerPayload{5}));
    EXPECT_EQ(loop.attachSource(source), true);
    EXPECT_EQ(source->activate(), true);

    // exec() first drains (empty), then polls the source (enqueues), then
    // drains again. Quit after a short delay so the loop has time for both.
    std::thread quitter([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        loop.quit();
    });
    loop.exec();
    quitter.join();

    EXPECT_EQ(timerCalls, 1); // manual event reached the handler via the loop
}

// --- exec thread-affinity assertion (§13.3 #2) -----------------------------

TEST_CASE(m2_exec_on_wrong_thread_asserts_fails_release_degrades)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    CtEventLoop loop;
    loop.attachToCurrentThread(); // THIS thread is the loop's affinity
    loop.setDispatcher(dispatcher);

    // A different thread calls exec(): in a release build the affinity
    // assert degrades and exec returns 0 immediately.
    int execResult = -1;
    std::thread t([&] {
        execResult = loop.exec(); // wrong thread (release: returns 0)
    });
    t.join();

    // Debug builds abort on the assert; release returns 0 (degraded).
    EXPECT_EQ(execResult, 0);
}

// --- queue overflow: drop-tail + reported via popDropped (§13.3 #3) ---------

TEST_CASE(m2_queue_overflow_drops_tail_and_reports)
{
    TestQueue queue;
    auto receiver = makeReceiver();

    // Fill the queue past kMaxQueued (8192). Drop-tail policy: the NEW event
    // is always accepted (push returns true); the OLDEST lowest-priority
    // tail is evicted and counted. All pushes report accepted.
    constexpr std::size_t kMax = 8192;
    std::size_t accepted = 0;
    for (std::size_t i = 0; i <= kMax; ++i) { // 8193 pushes
        CtEvent<MousePayload> ev(CtEventOrigin::kApplication,
                                 MousePayload{static_cast<int>(i), 0});
        if (queue.push(std::weak_ptr<CtReceiver>(receiver), std::move(ev)))
            ++accepted;
    }
    EXPECT_EQ(accepted, kMax + 1); // all 8193 accepted (drop-tail policy)
    EXPECT_EQ(queue.size(), kMax); // one evicted from the tail

    // The drop is reported by the affinity thread via popDropped().
    const std::size_t dropped = queue.popDropped();
    EXPECT_EQ(dropped, 1u);
}

// --- queue closed: push rejected (§10.1 契约 #3) ---------------------------

TEST_CASE(m2_closed_queue_rejects_pushes)
{
    TestQueue queue;
    queue.close();

    CtEvent<MousePayload> ev(CtEventOrigin::kApplication, MousePayload{1, 1});
    EXPECT_EQ(queue.push(std::weak_ptr<CtReceiver>{}, std::move(ev)), false);
}

// --- recordAsyncDrop: source-side rejection leaves a kDropped trace -------

TEST_CASE(m2_record_async_drop_records_kdropped_in_queue_trace)
{
    // (Rev 13, P1) CtQueueBase::recordAsyncDrop is the public hook for
    // event sources whose poll() rejects an event BEFORE it enters the
    // queue (e.g. an unregistered Qt translator in the M3 bridge, §3.3
    // contract #5). The queue (a CtDispatchContext friend) records the
    // kDropped phase in its OWN trace on the affinity thread.
    TestQueue queue;

    EXPECT_EQ(queue.trace().count(), 0u);  // fresh trace

    // Known payload key -> traits name recorded.
    const uint64_t mouseKey = CtTypeKey<MousePayload>::value();
    queue.recordAsyncDrop(mouseKey);
    EXPECT_EQ(queue.trace().count(), 1u);
    EXPECT_EQ(queue.trace().at(0).phase, CtTracePhase::kDropped);
    EXPECT_TRUE(queue.trace().at(0).typeName == "Mouse");

    // Unknown key -> non-empty placeholder, still a kDropped record.
    queue.recordAsyncDrop(0xDEADBEEF);
    EXPECT_EQ(queue.trace().count(), 2u);
    EXPECT_EQ(queue.trace().at(1).phase, CtTracePhase::kDropped);
    EXPECT_TRUE(queue.trace().at(1).typeName == "<unregistered>");

    // The drop hook does NOT enqueue anything (queue stays empty).
    CtPendingEventBase* pe = nullptr;
    EXPECT_EQ(queue.front(pe), false);
}

// --- front() kQueued records the real payload name (M4, 04 Rev 10 P3-2) ----

TEST_CASE(m4_front_kqueued_records_real_type_name)
{
    // (M4) CtPendingEventBase::typeName() lets the queue's front() kQueued
    // trace record the REAL payload name instead of the "<async>"
    // placeholder (§7.10 contract #6). typeName() is a variant visit —
    // zero allocation, affinity thread.
    TestQueue queue;

    auto receiver = makeReceiver();
    CtEvent<MousePayload> ev(CtEventOrigin::kApplication, MousePayload{1, 1});
    queue.push(std::weak_ptr<CtReceiver>(receiver), std::move(ev));

    CtPendingEventBase* pe = nullptr;
    EXPECT_EQ(queue.front(pe), true);

    // The pending event knows its own payload name.
    EXPECT_TRUE(pe->typeName() == "Mouse");

    // The queue's kQueued trace record carries that name (not "<async>").
    EXPECT_EQ(queue.trace().count(), 1u);
    EXPECT_EQ(queue.trace().at(0).phase, CtTracePhase::kQueued);
    EXPECT_TRUE(queue.trace().at(0).typeName == "Mouse");
    queue.popFront(pe);

    // A second payload type records its own name.
    CtEvent<KeyPayload> keyEv(CtEventOrigin::kApplication, KeyPayload{7});
    queue.push(std::weak_ptr<CtReceiver>{}, std::move(keyEv));
    EXPECT_EQ(queue.front(pe), true);
    EXPECT_TRUE(pe->typeName() == "Key");
    EXPECT_EQ(queue.trace().count(), 2u);
    EXPECT_EQ(queue.trace().at(1).phase, CtTracePhase::kQueued);
    EXPECT_TRUE(queue.trace().at(1).typeName == "Key");
    queue.popFront(pe);
}

// --- popFront removes BY IDENTITY: concurrent push cannot evict wrong ------
// --- element (audit MUST-1 regression) ------------------------------------

TEST_CASE(m4_pop_front_identity_removes_dispatched_not_new_insert)
{
    // (Audit MUST-1 fix) The drain loop is front() → dispatchAny() →
    // popFront(). Between front() and popFront() another thread may push a
    // HIGHER-priority event which the priority insertion places AHEAD of the
    // dispatched element. A positional pop would remove the NEW event and
    // leave the dispatched one queued (→ second dispatch); popFront(which)
    // removes BY IDENTITY so the new event survives.
    TestQueue queue;

    // Low-priority event first: it becomes the front after the initial push.
    CtEvent<MousePayload> low(CtEventOrigin::kApplication, MousePayload{10, 0});
    low.setPriority(1);
    queue.push(std::weak_ptr<CtReceiver>{}, std::move(low));

    CtPendingEventBase* pe = nullptr;
    EXPECT_EQ(queue.front(pe), true); // "dispatched" element now

    // Simulate a cross-thread push DURING dispatch: higher priority event
    // inserts AHEAD of the element pe points at.
    CtEvent<KeyPayload> high(CtEventOrigin::kApplication, KeyPayload{99});
    high.setPriority(1000);
    queue.push(std::weak_ptr<CtReceiver>{}, std::move(high));

    // The drain loop now calls popFront(pe) for the DISPATCHED (low) event.
    queue.popFront(pe);

    // The high-priority event must still be queued (and be the front).
    EXPECT_EQ(queue.size(), 1u);
    EXPECT_EQ(queue.front(pe), true);
    auto* any = static_cast<TestEventSet::any_type*>(pe->payloadPtr());
    auto* key = std::get_if<CtEvent<KeyPayload>>(any);
    EXPECT_TRUE(key != nullptr); // the surviving event is the NEW one
    EXPECT_EQ(key->payload().key, 99);
    queue.popFront(pe);
    EXPECT_EQ(queue.size(), 0u);
}

} // namespace (anonymous)
