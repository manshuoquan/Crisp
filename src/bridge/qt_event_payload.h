#pragma once

// qt_event_payload.h — bridge payload owning exactly one QEvent (RAII).
//
// Part of the CRISP project, M3 Qt bridge (docs/event-system/05-qt-bridge.md
// §3.1). QEvent is not safely copyable (Q_EVENT_DISABLE_COPY); the bridge
// creates payloads from QEvent::clone() or from application-provided
// translators. Move-only; stored inside CtEvent<CtQtEventPayload> by value
// (no extra heap).

#include <QEvent>
#include <memory>
#include <string_view>

#include "ct_event.h"      // CtEventTraits primary template
#include "ct_type_key.h"   // CRISP_EVENT_TYPE macro

namespace crisp::event {

/// Bridge payload: owns exactly one QEvent (RAII via std::unique_ptr).
/// Move-only — QEvent is not safely copyable in general; the bridge creates
/// payloads from QEvent::clone() or from application-provided translators.
/// Stored inside CtEvent<CtQtEventPayload>; the queue stores it by value
/// in the CtAnyEvent variant (move semantics, no extra heap).
class CtQtEventPayload {
public:
    /// Wrap an owned QEvent (nullptr permitted as an "empty marker").
    explicit CtQtEventPayload(std::unique_ptr<QEvent> event) noexcept;

    CtQtEventPayload(CtQtEventPayload&&) noexcept = default;
    CtQtEventPayload& operator=(CtQtEventPayload&&) noexcept = default;

    CtQtEventPayload(const CtQtEventPayload&) = delete;
    CtQtEventPayload& operator=(const CtQtEventPayload&) = delete;

    /// The owned event (nullptr if the payload was default-created).
    QEvent* event() const noexcept { return m_event.get(); }

    /// Qt event type id (QEvent::Type as int); 0 (QEvent::None) for null.
    int qtType() const noexcept;

    /// True if this payload owns an event.
    bool hasEvent() const noexcept { return m_event != nullptr; }

    /// Release ownership (used by the CRISP->Qt direction helpers, §3.5).
    std::unique_ptr<QEvent> release() noexcept { return std::move(m_event); }

private:
    std::unique_ptr<QEvent> m_event;  ///< owned Qt event (RAII)
};

} // namespace crisp::event

/// EventTraits specialization — required by CtEvent<CtQtEventPayload>.
/// (File scope, OUTSIDE namespace crisp::event: the CRISP_EVENT_TYPE macro
/// opens `namespace crisp::event { ... }` itself, see ct_type_key.h.)
CRISP_EVENT_TYPE(crisp::event::CtQtEventPayload, "QtEvent");
