// Unit tests for CtDispatcher (M1 sync core).
//
// Part of the CRISP event subsystem (crisp::event). Covers §13.1 contracts:
// COW snapshot consistency, weak_ptr skipping, token lifecycle, send
// thread-affinity assertion, filter ordering, first-accept stopping,
// broadcast OR semantics, observer pre/post phases, and trace phase
// sequences. Hand-written test doubles only (NO mock framework).
//
// The dispatcher MUST be heap-allocated via std::make_shared (§7.8) —
// CtDispatcherBase::makeTokenState() calls shared_from_this().

#include "event/ct_dispatcher.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "test_harness.h"
#include "tu_a.h"
#include "tu_b.h"

namespace {
using namespace crisp::event;

struct MousePayload {
    int x = 0;
    int y = 0;
};

struct KeyPayload {
    int key = 0;
};

struct TimerPayload {
    int timerId = 0;
};

} // namespace (anonymous)

CRISP_EVENT_TYPE(MousePayload, "Mouse");
CRISP_EVENT_TYPE(KeyPayload, "Key");
CRISP_EVENT_TYPE(TimerPayload, "Timer");

namespace {
using namespace crisp::event;

using TestEventSet = CtEventSet<MousePayload, KeyPayload, TimerPayload>;
using TestDispatcher = CtDispatcher<TestEventSet>;

// --- helpers ---------------------------------------------------------------

class TestReceiver : public CtReceiver {
public:
    int mouseCalls = 0;
    int keyCalls = 0;
    int timerCalls = 0;
    bool acceptMouse = true;
};

std::shared_ptr<TestReceiver> makeReceiver() {
    return createReceiver<TestReceiver>();
}

// --- COW snapshot consistency (§13.1 #3) -----------------------------------

TEST_CASE(m1_cow_snapshot_unregister_between_dispatches_takes_effect)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    auto receiver = makeReceiver();

    int calls = 0;
    auto token = dispatcher->addHandler<MousePayload>(
        receiver, [&](CtEvent<MousePayload>&) { ++calls; });

    CtEvent<MousePayload> ev1(MousePayload{1, 1});
    dispatcher->send(receiver, ev1);
    EXPECT_EQ(calls, 1);

    token = CtHandlerToken{}; // destroy: unregister takes effect at next dispatch

    CtEvent<MousePayload> ev2(MousePayload{2, 2});
    dispatcher->send(receiver, ev2);
    EXPECT_EQ(calls, 1); // still 1: second dispatch found no handler
}

// --- weak_ptr skip (§13.1 #4) ----------------------------------------------

TEST_CASE(m1_expired_receiver_is_skipped_not_called)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    int calls = 0;
    CtHandlerToken token;
    {
        auto receiver = makeReceiver();
        token = dispatcher->addHandler<MousePayload>(
            receiver, [&](CtEvent<MousePayload>&) { ++calls; });

        CtEvent<MousePayload> ev(MousePayload{0, 0});
        dispatcher->send(receiver, ev);
        EXPECT_EQ(calls, 1);
    } // receiver destroyed here; snapshot still holds its weak entry

    auto receiver2 = makeReceiver();
    CtEvent<MousePayload> ev2(MousePayload{0, 0});
    dispatcher->send(receiver2, ev2); // old weak entry: lock() fails, skipped
    EXPECT_EQ(calls, 1); // no call through the expired receiver
}

// --- token lifecycle (§13.1 #5) --------------------------------------------

TEST_CASE(m1_token_move_only_and_destroy_unregisters)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    auto receiver = makeReceiver();

    int calls = 0;
    auto token = dispatcher->addHandler<MousePayload>(
        receiver, [&](CtEvent<MousePayload>&) { ++calls; });
    EXPECT_TRUE(token.valid());

    CtHandlerToken moved = std::move(token);
    EXPECT_TRUE(moved.valid());
    EXPECT_TRUE(!token.valid()); // moved-from is null

    // Destroying the moved token unregisters.
    moved = CtHandlerToken{};
    CtEvent<MousePayload> ev(MousePayload{5, 5});
    dispatcher->send(receiver, ev);
    EXPECT_EQ(calls, 0);
}

TEST_CASE(m1_token_outliving_dispatcher_is_safe)
{
    int calls = 0;
    CtHandlerToken token;
    {
        auto dispatcher = std::make_shared<TestDispatcher>();
        auto receiver = makeReceiver();
        token = dispatcher->addHandler<MousePayload>(
            receiver, [&](CtEvent<MousePayload>&) { ++calls; });
    } // dispatcher destroyed; token must not dangle
    // Token destructor runs here — must be a no-op (alive flipped false).
}

