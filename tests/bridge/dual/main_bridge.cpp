// main_bridge.cpp — dual-run verification, CRISP+BRIDGE backend (M3, §6.1).
//
// Part of the CRISP project. Uses the SAME CtDualModel and the SAME driving
// sequence as main_qt.cpp, but the widget is a DUMB receiver (no overridden
// handlers): every Qt event is captured by the bridge, translated into a
// CtEvent<CtQtEventPayload>, injected into the CRISP queue, and delivered to
// a CRISP handler that feeds the model. The log() output must be byte-
// identical to the pure-Qt backend (§6.2/§6.3).
//
// Loop model (§2.4): CtEventLoop::exec() is the single event-loop driver.
// The bridge source's poll() pumps QCoreApplication::processEvents() and
// translates the capture buffer into the queue; exec() drains the queue
// into the dispatcher. The timeout is a posted QTimerEvent (a real Qt
// Timer event that walks the whole bridge path); the CRISP handler for it
// calls loop.quit() (the default quitHandler semantics, §3.4 contract #8).

#include "business_logic.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QObject>
#include <QTimerEvent>
#include <QtGlobal>  // qRound

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

#include "bridge/qt_event_set.h"
#include "bridge/qt_event_source.h"
#include "bridge/qt_event_payload.h"

namespace {
using namespace crisp::event;

using BridgeSet = CtQtEventSet;
using BridgeDispatcher = CtDispatcher<BridgeSet>;

/// CRISP receiver: converts the raw Qt payload into model calls.
class CtDualReceiver : public CtReceiver {
public:
    explicit CtDualReceiver(CtDualModel& model, CtEventLoop& loop)
        : m_model(model), m_loop(loop) {}

    void handleQtEvent(CtEvent<CtQtEventPayload>& ev) {
        QEvent* qe = ev.payload().event();
        if (qe == nullptr)
            return;
        switch (qe->type()) {
        case QEvent::MouseMove: {
            auto* me = static_cast<QMouseEvent*>(qe);
            const QPointF pos = me->position();
            m_model.onMouseMove(qRound(pos.x()), qRound(pos.y()));
            break;
        }
        case QEvent::KeyPress: {
            auto* ke = static_cast<QKeyEvent*>(qe);
            m_model.onKeyPress(ke->key());
            break;
        }
        case QEvent::Timer:
            m_model.onTimeout();
            m_loop.quit();  // terminate exec() after the timeout event
            break;
        default:
            break;
        }
    }

private:
    CtDualModel& m_model;
    CtEventLoop& m_loop;
};

/// Parse --log=<path> from argv.
std::string parseLogPath(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const std::string prefix = "--log=";
        if (arg.compare(0, prefix.size(), prefix) == 0)
            return arg.substr(prefix.size());
    }
    return "bridge.log";
}

} // namespace (anonymous)

int main(int argc, char** argv) {
    // QApplication (NOT QCoreApplication): the backend uses a QWidget
    // receiver, and Qt 6 fail-fasts (0xC0000602) when a QWidget is created
    // under a plain QCoreApplication.
    QApplication app(argc, argv);

    CtDualModel model;
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    CtEventLoop loop;
    loop.setDispatcher(dispatcher);
    loop.attachToCurrentThread();

    auto receiver = createReceiver<CtDualReceiver>(model, loop);
    // KEEP the token alive: CtHandlerToken's destructor UNREGISTERS the
    // handler (RAII, §7.7) — discarding the return value would silently
    // remove the handler before the loop runs, and no event would ever be
    // delivered (drain would drop them as kNoReceiver).
    auto handlerToken = receiver->bindHandler<BridgeSet, CtQtEventPayload>(
        dispatcher,
        [receiver](CtEvent<CtQtEventPayload>& ev) { receiver->handleQtEvent(ev); },
        "dual");

    auto source = std::make_shared<CtQtEventSource<BridgeSet>>(loop);
    loop.attachSource(source);
    if (!source->activate())
        return 3;

    // DUMB receiver: a plain QObject (NOT QWidget) — a QWidget would emit
    // ~19 window-lifecycle events (Create/Show/Move/Resize/Polish/...) on
    // construction+show(), polluting the captured stream. A QObject emits
    // nothing; the business runs ONLY in the CRISP handler (§6.3, no
    // double execution, deterministic capture buffer).
    QObject qtSink;

    // Same deterministic sequence as main_qt.cpp (§6.2): 3 mouse moves,
    // 2 key presses, 1 timeout. sendEvent is synchronous — the capture
    // filter clones into the buffer on the notify() path (never intercepts).
    QMouseEvent m1(QEvent::MouseMove, QPoint(10, 20), QPoint(10, 20),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&qtSink, &m1);

    QMouseEvent m2(QEvent::MouseMove, QPoint(30, 40), QPoint(30, 40),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&qtSink, &m2);

    QMouseEvent m3(QEvent::MouseMove, QPoint(50, 60), QPoint(50, 60),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&qtSink, &m3);

    QKeyEvent k1(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&qtSink, &k1);

    QKeyEvent k2(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier);
    QCoreApplication::sendEvent(&qtSink, &k2);

    // Timeout: a QTimerEvent. Posted events would need the Qt main loop to
    // pump them — but the CRISP loop drives poll() and blocks in
    // waitForEvent() when its queue is empty (posted events never get
    // pumped, §2.4 co-existence). Sending it synchronously walks the same
    // full bridge path (capture -> translate -> queue -> dispatch) and the
    // CRISP handler terminates the loop deterministically.
    QTimerEvent te(1);
    QCoreApplication::sendEvent(&qtSink, &te);

    // Single event-loop driver (§2.4): exec() drains + polls until quit().
    loop.exec();

    const std::string path = parseLogPath(argc, argv);
    std::ofstream out(path.c_str(), std::ios::trunc);
    for (const std::string& line : model.log())
        out << line << "\n";
    out.close();

    // Exit code: 0 on consistent final state (used by ctest).
    const bool ok = model.mMouseMoves == 3 && model.mKeyPresses == 2 &&
                    model.mTimeouts == 1;
    std::printf("bridge backend: moves=%d keys=%d timeouts=%d -> %s\n",
                model.mMouseMoves, model.mKeyPresses, model.mTimeouts,
                ok ? "OK" : "BAD");
    return ok ? 0 : 2;
}
