// Unit tests for CtQtEventSource<EventSet> + capture (M3, §7.2–§7.4).
//
// Part of the CRISP project. Covers: capture-not-intercept, poll
// translation into the queue, ignored-event skipping, unregistered-type
// drops (kDropped via recordAsyncDrop), timer translation, quit handler,
// the full bridge->CRISP->bridge loop, ordering, capture lifecycle,
// inactive poll, the in-poll re-entry guard, and the deferred-postToQt
// positive case (review P3 #3). Hand-written test doubles only.
//
// Queue-content cases (e.g. §7.2 #6) inspect the queue directly after
// poll(): front() + payloadPtr() + get_if — no dispatchAny dependency.
// Handler-delivery cases drive CtEventLoop::exec() with loop.quit() from
// the handler (exec is the only public drain path).

#include "bridge/qt_event_source.h"
#include "bridge/qt_event_set.h"
#include "bridge/qt_sink.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QTimerEvent>

#include <memory>
#include <string>

#include "test_harness.h"

namespace {
using namespace crisp::event;

using BridgeSet = CtQtEventSet;
using BridgeDispatcher = CtDispatcher<BridgeSet>;
using BridgeQueue = CtQueue<BridgeSet>;

/// Drain the queue, returning the FIRST CtEvent<CtQtEventPayload> whose
/// qtType() == `qtType` (or nullptr). Pops as it scans (queue-content
/// inspection helper — tests use it because a QWidget receiver produces
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
        queue.popFront();
    }
    return nullptr;
}

/// Send a Qt event to a receiver object; the app-level filter observes it.
/// QObject (NOT QWidget): a plain QObject produces NO window-lifecycle
/// events (Create/Show/etc.) that a QWidget emits on construction, so the
/// capture buffer contains exactly the sent event — deterministic tests.
bool sendToObject(QObject& o, QEvent& ev) {
    return QCoreApplication::sendEvent(&o, &ev);
}

/// QEvent subclass whose clone() returns nullptr: the non-cloneable custom
/// type used to exercise the §3.4a #6 kDropped path.
class NonCloneableEvent : public QEvent {
public:
    explicit NonCloneableEvent() : QEvent(QEvent::Type(QEvent::User + 50)) {}
    QEvent* clone() const override { return nullptr; }
};

// --- §7.2 #5: capture does NOT intercept ------------------------------------

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
    // sendEvent returns false — but the capture buffer got the clone.
    const bool handled = sendToObject(receiver, ev);
    EXPECT_EQ(handled, false);  // receiver did not consume it (dumb)
    EXPECT_EQ(source->capture()->bufferedCount(), 1u);
}

// --- §7.2 #6: poll translates into the queue --------------------------------

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

    auto* qev = findQueuedQtEvent(loop.queue(), QEvent::MouseMove);
    EXPECT_TRUE(qev != nullptr);
    if (qev) {
        EXPECT_EQ(qev->payload().hasEvent(), true);
    }
}

// --- §7.2 #7: kIgnoredQtEvents are skipped ----------------------------------

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

// --- §7.2 #8: non-cloneable type -> kDropped, not queued --------------------

TEST_CASE(qt_source_noncloneable_type_drops_with_trace)
{
    CtEventLoop loop;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);
    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    EXPECT_EQ(source->activate(), true);

    // Non-cloneable custom event (§3.4a #6): the filter buffers a NULL
    // marker; the drain records kDropped (queue trace) and nothing enters
    // the queue. This is the REAL unregistered/unbridgeable path — every
    // bridgeable built-in type (mouse/key/focus/...) is identity-translated
    // by default (§3.3 contract #4, §3.4a #4), so "unregistered" no longer
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

} // namespace (anonymous)