// --- send thread-affinity (§13.1 #6) ---------------------------------------

TEST_CASE(m1_send_on_thread_returns_result)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    auto receiver = makeReceiver();

    int calls = 0;
    auto token = dispatcher->addHandler<MousePayload>(
        receiver, [&](CtEvent<MousePayload>& ev) {
            ++calls;
            ev.accept();
        });

    CtEvent<MousePayload> ev(MousePayload{7, 7});
    const auto result = dispatcher->send(receiver, ev);
    EXPECT_EQ(static_cast<int>(result), static_cast<int>(CtDispatchResult::kHandled));
    EXPECT_EQ(calls, 1);
}

// --- filter ordering (§13.1 #7) --------------------------------------------

TEST_CASE(m1_filters_run_in_registration_order)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    auto receiver = makeReceiver();

    std::vector<int> order;
    auto f1 = dispatcher->addFilter<MousePayload>([&](CtEvent<MousePayload>&) {
        order.push_back(1);
        return false;
    });
    auto f2 = dispatcher->addFilter<MousePayload>([&](CtEvent<MousePayload>&) {
        order.push_back(2);
        return false;
    });
    auto f3 = dispatcher->addFilter<MousePayload>([&](CtEvent<MousePayload>&) {
        order.push_back(3);
        return false;
    });

    CtEvent<MousePayload> ev(MousePayload{1, 2});
    dispatcher->send(receiver, ev);

    EXPECT_EQ(order.size(), static_cast<std::size_t>(3));
    if (order.size() == 3) {
        EXPECT_EQ(order[0], 1);
        EXPECT_EQ(order[1], 2);
        EXPECT_EQ(order[2], 3);
    }
}

// --- first accept stops (§13.1 #8) -----------------------------------------

TEST_CASE(m1_first_accept_stops_later_handlers)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    auto receiver = makeReceiver();

    int firstCalls = 0;
    int secondCalls = 0;
    auto t1 = dispatcher->addHandler<MousePayload>(
        receiver, [&](CtEvent<MousePayload>& ev) {
            ++firstCalls;
            ev.accept();
        });
    auto t2 = dispatcher->addHandler<MousePayload>(
        receiver, [&](CtEvent<MousePayload>&) { ++secondCalls; });

    CtEvent<MousePayload> ev(MousePayload{1, 1});
    const auto result = dispatcher->send(receiver, ev);

    EXPECT_EQ(firstCalls, 1);
    EXPECT_EQ(secondCalls, 0); // first accept stopped delivery
    EXPECT_EQ(static_cast<int>(result), static_cast<int>(CtDispatchResult::kHandled));
}

// --- broadcast OR semantics (§13.1 #9) -------------------------------------

TEST_CASE(m1_broadcast_all_receivers_called_accept_or)
{
    auto dispatcher = std::make_shared<TestDispatcher>();

    int accepted = 0;
    int ignored = 0;
    auto r1 = makeReceiver();
    auto r2 = makeReceiver();
    auto t1 = dispatcher->addHandler<MousePayload>(r1, [&](CtEvent<MousePayload>& ev) {
        ++accepted;
        ev.accept();
    });
    auto t2 = dispatcher->addHandler<MousePayload>(r2, [&](CtEvent<MousePayload>&) { ++ignored; });

    CtEvent<MousePayload> ev(MousePayload{2, 3});
    const auto result = dispatcher->broadcast(ev);

    EXPECT_EQ(accepted, 1);
    EXPECT_EQ(ignored, 1); // broadcast delivers to ALL, even after accept
    EXPECT_EQ(static_cast<int>(result), static_cast<int>(CtDispatchResult::kHandled));
}

TEST_CASE(m1_broadcast_all_ignore_returns_unhandled)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    auto r1 = makeReceiver();
    auto r2 = makeReceiver();
    auto t1 = dispatcher->addHandler<MousePayload>(r1, [&](CtEvent<MousePayload>&) {});
    auto t2 = dispatcher->addHandler<MousePayload>(r2, [&](CtEvent<MousePayload>&) {});

    CtEvent<MousePayload> ev(MousePayload{1, 1});
    const auto result = dispatcher->broadcast(ev);
    EXPECT_EQ(static_cast<int>(result), static_cast<int>(CtDispatchResult::kUnhandled));
}

TEST_CASE(m1_broadcast_no_receiver_returns_kNoReceiver)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    CtEvent<MousePayload> ev(MousePayload{1, 1});
    const auto result = dispatcher->broadcast(ev);
    EXPECT_EQ(static_cast<int>(result), static_cast<int>(CtDispatchResult::kNoReceiver));
}

