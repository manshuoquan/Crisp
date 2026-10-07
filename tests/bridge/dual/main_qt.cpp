// main_qt.cpp — dual-run verification, PURE Qt backend (M3, §6.1/§6.2).
//
// Part of the CRISP project. Drives the shared CtDualModel from Qt widget
// event handlers (mouseMoveEvent/keyPressEvent/timerEvent) using the SAME
// deterministic event sequence as the bridge backend. The model log() is
// written to a file given by --log=<path>; ctest compares this file against
// the bridge backend's output byte-for-byte (§6.2).

#include "business_logic.h"

#include <QApplication>
#include <QEvent>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QTimerEvent>
#include <QWidget>
#include <QtGlobal>  // qRound

#include <cstdio>
#include <fstream>
#include <string>

namespace {

/// Qt-side widget: runs the business logic directly in the event handlers.
class CtDualWidget : public QWidget {
public:
    explicit CtDualWidget(CtDualModel& model, bool& quitFlag)
        : m_model(model), m_quit(quitFlag) {}

    /// Arm the timeout that terminates the run (drives model.onTimeout).
    void armTimeout() { m_timer.start(10, this); }

protected:
    void mouseMoveEvent(QMouseEvent* ev) override {
        const QPointF pos = ev->position();
        m_model.onMouseMove(qRound(pos.x()), qRound(pos.y()));
    }
    void keyPressEvent(QKeyEvent* ev) override {
        m_model.onKeyPress(ev->key());
    }
    void timerEvent(QTimerEvent* ev) override {
        if (ev->timerId() == m_timer.timerId()) {
            m_model.onTimeout();
            m_quit = true;
        }
    }

private:
    CtDualModel& m_model;
    bool& m_quit;
    QBasicTimer m_timer;
};

/// Drives the fixed, reproducible sequence: 3 mouse moves, 2 key presses,
/// then one timeout. Identical to the bridge backend (§6.2).
void runDriver(CtDualModel& model, bool& quitFlag) {
    CtDualWidget widget(model, quitFlag);
    widget.resize(100, 100);
    widget.setMouseTracking(true);  // receive ALL mouse moves (not only
                                    // while a button is held, §6.2)
    widget.show();

    // Mouse moves (fixed coordinates).
    QMouseEvent m1(QEvent::MouseMove, QPoint(10, 20), QPoint(10, 20),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &m1);

    QMouseEvent m2(QEvent::MouseMove, QPoint(30, 40), QPoint(30, 40),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &m2);

    QMouseEvent m3(QEvent::MouseMove, QPoint(50, 60), QPoint(50, 60),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &m3);

    // Key presses (fixed keys).
    QKeyEvent k1(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &k1);

    QKeyEvent k2(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &k2);

    // Timeout: widget.timerEvent fires -> onTimeout -> quit.
    widget.armTimeout();

    // Pump Qt events (no CRISP loop in this backend).
    int guard = 0;
    while (!quitFlag && guard++ < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }
}

/// Parse --log=<path> from argv.
std::string parseLogPath(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const std::string prefix = "--log=";
        if (arg.compare(0, prefix.size(), prefix) == 0)
            return arg.substr(prefix.size());
    }
    return "qt.log";
}

} // namespace (anonymous)

int main(int argc, char** argv) {
    // QApplication (NOT QCoreApplication): the backend uses a QWidget
    // receiver, and Qt 6 fail-fasts (0xC0000602) when a QWidget is created
    // under a plain QCoreApplication.
    QApplication app(argc, argv);

    CtDualModel model;
    bool quitFlag = false;
    runDriver(model, quitFlag);

    const std::string path = parseLogPath(argc, argv);
    std::ofstream out(path.c_str(), std::ios::trunc);
    for (const std::string& line : model.log())
        out << line << "\n";
    out.close();

    // Exit code: 0 on consistent final state (used by ctest).
    const bool ok = model.mMouseMoves == 3 && model.mKeyPresses == 2 &&
                    model.mTimeouts == 1;
    std::printf("qt backend: moves=%d keys=%d timeouts=%d -> %s\n",
                model.mMouseMoves, model.mKeyPresses, model.mTimeouts,
                ok ? "OK" : "BAD");
    return ok ? 0 : 2;
}
