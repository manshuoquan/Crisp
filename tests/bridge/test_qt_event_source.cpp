// Unit tests for CtQtEventSource<EventSet> + capture (M3, 搂7.2鈥撀?.4).
//
// Part of the CRISP project. Covers: capture-not-intercept, poll
// translation into the queue, ignored-event skipping, unregistered-type
// drops (kDropped via recordAsyncDrop), timer translation (搂7.2 #9), quit
// handler (搂7.2 #11), the full bridge->CRISP->bridge loop, ordering,
// capture lifecycle (搂7.4 #15), QObject lifecycle via deleteLater (搂7.4
// #14), inactive poll (搂7.4 #15a), the in-poll re-entry guard (搂7.4 #16),
// and cross-thread delivery (搂7.4 #17). Hand-written test doubles only.
//
// Queue-content cases (e.g. 搂7.2 #6) inspect the queue directly after
// poll(): front() + payloadPtr() + get_if 鈥?no dispatchAny dependency.
// Handler-delivery cases drive CtEventLoop::exec() with loop.quit() from
// the handler (exec is the only public drain path).

#include "bridge/qt_event_source.h"
#include "bridge/qt_event_set.h"
#include "bridge/qt_mouse_payload.h"
#include "bridge/qt_sink.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QTimerEvent>
#include <QWidget>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "test_harness.h"

namespace {
using namespace crisp::event;

using BridgeSet = CtQtEventSet;
using BridgeDispatcher = CtDispatcher<BridgeSet>;
using BridgeQueue = CtQueue<BridgeSet>;

/// Drain the queue, returning the FIRST CtEvent<CtQtEventPayload> whose
/// qtType() == `qtType` (or nullptr). Pops as it scans (queue-content
/// inspection helper 鈥?tests use it because a QWidget receiver produces
/// lifecycle events like QEvent::Create alongside the sent event).
CtEvent<CtQtEventPayload>* findQueuedQtEvent(CtQueueBase& queue, int qtType) {
    CtPendingEventBase* pe = nullptr;
    while (queue.front(pe)) {
        auto* any = static_cast<BridgeSet::any_type*>(pe->payloadPtr());
        auto* qev = std::get_if<CtEvent<CtQtEventPayload>>(any);
        if (qev && qev->payload().qtType() == qtType) {
            // Leave it in the queue for further inspection.
            return qev;
        }
        queue.popFront(pe);
    }
    return nullptr;
}

/// (M4) Drain the queue, returning the FIRST CtEvent<T> of the typed
/// payload T (or nullptr). Pops non-matching events as it scans; leaves the
/// match queued (same semantics as findQueuedQtEvent). Used by the S1/S2
/// typed-translation tests (06 搂6.2).
template <typename T>
CtEvent<T>* findQueuedTypedEvent(CtQueueBase& queue) {
    CtPendingEventBase* pe = nullptr;
    while (queue.front(pe)) {
        auto* any = static_cast<BridgeSet::any_type*>(pe->payloadPtr());
        auto* typed = std::get_if<CtEvent<T>>(any);
        if (typed != nullptr) {
            // Leave it in the queue for further inspection.
            return typed;
        }
        queue.popFront(pe);
    }
    return nullptr;
}

/// Send a Qt event to a receiver object; the app-level filter observes it.
/// QObject (NOT QWidget): a plain QObject produces NO window-lifecycle
/// events (Create/Show/etc.) that a QWidget emits on construction, so the
/// capture buffer contains exactly the sent event 鈥?deterministic tests.
bool sendToObject(QObject& o, QEvent& ev) {
    return QCoreApplication::sendEvent(&o, &ev);
}

/// QEvent subclass whose clone() returns nullptr: the non-cloneable custom
/// type used to exercise the 搂3.4a #6 kDropped path.
class NonCloneableEvent : public QEvent {
public:
    explicit NonCloneableEvent() : QEvent(QEvent::Type(QEvent::User + 50)) {}
    QEvent* clone() const override { return nullptr; }
};

/// QObject whose event() handler re-enters poll() when it receives a posted
/// MouseMove (the FORBIDDEN re-entry, 搂4.2 濂戠害 #2): the posted event is
/// delivered INSIDE poll()'s processEvents(), so the nested source->poll()
/// hits the m_inPoll guard deterministically 鈥?no timer timing involved.
class ReentrantReceiver : public QObject {
public:
    CtQtEventSource<BridgeSet>* source = nullptr;  ///< source to re-enter
    CtQueueBase* queue = nullptr;                  ///< queue for the nested poll
    int nestedAttempts = 0;                        ///< nested poll() calls made

protected:
    bool event(QEvent* e) override {
        if (e->type() == QEvent::MouseMove) {
            ++nestedAttempts;
            if (source != nullptr && queue != nullptr)
                source->poll(*queue);  // forbidden re-entry (skipped by guard)
            return true;  // consume: only the filter observed it
        }
        return QObject::event(e);
    }
};

// --- 搂7.2 #5: capture does NOT intercept ------------------------------------

TEST_CASE(qt_source_capture_never_intercepts)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    EXPECT_EQ(source->activate(), true);

