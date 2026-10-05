// Unit tests: hot-path zero-allocation (§13.1 #12) via operator new counting.
//
// Part of the CRISP event subsystem (crisp::event). §11.1 promises ZERO heap
// allocations on the sync dispatch hot path (send/broadcast). We count
// allocations with a global operator new override scoped to the test binary,
// then assert a full send pipeline (observers + filters + handlers + trace)
// performs none.
//
// CAUTION: the override is global to this test executable. Tests that
// legitimately allocate (registration, queue pushes, post routing) MUST run
// with the counter DISARMED — only the zero-allocation cases arm it.

#include "event/ct_dispatcher.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <memory>

#include "test_harness.h"

// ---------------------------------------------------------------------------
// Allocation counter (global operator new/delete override, test binary only).
// ---------------------------------------------------------------------------
namespace test {

std::atomic<std::size_t> g_allocCount{0};
std::atomic<bool> g_counting{false};

} // namespace test

// Override the global allocation functions so we can count framework-side
// allocations on the hot path. The std::function/std::string machinery in
// TEST code is allocated BEFORE the counter is armed and AFTER it is
// disarmed, so only the dispatch call itself is measured.
void* operator new(std::size_t size) {
    if (test::g_counting.load(std::memory_order_relaxed))
        test::g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size);
    if (!p)
        throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t size) {
    if (test::g_counting.load(std::memory_order_relaxed))
        test::g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size);
    if (!p)
        throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
using namespace crisp::event;

struct AllocPayload {
    int value = 0;
};

} // namespace (anonymous)

CRISP_EVENT_TYPE(AllocPayload, "Alloc");

namespace {
using namespace crisp::event;

using AllocEventSet = CtEventSet<AllocPayload>;
using AllocDispatcher = CtDispatcher<AllocEventSet>;

// --- hot-path zero allocation (§13.1 #12) ----------------------------------

TEST_CASE(m1_hot_path_send_allocates_zero)
{
    auto dispatcher = std::make_shared<AllocDispatcher>();
    auto receiver = createReceiver<CtReceiver>();

    // Registration is a WRITE path (allocation allowed). Register everything
    // BEFORE arming the counter.
    int handlerCalls = 0;
    auto tHandler = dispatcher->addHandler<AllocPayload>(
        receiver, [&](CtEvent<AllocPayload>& ev) {
            ++handlerCalls;
            ev.accept();
        });
    auto tFilter = dispatcher->addFilter<AllocPayload>(
        [](CtEvent<AllocPayload>&) { return false; }); // pass through
    auto tObserver = dispatcher->addObserver<AllocPayload>(
        [](const CtEvent<AllocPayload>&, const CtDispatchContext&) {});

    // Warm-up trace ring slot reuse is irrelevant (record() writes into the
    // pre-existing buffer); arm the counter and run ONE full send.
    test::g_allocCount.store(0, std::memory_order_relaxed);
    test::g_counting.store(true, std::memory_order_relaxed);

    CtEvent<AllocPayload> ev(AllocPayload{42});
    const auto result = dispatcher->send(receiver, ev);

    test::g_counting.store(false, std::memory_order_relaxed);
    const std::size_t allocs = test::g_allocCount.load(std::memory_order_relaxed);

    EXPECT_EQ(static_cast<int>(result), static_cast<int>(CtDispatchResult::kHandled));
    EXPECT_EQ(handlerCalls, 1);
    // §11.1: zero allocations on the hot path. The COW snapshot is already
    // built (read-only refcount bump, no allocation); the trace ring writes
    // into its preallocated buffer.
    EXPECT_EQ(allocs, 0u);
}

TEST_CASE(m1_hot_path_broadcast_allocates_zero)
{
    auto dispatcher = std::make_shared<AllocDispatcher>();
    auto receiver = createReceiver<CtReceiver>();

    int calls = 0;
    auto tHandler = dispatcher->addHandler<AllocPayload>(
        receiver, [&](CtEvent<AllocPayload>& ev) {
            ++calls;
            ev.accept();
        });

    test::g_allocCount.store(0, std::memory_order_relaxed);
    test::g_counting.store(true, std::memory_order_relaxed);

    CtEvent<AllocPayload> ev(AllocPayload{1});
    const auto result = dispatcher->broadcast(ev);

    test::g_counting.store(false, std::memory_order_relaxed);
    const std::size_t allocs = test::g_allocCount.load(std::memory_order_relaxed);

    EXPECT_EQ(static_cast<int>(result), static_cast<int>(CtDispatchResult::kHandled));
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(allocs, 0u);
}

} // namespace (anonymous)
