#pragma once

// qt_sink.h — CRISP -> Qt direction helper (M3, §3.5).
//
// Part of the CRISP project. postToQt is a thin typed wrapper around
// QCoreApplication::postEvent with std::unique_ptr<QEvent> RAII ownership
// semantics: the Qt event queue takes ownership after postEvent returns
// (never touch the pointer afterwards). Safe to call inside a CRISP handler
// (deferred delivery, NOT re-entrant — §2.3 contract #4).
//
// NOT provided: sendToQt — QCoreApplication::sendEvent needs same-thread +
// stack events, semantics differ from CRISP send; applications use
// QCoreApplication::sendEvent directly (M3 does not wrap it).

#include <QCoreApplication>
#include <QEvent>
#include <QObject>

#include <memory>

namespace crisp::event {

/// Post an owned Qt event to a QObject (thin RAII wrapper around
/// QCoreApplication::postEvent — the queue takes ownership, matching Qt's
/// postEvent ownership rule: never touch the pointer afterwards).
/// Returns true if the event was accepted by Qt (non-null receiver).
bool postToQt(QObject* receiver, std::unique_ptr<QEvent> event,
              int priority = 0);

/// Convenience: post a generic (typeless) Qt event of a given type.
/// Builds a QEvent(type) internally and posts it.
bool postToQt(QObject* receiver, QEvent::Type type, int priority = 0);

} // namespace crisp::event
