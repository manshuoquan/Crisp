// qt_sink.cpp — postToQt implementation (M3, §3.5).
//
// Part of the CRISP project. Both overloads are thin RAII wrappers around
// QCoreApplication::postEvent (§2.3 mode two).

#include "qt_sink.h"

#include <utility>

namespace crisp::event {

bool postToQt(QObject* receiver, std::unique_ptr<QEvent> event,
              int priority) {
    if (receiver == nullptr || !event)
        return false;
    // Ownership transfer: Qt's queue deletes the event after delivery.
    // The unique_ptr release is the ONLY sanctioned escape of the RAII
    // wrapper (the queue becomes the owner, matching Qt's postEvent rule).
    QCoreApplication::postEvent(receiver, event.release(), priority);
    return true;
}

bool postToQt(QObject* receiver, QEvent::Type type, int priority) {
    if (receiver == nullptr)
        return false;
    return postToQt(receiver, std::unique_ptr<QEvent>(new QEvent(type)),
                    priority);
}

} // namespace crisp::event