    QObject receiver;
    QMouseEvent ev(QEvent::MouseMove, QPoint(5, 5), QPoint(5, 5),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);

    // sendEvent returns the receiver's handling result; the filter never
    // intercepts, so the receiver (default QObject::event) ignores it and
    // sendEvent returns false 鈥?but the capture buffer got the clone.
    const bool handled = sendToObject(receiver, ev);
    EXPECT_EQ(handled, false);  // receiver did not consume it (dumb)
    EXPECT_EQ(source->capture()->bufferedCount(), 1u);
}

// --- 搂7.2 #6: poll translates into the queue --------------------------------

TEST_CASE(qt_source_poll_translates_mouse_move_into_queue)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    EXPECT_EQ(source->activate(), true);

    QObject receiver;
    QMouseEvent ev(QEvent::MouseMove, QPoint(10, 20), QPoint(10, 20),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    sendToObject(receiver, ev);

    // One poll(): pumps Qt events, drains the capture buffer into the queue.
    source->poll(loop.queue());

    // (M4) MouseMove is typed-translated: the queued event is
    // CtEvent<CtMousePayload>, NOT the raw CtQtEventPayload (06 搂6.2 S1).
    auto* mouse = findQueuedTypedEvent<CtMousePayload>(loop.queue());
    EXPECT_TRUE(mouse != nullptr);
    if (mouse) {
        EXPECT_EQ(mouse->payload().action, CtMouseAction::kMove);
        EXPECT_EQ(mouse->payload().x, 10);
        EXPECT_EQ(mouse->payload().y, 20);
    }
}

// --- 搂7.2 #7: kIgnoredQtEvents are skipped ----------------------------------

TEST_CASE(qt_source_skips_ignored_events)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    EXPECT_EQ(source->activate(), true);

    QObject receiver;
    QEvent paint(QEvent::Paint);
    sendToObject(receiver, paint);

    EXPECT_EQ(source->capture()->bufferedCount(), 0u);  // never cloned
    source->poll(loop.queue());
    EXPECT_EQ(loop.queue().size(), 0u);  // nothing translated
}

// --- 搂7.2 #8: non-cloneable type -> kDropped, not queued --------------------

TEST_CASE(qt_source_noncloneable_type_drops_with_trace)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    EXPECT_EQ(source->activate(), true);

    // Non-cloneable custom event (搂3.4a #6): the filter buffers a NULL
    // marker; the drain records kDropped (queue trace) and nothing enters
    // the queue. This is the REAL unregistered/unbridgeable path 鈥?every
    // bridgeable built-in type (mouse/key/focus/...) is identity-translated
    // by default (搂3.3 contract #4, 搂3.4a #4), so "unregistered" no longer
    // means "dropped" for those.
    QObject receiver;
    NonCloneableEvent ev;
    sendToObject(receiver, ev);

    // Captured as a null marker (buffer got a slot, not a clone).
    EXPECT_EQ(source->capture()->bufferedCount(), 1u);

    const std::size_t traceBefore = loop.queue().trace().count();
    source->poll(loop.queue());

    // Not queued.
    EXPECT_EQ(loop.queue().size(), 0u);

    // kDropped recorded in the QUEUE trace (recordAsyncDrop, Rev 13 P1).
    EXPECT_EQ(loop.queue().trace().count(), traceBefore + 1u);
    EXPECT_EQ(loop.queue().trace().at(traceBefore).phase,
              CtTracePhase::kDropped);
}

// --- 搂7.2 #9: timer translation (real Qt timer -> CRISP handler) ------------

/// QObject that stops its own timer after the FIRST fire: the single-shot
/// behavior makes the 搂7.2 #9 test deterministic (a repeating raw timer
/// fires once per poll cycle on Windows, accumulating extra events).
class SingleShotTimerContext : public QObject {
public:
    int timerId = -1;       ///< the started timer id
    int fireCount = 0;      ///< number of timerEvent fires seen

protected:
    void timerEvent(QTimerEvent* ev) override {
        if (ev->timerId() != timerId)
            return;
        ++fireCount;
        killTimer(timerId);  // single-shot: no further fires
        timerId = -1;
    }
};

