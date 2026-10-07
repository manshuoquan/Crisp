// qt_event_payload.cpp — non-template CtQtEventPayload implementation.
//
// Part of the CRISP project, M3 Qt bridge (docs/event-system/05-qt-bridge.md
// §3.1). The class is declared in qt_event_payload.h; only the members that
// cannot be defaulted inline live here (AGENTS.md .h/.cpp split).

#include "qt_event_payload.h"

namespace crisp::event {

CtQtEventPayload::CtQtEventPayload(std::unique_ptr<QEvent> event) noexcept
    : m_event(std::move(event)) {}

int CtQtEventPayload::qtType() const noexcept {
    return m_event ? static_cast<int>(m_event->type()) : 0;
}

} // namespace crisp::event
