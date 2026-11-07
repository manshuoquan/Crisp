// CRISP Qt bridge demo (M3/M4, needs Qt6).
//
// Shows both directions of the bridge:
//   Qt -> CRISP: CtQtEventSource<EventSet> captures Qt events (QEvent via
//                an application-level event filter), translates them into
//                strong-typed payloads (CtQtEventPayload raw identity /
//                CtMousePayload / CtKeyPayload) and injects them into the
//                CRISP CtEventLoop's queue;
//   CRISP -> Qt: postToQt() posts an owned QEvent to a QObject (RAII
//                ownership transfer into Qt's event queue).
//
// The demo posts a few QEvents directly (QCoreApplication::postEvent) to
// simulate platform traffic, runs the CRISP loop, and shows both the CRISP
// handlers and a Qt event handler receiving the translated events.
//
// New-API invariants:
//   - the source's EventSet MUST contain CtQtEventPayload + CtMousePayload
//     + CtKeyPayload (static_asserted in qt_event_source.h);
//   - the source must be activated before the loop runs;
//   - QCoreApplication must exist (the source asserts it).
//
// Build & run (requires Qt6 configured at CMake time):
//   cmake -S . -B build -G "MinGW Makefiles"
//   cmake --build build --parallel 2
//   ./build/examples/qt_bridge_demo/qt_bridge_demo.exe

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QObject>
#include <QTimerEvent>

#include <cstdint>
#include <cstdio>
#include <memory>

#include "bridge/qt_bridge.h"       // CtQtEventSource / CtQtEventSet / postToQt
#include "event/ct_dispatcher.h"
#include "event/ct_event.h"
#include "event/ct_event_loop.h"
#include "event/ct_event_set.h"
#include "event/ct_handler_token.h"
#include "event/ct_receiver.h"

using crisp::event::CtDispatcher;
using crisp::event::CtEvent;
using crisp::event::CtEventLoop;
using crisp::event::CtEventSet;
using crisp::event::CtQtEventPayload;
using crisp::event::CtQtEventSet;
using crisp::event::CtQtEventSource;
using crisp::event::CtReceiver;
using crisp::event::createReceiver;
using crisp::event::postToQt;

// ---------------------------------------------------------------------------
// A CRISP receiver: handles the strong-typed events the bridge delivers.
// The loop reference lets the Timer handler terminate exec() deterministically
// (the canonical dual-backend pattern, tests/bridge/dual/main_bridge.cpp).
// ---------------------------------------------------------------------------
class CtQtSink : public CtReceiver {
public:
    explicit CtQtSink(CtEventLoop& loop) : m_loop(loop) {}

    /// Typed mouse handler (M4): CtMousePayload carries the action + x/y.
    void onMouse(const crisp::event::CtMousePayload& payload) {
        std::printf("  [crisp] mouse %s at (%d, %d) button=%d\n",
                    payload.action == crisp::event::CtMouseAction::kPress
                        ? "press"
                        : (payload.action == crisp::event::CtMouseAction::kRelease
                               ? "release"
                               : "move"),
                    payload.x, payload.y, static_cast<int>(payload.button));
    }

    /// Typed key handler (M4): CtKeyPayload carries Qt::Key + text.
    void onKey(const crisp::event::CtKeyPayload& payload) {
        std::printf("  [crisp] key 0x%x %s text=\"%s\"\n",
                    static_cast<unsigned int>(payload.key),
                    payload.action == crisp::event::CtKeyAction::kPress
                        ? "press"
                        : "release",
                    payload.text.toUtf8().constData());
    }

    /// Raw Qt payload handler: events that stay on the identity path.
    /// Besides the Timer event (kept raw by the default translators), any
    /// User+ UNREGISTERED bridgeable type also falls back to identity, so
    /// our synthetic `QEvent::User + 1` arrives here. The Timer event also
    /// terminates the loop (dual-backend pattern).
    void onQtEvent(const CtQtEventPayload& payload) {
        std::printf("  [crisp] raw Qt event type=%d\n", payload.qtType());
        QEvent* qe = payload.event();
        if (qe && qe->type() == QEvent::Timer)
            m_loop.quit(); // terminate exec() after the timeout event
    }

private:
    CtEventLoop& m_loop;
};