TEST_CASE(qt_source_timer_translation_delivers_to_crisp_handler)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    EXPECT_EQ(source->activate(), true);

    // A real Qt timer that fires EXACTLY ONCE (the context stops it after
    // the first QTimerEvent 鈥?deterministic under ctest load, unlike a
    // repeating raw timer which can fire multiple times per pump on
    // Windows). The QTimerEvent is cloned by the capture filter (搂3.3
    // contract #4); the 50ms interval keeps one fire per pump.
    SingleShotTimerContext context;
    context.timerId = context.startTimer(50);

    // Poll until the single timer event is captured (robust under ctest
    // load: the OS may delay the fire; a single poll would be flaky).
    auto* qev = findQueuedQtEvent(loop.queue(), QEvent::Timer);
    for (int i = 0; i < 200 && qev == nullptr; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        source->poll(loop.queue());
        qev = findQueuedQtEvent(loop.queue(), QEvent::Timer);
    }
    EXPECT_EQ(context.fireCount, 1);   // the Qt timer fired exactly once
    EXPECT_TRUE(qev != nullptr);  // the Qt timer reached the queue

    // Drain the queued event into a CRISP handler via exec() (the only
    // public drain path); the handler quits the loop after delivery.
    auto receiver = createReceiver<CtReceiver>();
    int timerCalls = 0;
    auto token = dispatcher->addHandler<CtQtEventPayload>(
        receiver, [&](CtEvent<CtQtEventPayload>& ev) {
            if (ev.payload().qtType() == static_cast<int>(QEvent::Timer)) {
                ++timerCalls;
                ev.accept();
            }
            loop.quit();
        });
    std::thread quitter([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        loop.quit();  // safety: never hang the suite
    });
    [[maybe_unused]] const int iterations = loop.exec();
    quitter.join();

    EXPECT_EQ(timerCalls, 1);  // the Qt timer reached CRISP as a Timer event
}

// --- 搂7.2 #11: quit handler -------------------------------------------------

TEST_CASE(qt_source_quit_handler_recording_handler_does_not_quit_loop)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    EXPECT_EQ(source->activate(), true);

    // Replace the default (loop->quit()) with a recording handler.
    int quitCalls = 0;
    source->setQuitHandler([&]() { ++quitCalls; });

    // A Qt Quit event is delivered to a plain object (the app-level filter
    // observes ALL deliveries); the filter sets the quit flag, never clones
    // (搂3.4a #3). sendEvent is synchronous and does NOT take ownership, so
    // a stack event is used.
    QObject receiver;
    QEvent quitEvent(QEvent::Quit);
    sendToObject(receiver, quitEvent);
    source->poll(loop.queue());

    // The recording handler ran; the LOOP itself did NOT quit (we never
    // attached an exec, and the recording handler does not call quit).
    EXPECT_EQ(quitCalls, 1);
    EXPECT_EQ(source->capture()->quitCaptured(), false);  // cleared after run

    // No event entered the queue (Quit is never translated 鈥?搂3.4a #3).
    EXPECT_EQ(loop.queue().size(), 0u);
}

// --- 搂7.4 #14: QObject lifecycle (deleteLater while the bridge runs) --------

TEST_CASE(qt_source_qobject_delete_later_is_safe)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    EXPECT_EQ(source->activate(), true);

    // A widget alive during the bridge run; deleteLater() hands its
    // lifetime to Qt's deferred-delete bookkeeping (the bridge holds NO
    // QObject reference 鈥?搂4.1 ownership table). Qt destroys it when the
    // DeferredDelete event is processed.
    auto* widget = new QWidget();
    widget->deleteLater();
    QCoreApplication::processEvents();  // DeferredDelete runs: widget is gone

    // The destruction pass may deliver non-ignored lifecycle events to the
    // filter (e.g. QEvent::Destroy, type 25 鈥?bridgeable below User); the
    // point is the bridge NEVER crashes or dangles (P2-4: it holds no QObject
    // reference; Qt removes dead objects from the filter chain itself). Drain
    // whatever the destruction delivered.
    source->poll(loop.queue());

    // The bridge is still functional after the watched object died: a new
    // event to a live object is captured normally.
    QObject live;
    QMouseEvent ev(QEvent::MouseMove, QPoint(1, 2), QPoint(1, 2),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    sendToObject(live, ev);
    EXPECT_EQ(source->capture()->bufferedCount(), 1u);

    source->poll(loop.queue());
    // (M4) MouseMove is typed-translated -> CtMousePayload (搂1.3 淇硶).
    auto* mouse = findQueuedTypedEvent<CtMousePayload>(loop.queue());
    EXPECT_TRUE(mouse != nullptr);  // bridge intact after the widget's death
}

