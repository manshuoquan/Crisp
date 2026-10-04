// CRISP event system demo (M1 sync core + M2 async core).
//
// Shows the type-safe, observable event pipeline:
//   M1 sync: CtEvent<T> -> CtDispatcher<CtEventSet> -> filters -> handlers
//            -> trace (observability)
//   M2 async: CtEventLoop drains a CtQueue; post() routes an event to the
//            receiver's thread; CtTimerEventSource injects periodic events.
//
// New-API invariants:
//   - a dispatcher MUST be heap-allocated and held by shared_ptr (§7.8);
//   - a receiver MUST be created via createReceiver<R>() (shared_ptr, §7.5);
//   - the dispatcher's EventSet closes the set of payload types (§6.3);
//   - send() takes the receiver shared_ptr; handler lambdas take
//     CtEvent<T>& and read the payload via .payload();
//   - post() is async: it returns kHandled on ACCEPTANCE (queued), not on
//     delivery (§10.1 contract #3).
//
// Build & run:
//   cmake -S . -B build -G "MinGW Makefiles"
//   cmake --build build
//   ./build/examples/event_demo/event_demo.exe

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "event/ct_dispatcher.h"
#include "event/ct_event_loop.h"
#include "event/ct_timer_event_source.h"

using namespace crisp::event;

// ---------------------------------------------------------------------------
// Event payload types (plain data; type identity comes from the C++ type).
// ---------------------------------------------------------------------------
struct MouseMovePayload {
    int x = 0;
    int y = 0;
};

struct KeyPressPayload {
    int key = 0;
    bool ctrl = false;
};

// NOTE: the framework provides CtTimerPayload (ct_timer_event_source.h) —
// applications add it to their EventSet and receive timer events typed as
// CtEvent<CtTimerPayload>. We do NOT define our own timer payload.

// Give events readable names for traces (compile-time metadata, §7.2).
CRISP_EVENT_TYPE(MouseMovePayload, "MouseMove");
CRISP_EVENT_TYPE(KeyPressPayload, "KeyPress");

using DemoEventSet = CtEventSet<MouseMovePayload, KeyPressPayload, CtTimerPayload>;
using DemoDispatcher = CtDispatcher<DemoEventSet>;

// ---------------------------------------------------------------------------
// A "window" that receives events.
// ---------------------------------------------------------------------------
class CtDemoWindow : public CtReceiver {
public:
    void onMouseMove(CtEvent<MouseMovePayload>& ev) {
        std::printf("  [window] mouse moved to (%d, %d)\n",
                    ev.payload().x, ev.payload().y);
        ev.accept(); // consume
    }

    void onKeyPress(CtEvent<KeyPressPayload>& ev) {
        std::printf("  [window] key %d pressed%s\n",
                    ev.payload().key, ev.payload().ctrl ? " (ctrl)" : "");
        // do NOT accept: let it propagate conceptually
    }

    void onTimer(CtEvent<CtTimerPayload>& ev) {
        std::printf("  [window] timer %d fired\n", ev.payload().timerId);
        ev.accept();
    }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    auto dispatcher = std::make_shared<DemoDispatcher>();
    auto window = createReceiver<CtDemoWindow>();

    // 1. Register handlers (type-safe: handler signature matches event type).
    //    NOTE: tokens are RAII — keeping them alive keeps the registrations
    //    live; destroying them unregisters (§7.7).
    auto tMouse = dispatcher->addHandler<MouseMovePayload>(
        window, [&window](CtEvent<MouseMovePayload>& ev) { window->onMouseMove(ev); });
    auto tKey = dispatcher->addHandler<KeyPressPayload>(
        window, [&window](CtEvent<KeyPressPayload>& ev) { window->onKeyPress(ev); });
    auto tTimer = dispatcher->addHandler<CtTimerPayload>(
        window, [&window](CtEvent<CtTimerPayload>& ev) { window->onTimer(ev); });

    // 2. A global filter: block mouse moves at x < 0 (edge case).
    auto tFilter = dispatcher->addFilter<MouseMovePayload>([](CtEvent<MouseMovePayload>& ev) {
        if (ev.payload().x < 0) {
            std::printf("  [filter] intercepted mouse move at x=%d\n", ev.payload().x);
            return true; // intercept
        }
        return false; // pass
    });

    // 3. A pure observer: logs every KeyPress without changing the outcome.
    auto tObserver = dispatcher->addObserver<KeyPressPayload>([](const CtEvent<KeyPressPayload>& ev,
                                                                 const CtDispatchContext&) {
        std::printf("  [observer] saw key press %d\n", ev.payload().key);
    });

    // 4. Dispatch a few events (sync path, §8.1).
    std::printf("== dispatch MouseMove(10, 20) ==\n");
    CtEvent<MouseMovePayload> moveEv(MouseMovePayload{10, 20});
    dispatcher->send(window, moveEv);

    std::printf("== dispatch MouseMove(-5, 3) -> should be filtered ==\n");
    CtEvent<MouseMovePayload> badMoveEv(MouseMovePayload{-5, 3});
    dispatcher->send(window, badMoveEv);

    std::printf("== dispatch KeyPress(65, ctrl) ==\n");
    CtEvent<KeyPressPayload> keyEv(KeyPressPayload{65, true});
    dispatcher->send(window, keyEv);

    std::printf("== dispatch Timer(1) ==\n");
    CtEvent<CtTimerPayload> timerEv(CtTimerPayload{1});
    dispatcher->send(window, timerEv);

    // 5. M2 async path: a per-thread event loop drains a CtQueue. post()
    //    routes an event to the receiver's thread (here: THIS thread).
    std::printf("\n== async: CtEventLoop + post() ==\n");
    CtEventLoop loop;
    loop.attachToCurrentThread();
    loop.setDispatcher(dispatcher);

    // Attach a timer source; the loop polls it every iteration.
    auto timerSource = std::make_shared<CtTimerEventSource<DemoEventSet>>(
        std::chrono::milliseconds(100), 7);
    loop.attachSource(timerSource);
    timerSource->activate();

    // post a mouse event: accepted (queued) -> kHandled. Sleep past the
    // timer's first deadline first — M2 uses a POLLING model (§7.12 contract #3,
    // §14 #8): the timer fires only when exec is already awake (a platform
    // wait arrives in M4), so each posted event wakes the loop, which then
    // polls the timer.
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CtEvent<MouseMovePayload> asyncMove(MouseMovePayload{30, 40});
    const CtDispatchResult posted = dispatcher->post(window, std::move(asyncMove));
    std::printf("  post -> %s\n",
                posted == CtDispatchResult::kHandled ? "kHandled (accepted)"
                                                     : "kUnhandled (rejected)");

    // 6. Run the loop; quit from a helper thread after a few timer cycles.
    std::thread quitter([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(450));
        loop.quit();
    });
    const int iterations = loop.exec();
    quitter.join();
    std::printf("  loop ran %d drain iterations\n", iterations);

    // 7. Show the built-in trace: the full journey of every event (sync +
    //    async phases kQueued/kDropped land in the queue's own trace).
    std::printf("\n== dispatcher trace (last events) ==\n");
    dispatcher->trace().dump();
    std::printf("\n== queue trace (async phases) ==\n");
    loop.queue().trace().dump();

    (void)tMouse; (void)tKey; (void)tTimer; (void)tFilter; (void)tObserver;
    (void)timerSource;
    return 0;
}