// --- observer pre/post (§13.1 #10) -----------------------------------------

TEST_CASE(m1_observer_pre_post_phases)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    auto receiver = makeReceiver();

    std::vector<CtTracePhase> phases;
    auto t = dispatcher->addHandler<MousePayload>(
        receiver, [&](CtEvent<MousePayload>& ev) { ev.accept(); });
    auto o = dispatcher->addObserver<MousePayload>(
        [&](const CtEvent<MousePayload>&, const CtDispatchContext& ctx) {
            phases.push_back(ctx.phase());
        });

    dispatcher->CtDispatcherBase::trace().clear();
    CtEvent<MousePayload> ev(MousePayload{4, 5});
    dispatcher->send(receiver, ev);

    // Pre and post observer callbacks: 2 entries, both kObserved.
    EXPECT_EQ(phases.size(), static_cast<std::size_t>(2));
    if (phases.size() == 2) {
        EXPECT_EQ(static_cast<int>(phases[0]), static_cast<int>(CtTracePhase::kObserved));
        EXPECT_EQ(static_cast<int>(phases[1]), static_cast<int>(CtTracePhase::kObserved));
    }
}

// --- trace phases (§13.1 #11) ---------------------------------------------

TEST_CASE(m1_trace_phase_sequence_full_send)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    auto receiver = makeReceiver();

    auto t = dispatcher->addHandler<MousePayload>(
        receiver, [&](CtEvent<MousePayload>& ev) { ev.accept(); });
    // §13.1 #11 expects the full kDispatched/kObserved/kDelivered/kObserved
    // sequence — the two kObserved records come from a registered observer.
    auto o = dispatcher->addObserver<MousePayload>(
        [&](const CtEvent<MousePayload>&, const CtDispatchContext&) {});

    dispatcher->CtDispatcherBase::trace().clear();
    CtEvent<MousePayload> ev(MousePayload{9, 9});
    dispatcher->send(receiver, ev);

    const auto& trace = dispatcher->trace();
    const auto n = trace.count();
    EXPECT_TRUE(n >= 4);
    if (n >= 4) {
        EXPECT_EQ(static_cast<int>(trace.at(n - 4).phase), static_cast<int>(CtTracePhase::kDispatched));
        EXPECT_EQ(static_cast<int>(trace.at(n - 3).phase), static_cast<int>(CtTracePhase::kObserved));
        EXPECT_EQ(static_cast<int>(trace.at(n - 2).phase), static_cast<int>(CtTracePhase::kDelivered));
        EXPECT_EQ(static_cast<int>(trace.at(n - 1).phase), static_cast<int>(CtTracePhase::kObserved));
    }
}

TEST_CASE(m1_trace_phase_sequence_filtered)
{
    auto dispatcher = std::make_shared<TestDispatcher>();
    auto receiver = makeReceiver();

    auto f = dispatcher->addFilter<MousePayload>([](CtEvent<MousePayload>&) { return true; });
    auto t = dispatcher->addHandler<MousePayload>(
        receiver, [&](CtEvent<MousePayload>& ev) { ev.accept(); });

    dispatcher->CtDispatcherBase::trace().clear();
    CtEvent<MousePayload> ev(MousePayload{1, 2});
    const auto result = dispatcher->send(receiver, ev);

    EXPECT_EQ(static_cast<int>(result), static_cast<int>(CtDispatchResult::kFiltered));
    const auto& trace = dispatcher->trace();
    const auto n = trace.count();
    EXPECT_TRUE(n >= 2);
    if (n >= 2) {
        EXPECT_EQ(static_cast<int>(trace.at(n - 2).phase), static_cast<int>(CtTracePhase::kDispatched));
        EXPECT_EQ(static_cast<int>(trace.at(n - 1).phase), static_cast<int>(CtTracePhase::kFiltered));
    }
    // No post-observer, no kDelivered (interception short-circuits, §8.3 #3).
}

// --- cross-TU key stability (§13.2) ----------------------------------------

TEST_CASE(m1_cross_tu_type_key_stable)
{
    EXPECT_EQ(tu_a::sharedKey(), tu_b::sharedKey());
    EXPECT_NE(tu_a::sharedKey(), tu_a::uniqueKey());
    EXPECT_NE(tu_a::sharedKey(), tu_b::uniqueKey());
    EXPECT_NE(tu_a::uniqueKey(), tu_b::uniqueKey());
}

} // namespace