// --- 搂7.4 #15: capture filter lifecycle -------------------------------------

TEST_CASE(qt_source_destroyed_removes_filter_and_stops_capture)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);

    // Scope the source: destructor removes the app-level filter while the
    // QCoreApplication is still alive (搂3.4 contract #1/#6, 搂4.2 contract #5).
    {
        auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
        EXPECT_EQ(source->activate(), true);
        EXPECT_EQ(source->capture()->bufferedCount(), 0u);

        QObject receiver;
        QMouseEvent ev(QEvent::MouseMove, QPoint(5, 5), QPoint(5, 5),
                       Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        sendToObject(receiver, ev);
        EXPECT_EQ(source->capture()->bufferedCount(), 1u);
    }  // source destroyed: filter removed

    // After destruction the same event is delivered WITHOUT capture (Qt
    // delivers normally 鈥?sendEvent result unchanged, no buffer exists).
    QObject receiver;
    QMouseEvent ev(QEvent::MouseMove, QPoint(6, 6), QPoint(6, 6),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    const bool handled = sendToObject(receiver, ev);
    EXPECT_EQ(handled, false);  // receiver did not consume it (dumb)
    // Nothing to capture: the (now destroyed) filter's buffer is gone.
    EXPECT_EQ(loop.queue().size(), 0u);
}

// --- 搂7.4 #15a: inactive poll does not pump or capture -----------------------

TEST_CASE(qt_source_inactive_poll_does_not_pump_or_capture)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);

    // Source constructed but NOT activated: poll must not pump Qt events.
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    source->poll(loop.queue());
    EXPECT_EQ(loop.queue().size(), 0u);

    // Even with a pending Qt event, an inactive source never captures it.
    QObject receiver;
    QMouseEvent ev(QEvent::MouseMove, QPoint(7, 7), QPoint(7, 7),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    sendToObject(receiver, ev);

    source->poll(loop.queue());  // still inactive: no pump, no capture
    EXPECT_EQ(loop.queue().size(), 0u);

    // After deactivate(): same guarantee (idempotent, 搂3.4 contract #6).
    EXPECT_EQ(source->activate(), true);
    QMouseEvent evActive(QEvent::MouseMove, QPoint(8, 8), QPoint(8, 8),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    sendToObject(receiver, evActive);
    EXPECT_EQ(source->capture()->bufferedCount(), 1u);  // active: captured
    source->deactivate();  // clears the buffer (搂3.4 contract #6)

    QMouseEvent ev2(QEvent::MouseMove, QPoint(9, 9), QPoint(9, 9),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    sendToObject(receiver, ev2);
    source->poll(loop.queue());  // inactive again: no pump, no capture
    EXPECT_EQ(source->capture()->bufferedCount(), 0u);  // buffer cleared + no capture
    EXPECT_EQ(loop.queue().size(), 0u);  // nothing translated
}

// --- 搂7.4 #16: in-poll re-entry guard (release: skip, no pump/enqueue) ------

TEST_CASE(qt_source_reentry_guard_skips_nested_poll_in_release)
{
    // (audit WARN fix) The nested poll() hits `assert(false)` in DEBUG
    // builds (see poll(): debug terminates, release skips + records kOther).
    // This test exercises the RELEASE leg only; skip under NDEBUG-off so a
    // debug test run does not abort.
#if !defined(NDEBUG)
    return;
#else
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    EXPECT_EQ(source->activate(), true);

    // Deterministic re-entry: a posted MouseMove is delivered INSIDE poll()'s
    // processEvents(); the receiver's event() re-enters poll() while m_inPoll
    // is true. The nested call must be skipped 鈥?nothing pumped, nothing
    // enqueued (搂4.2 濂戠害 #2 / 搂7.4 #16). Release build: the skip is
    // accompanied by a recorded kOther source error (Rev 3 gap close).
    ReentrantReceiver receiver;
    receiver.source = source.get();
    receiver.queue = &loop.queue();

    QMouseEvent* posted = new QMouseEvent(
        QEvent::MouseMove, QPoint(9, 9), QPoint(9, 9), Qt::NoButton,
        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::postEvent(&receiver, posted);

    // One poll: processEvents() delivers the posted event; the receiver's
    // event() re-enters poll() (skipped); the OUTER poll then drains the
    // captured clone into the queue exactly once.
    source->poll(loop.queue());

    // The re-entrant attempt ran (the event was delivered) but produced no
    // extra queue entry: the queue holds exactly the ONE captured clone.
    EXPECT_EQ(receiver.nestedAttempts, 1);
    EXPECT_EQ(loop.queue().size(), 1u);

    // 搂7.4 #16 release leg: the re-entry was recorded as a kOther source
    // error (spec 搂4.2 濂戠害 #2's "璁颁竴娆￠敊璇? 鈥?Rev 3 gap close).
    EXPECT_EQ(source->errorCount(), 1u);
    EXPECT_EQ(source->lastErrorKind(), CtEventErrorKind::kOther);

    // findQueuedTypedEvent LEAVES the match queued (by design); pop it and
    // confirm the queue is now empty 鈥?nothing was duplicated by the
    // (skipped) nested poll. (M4: the MouseMove clone is typed-translated
    // -> CtMousePayload, 搂1.3 淇硶.)
    auto* mouse = findQueuedTypedEvent<CtMousePayload>(loop.queue());
    EXPECT_TRUE(mouse != nullptr);
    // Pop by identity: the helper leaves the match queued, so the front IS
    // that event (queue size == 1); front() + popFront(pe) removes it.
    CtPendingEventBase* pe = nullptr;
    EXPECT_EQ(loop.queue().front(pe), true);
    loop.queue().popFront(pe);  // remove the inspected event
    EXPECT_EQ(loop.queue().size(), 0u);  // fully drained, nothing duplicated
#endif
}

// --- 搂7.4 #17: cross-thread delivery into the main-thread bridge ------------

TEST_CASE(qt_source_cross_thread_post_is_captured_and_translated)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    EXPECT_EQ(source->activate(), true);

    // A Qt object affined to the MAIN thread; a worker thread posts a
    // bridgeable event to it. postEvent is thread-safe (搂4.2 contract #3).
    QObject mainReceiver;

    std::thread worker([&] {
        // Ownership transfers to Qt's event queue (postEvent rule): the
        // event is delivered to mainReceiver on the main thread's next
        // event-loop iteration. Constructed in place (no cross-thread copy).
        auto* ev = new QMouseEvent(QEvent::MouseMove, QPoint(11, 12),
                                   QPoint(11, 12), Qt::NoButton,
                                   Qt::NoButton, Qt::NoModifier);
        QCoreApplication::postEvent(&mainReceiver, ev);
    });
    worker.join();

    // The main-thread bridge source pumps: processEvents() delivers the
    // posted event to mainReceiver (filter captures the clone), then drain
    // translates it into the queue (搂4.2 contract #3). (M4: the MouseMove
    // is typed-translated -> CtMousePayload, 搂1.3 淇硶.)
    source->poll(loop.queue());

    auto* mouse = findQueuedTypedEvent<CtMousePayload>(loop.queue());
    EXPECT_TRUE(mouse != nullptr);
    if (mouse) {
        EXPECT_EQ(mouse->payload().action, CtMouseAction::kMove);
        EXPECT_EQ(mouse->payload().x, 11);
        EXPECT_EQ(mouse->payload().y, 12);
    }
}

// --- M4 S2: key press is typed-translated into CtKeyPayload (06 搂6.2) -------

TEST_CASE(qt_source_key_press_translates_to_key_payload)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    EXPECT_EQ(source->activate(), true);

    QObject receiver;
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
    sendToObject(receiver, ev);

    source->poll(loop.queue());

    auto* key = findQueuedTypedEvent<CtKeyPayload>(loop.queue());
    EXPECT_TRUE(key != nullptr);
    if (key) {
        EXPECT_EQ(key->payload().action, CtKeyAction::kPress);
        EXPECT_EQ(key->payload().key, Qt::Key_A);
        EXPECT_EQ(key->payload().text, QString("a"));
    }
}

// --- M4 S3: unregistered-but-bridgeable type -> identity CtQtEventPayload ---

TEST_CASE(qt_source_unregistered_type_falls_back_to_identity)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    EXPECT_EQ(source->activate(), true);

    // QEvent::Enter (type 10) is NOT registered by installDefaults and NOT
    // ignored -> the M3 identity fallback still applies: the queued event is
    // the raw CtQtEventPayload (06 搂6.2 S3, M3 compatibility).
    QObject receiver;
    QEvent enter(QEvent::Enter);
    sendToObject(receiver, enter);

    source->poll(loop.queue());

    auto* qev = findQueuedQtEvent(loop.queue(), QEvent::Enter);
    EXPECT_TRUE(qev != nullptr);
    if (qev) {
        EXPECT_EQ(qev->payload().hasEvent(), true);
        EXPECT_EQ(qev->payload().qtType(), static_cast<int>(QEvent::Enter));
    }
}

} // namespace (anonymous)

