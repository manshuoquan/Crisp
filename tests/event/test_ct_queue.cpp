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

    queue.popFront();
    EXPECT_EQ(queue.front(pe), true);
    auto* m2 = std::get_if<CtEvent<MousePayload>>(
        static_cast<TestEventSet::any_type*>(pe->payloadPtr()));
    EXPECT_EQ(m2->payload().x, 2);

    queue.popFront();
    EXPECT_EQ(queue.front(pe), true);
    auto* m3 = std::get_if<CtEvent<MousePayload>>(
        static_cast<TestEventSet::any_type*>(pe->payloadPtr()));
    EXPECT_EQ(m3->payload().x, 3);

    queue.popFront();
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
    queue.popFront();

    EXPECT_EQ(queue.front(pe), true);
    EXPECT_EQ(xOf(), 15);
    queue.popFront();

    EXPECT_EQ(queue.front(pe), true);
    EXPECT_EQ(xOf(), 10);
    queue.popFront();

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
    queue.popFront();

    // Broadcast (empty target): isBroadcast() must be true.
    CtEvent<MousePayload> broadcast(CtEventOrigin::kApplication, MousePayload{2, 2});
    queue.push(std::weak_ptr<CtReceiver>{}, std::move(broadcast));

    EXPECT_EQ(queue.front(pe), true);
    EXPECT_EQ(pe->isBroadcast(), true);
    EXPECT_TRUE(pe->target().expired()); // empty weak_ptr
    queue.popFront();
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
    queue.popFront();

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
    queue.popFront();

    // Second poll: source exhausted, nothing more.
    source->poll(queue);
    EXPECT_EQ(queue.front(pe), false);
}

} // namespace (anonymous)