// ---------------------------------------------------------------------------
// A Qt object that receives events posted from the CRISP side.
// ---------------------------------------------------------------------------
class QtEchoObject : public QObject {
public:
    bool event(QEvent* ev) override {
        if (ev->type() == QEvent::User) {
            std::printf("  [qt] received posted QEvent::User (from CRISP)\n");
            return true;
        }
        return QObject::event(ev);
    }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // The bridge's default event set: timer + raw Qt event + typed mouse/key.
    using BridgeEventSet = CtQtEventSet;
    using BridgeDispatcher = crisp::event::CtDispatcher<BridgeEventSet>;

    // Attach a CRISP loop on the main thread; the Qt source injects into it.
    CtEventLoop loop;
    loop.attachToCurrentThread();
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    loop.setDispatcher(dispatcher);

    auto sink = createReceiver<CtQtSink>(loop);
    auto tMouse = dispatcher->addHandler<crisp::event::CtMousePayload>(
        sink, [&sink](CtEvent<crisp::event::CtMousePayload>& ev) {
            sink->onMouse(ev.payload());
            ev.accept();
        });
    auto tKey = dispatcher->addHandler<crisp::event::CtKeyPayload>(
        sink, [&sink](CtEvent<crisp::event::CtKeyPayload>& ev) {
            sink->onKey(ev.payload());
            ev.accept();
        });
    auto tRaw = dispatcher->addHandler<CtQtEventPayload>(
        sink, [&sink](CtEvent<CtQtEventPayload>& ev) {
            sink->onQtEvent(ev.payload());
            ev.accept();
        });

    // Install the Qt event capture. The source's filter clones events that
    // pass through QCoreApplication::notify() (sendEvent or postEvent +
    // processEvents) and the CRISP loop's poll() translates them into the
    // queue.
    auto source = std::make_shared<CtQtEventSource<BridgeEventSet>>(loop);
    loop.attachSource(source);
    if (!source->activate())
        return 3;

    // Simulate platform traffic with SYNCHRONOUS sendEvent: each event
    // walks the capture -> translate -> queue path immediately, but the
    // queue is only DRAINED (dispatched) by loop.exec() below (the loop
    // drains first, then polls the Qt source). A plain QObject sink emits
    // no window-lifecycle noise.
    std::printf("== send synthetic Qt events ==\n");
    QObject qtSink;

    QMouseEvent m1(QEvent::MouseButtonPress, QPoint(10, 20), QPoint(10, 20),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&qtSink, &m1);
    QMouseEvent m2(QEvent::MouseButtonRelease, QPoint(10, 20), QPoint(10, 20),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&qtSink, &m2);

    QKeyEvent k1(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QString("a"));
    QCoreApplication::sendEvent(&qtSink, &k1);

    // A raw (untranslated) event: identity path keeps the QEvent.
    QEvent raw(QEvent::Type(QEvent::User + 1));
    QCoreApplication::sendEvent(&qtSink, &raw);

    // Timeout: a QTimerEvent. Posted events would need the Qt main loop to
    // pump them (the CRISP loop blocks when its queue is empty), so send it
    // synchronously - it walks the same capture path and the CRISP Timer
    // handler terminates the loop deterministically. NOTE: the Timer event
    // is only delivered once the Qt source's poll() calls
    // QCoreApplication::processEvents() (each exec() iteration does).
    QTimerEvent te(1);
    QCoreApplication::sendEvent(&qtSink, &te);

    // Single event-loop driver: exec() drains + polls until the Timer
    // handler calls loop.quit().
    const int iterations = loop.exec();
    std::printf("  loop ran %d drain iterations\n", iterations);

    // CRISP -> Qt direction: post a custom QEvent to a QObject (the Qt
    // event queue takes ownership; app.processEvents() delivers it).
    std::printf("== postToQt (CRISP -> Qt) ==\n");
    QtEchoObject echo;
    postToQt(&echo, std::make_unique<QEvent>(QEvent::Type(QEvent::User)));
    app.processEvents(); // deliver the posted event to echo

    (void)tMouse; (void)tKey; (void)tRaw;
    return 0;
}
