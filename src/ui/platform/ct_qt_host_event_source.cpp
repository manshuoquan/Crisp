// ct_qt_host_event_source.cpp - UI3 Qt-host event source (platform layer).
// Pumps Qt events, translates them through the M4 translator + mapBridge*
// into crisp::input core payloads, and strong-typed pushes them into the
// CtUiEventSet queue (W-1: NOT the M4 CtQtEventSource - bridge EventSet
// differs; see header comment).
// Authoritative spec: docs/ui/06-ui3-architecture.md section 4.4 (W-1).

#include "ct_qt_host_event_source.h"

#include <cassert>

#include "event/ct_event_loop.h"   // CtEventLoop
#include "event/ct_receiver.h"     // std::weak_ptr<CtReceiver> (broadcast)
#include "event/ct_type_key.h"     // CtTypeKey (eventSetId assertion)
#include "ct_input_mapper_bridge.h" // mapBridge* (Qt-host mapper)

using crisp::event::CtEvent;
using crisp::event::CtEventLoop;
using crisp::event::CtEventOrigin;
using crisp::event::CtQueue;
using crisp::event::CtQueueBase;
using crisp::event::CtReceiver;
using crisp::event::CtTypeKey;

namespace crisp::ui {

// ---------------------------------------------------------------------------
// CtQtHostCapture<EventSet> template members
// ---------------------------------------------------------------------------

template <typename EventSet>
bool CtQtHostCapture<EventSet>::eventFilter(
    [[maybe_unused]] QObject* watched, QEvent* event) {
    // No sink (source destroyed / not constructed) -> do NOT capture.
    // qApp null -> the filter is being torn down, do NOT dereference the
    // sink's translator table. NEVER throws (Qt filters must not throw).
    if (m_sink == nullptr || event == nullptr ||
        QCoreApplication::instance() == nullptr)
        return false;

    const crisp::event::CtQtTypeKey qtType =
        static_cast<crisp::event::CtQtTypeKey>(event->type());

    // Registry lookup FIRST (a registered translator re-enables an
    // otherwise-ignored type, M4 section 3.4a merge order).
    if (m_sink->translatorTable().find(qtType) != nullptr)
        return captureClone(event);

    // Quit is special - never cloned, tracked via the quit flag.
    if (event->type() == QEvent::Quit) {
        m_quitCaptured = true;
        return false;  // never intercept
    }

    // Unregistered + ignored: skip (never cloned, never buffered).
    if (crisp::event::isIgnoredQtEvent(qtType))
        return false;

    // Everything else: default capture as raw payload (M3 identity path).
    return captureClone(event);
}

template <typename EventSet>
bool CtQtHostCapture<EventSet>::captureClone(QEvent* event) {
    std::unique_ptr<QEvent> clone(event->clone());
    m_buffer.push_back(std::move(clone));
    return false;  // ALWAYS let Qt continue delivery
}

template <typename EventSet>
void CtQtHostCapture<EventSet>::drain(
    crisp::event::CtQueue<EventSet>& queue) {
    if (m_sink == nullptr)
        return;  // no sink: nothing to translate into

    std::vector<std::unique_ptr<QEvent>> batch;
    batch.swap(m_buffer);  // move the whole buffer out (drain point)

    for (auto& ev : batch) {
        // Pass the (possibly null) marker through: a null entry is a
        // captured-but-non-cloneable event - translateAndPush records
        // kDropped for it.
        m_sink->translateAndPush(queue, std::move(ev));
    }
    // Clear point: batch destructs here - the buffer is empty again.
}

// ---------------------------------------------------------------------------
// CtQtHostEventSource<EventSet> template members
// ---------------------------------------------------------------------------

template <typename EventSet>
CtQtHostEventSource<EventSet>::CtQtHostEventSource(CtEventLoop& loop)
    : m_loop(loop) {
    // The M4 defaults cover the built-in bridgeable types (mouse/key/timer
    // identity translations); install them so registered typed translators
    // produce bridge payloads for mapBridge*.
    crisp::event::CtQtTranslatorTable::installDefaults(m_translators);
    m_capture = std::make_unique<CtQtHostCapture<EventSet>>();
    m_capture->setSink(this);
}

template <typename EventSet>
CtQtHostEventSource<EventSet>::~CtQtHostEventSource() {
    deactivate();  // removes the event filter while qApp may still be alive
}

template <typename EventSet>
void CtQtHostEventSource<EventSet>::poll(CtQueueBase& queue) {
    if (!m_active)
        return;

    // EventSet identity assert BEFORE the downcast (M2 section 7.12
    // contract #6): the UI loop queue MUST be CtQueue<CtUiEventSet>.
    if (queue.eventSetId() != CtTypeKey<EventSet>::value()) {
        assert(false && "CtQtHostEventSource polled against a queue of a "
                        "different EventSet");
        return;
    }
    auto& concrete = static_cast<CtQueue<EventSet>&>(queue);

    // Pump Qt's event loop once (spontaneous + posted events fire here).
    QCoreApplication::processEvents();

    // Drain the capture buffer via the public drain() (M4 bridge
    // precedent): translate + strong-typed push.
    if (m_capture)
        m_capture->drain(concrete);

    // Quit handling: a captured QEvent::Quit stops the UI loop.
    if (m_capture && m_capture->quitCaptured()) {
        m_capture->clearQuitCaptured();
        m_loop.quit();
    }
}

template <typename EventSet>
bool CtQtHostEventSource<EventSet>::activate() {
    if (m_active)
        return true;  // idempotent

    QCoreApplication* app = QCoreApplication::instance();
    if (app == nullptr)
        return false;  // no qApp - do NOT set m_active

    app->installEventFilter(m_capture.get());
    m_active = true;
    return true;
}

template <typename EventSet>
void CtQtHostEventSource<EventSet>::deactivate() noexcept {
    if (!m_active)
        return;

    // Remove the filter ONLY while qApp is still alive (a null qApp in
    // eventFilter would be UB).
    if (QCoreApplication::instance() != nullptr && m_capture)
        QCoreApplication::instance()->removeEventFilter(m_capture.get());

    if (m_capture) {
        m_capture->clearQuitCaptured();
        m_capture->clearBuffer();
    }
    m_active = false;
}

template <typename EventSet>
std::string_view CtQtHostEventSource<EventSet>::sourceName() const noexcept {
    return "CtQtHostEventSource";
}

template <typename EventSet>
void CtQtHostEventSource<EventSet>::translateAndPush(
    CtQueue<EventSet>& queue, std::unique_ptr<QEvent> ev) {
    // Null marker = a captured event whose clone() failed: a real
    // observable loss -> record kDropped (the bridge cannot construct
    // CtDispatchContext; recordAsyncDrop is the public source-side hook).
    if (!ev) {
        queue.recordAsyncDrop(CtTypeKey<crisp::event::CtQtEventPayload>::value());
        return;
    }

    const crisp::event::CtQtTypeKey qtType =
        static_cast<crisp::event::CtQtTypeKey>(ev->type());

    // Registry lookup first; unregistered-but-bridgeable types fall back
    // to the identity translator (M3 contract #4). Ignored types never
    // reach here (the capture filter skips them - defensive drop).
    const crisp::event::CtQtTranslator* translator =
        m_translators.find(qtType);
    if (translator == nullptr && !crisp::event::isIgnoredQtEvent(qtType))
        translator = crisp::event::CtQtTranslatorTable::identityTranslator();

    if (translator == nullptr) {
        // Unregistered type -> kDropped (queue trace), event NOT pushed.
        queue.recordAsyncDrop(
            CtTypeKey<crisp::event::CtQtEventPayload>::value());
        return;
    }

    const bool spontaneous = ev->spontaneous();
    crisp::event::CtQtTypedPayload bridgePayload =
        (*translator)(std::move(ev), spontaneous);

    // Map the bridged payload to the core crisp::input payload via
    // mapBridge* (THE single sanctioned mapping boundary, D3), then
    // strong-typed push (CtUiEventSet).
    const CtEventOrigin origin =
        spontaneous ? CtEventOrigin::kSystem : CtEventOrigin::kApplication;

    std::visit(
        [&](auto&& p) {
            using P = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<P, crisp::event::CtMousePayload>) {
                // mapBridgeMouse ALWAYS produces a CtMousePayload (pure
                // function; the bridge mouse field set is complete) - but
                // guard the get anyway (a future signature change must not
                // throw bad_variant_access through the Qt event stack).
                auto result = platform::mapBridgeMouse(p, crisp::ui::CtSize{});
                if (!std::holds_alternative<crisp::input::CtMousePayload>(
                        result)) {
                    queue.recordAsyncDrop(CtTypeKey<
                        crisp::input::CtMousePayload>::value());
                    return;
                }
                crisp::input::CtMousePayload coreMouse =
                    std::move(std::get<crisp::input::CtMousePayload>(result));
                queue.push(std::weak_ptr<CtReceiver>{},
                           CtEvent<crisp::input::CtMousePayload>(
                               origin, std::move(coreMouse)));
            } else if constexpr (std::is_same_v<P, crisp::event::CtKeyPayload>) {
                auto result = platform::mapBridgeKey(p);
                if (!std::holds_alternative<crisp::input::CtKeyPayload>(
                        result)) {
                    queue.recordAsyncDrop(CtTypeKey<
                        crisp::input::CtKeyPayload>::value());
                    return;
                }
                crisp::input::CtKeyPayload coreKey =
                    std::move(std::get<crisp::input::CtKeyPayload>(result));
                queue.push(std::weak_ptr<CtReceiver>{},
                           CtEvent<crisp::input::CtKeyPayload>(
                               origin, std::move(coreKey)));
            } else {
                // CtQtEventPayload (identity path - non mouse/key Qt
                // events, e.g. focus/timer). UI3 does NOT re-map these to
                // core payloads: they are out of the UI input axis. Drop
                // them observably (they never enter the CtUiEventSet queue
                // - CtQtEventPayload is not a member of the UI EventSet).
                queue.recordAsyncDrop(
                    CtTypeKey<crisp::event::CtQtEventPayload>::value());
            }
        },
        bridgePayload);
}

// S-3 explicit instantiation: the template classes' vtable/out-of-line
// members must be emitted (headers live in a library; instantiation point
// is the app). MUST-2: instantiated with CtUiEventSet. The capture filter's
// members are instantiated too (its eventFilter/drain are out-of-line).
template class CtQtHostEventSource<crisp::ui::CtUiEventSet>;
template class CtQtHostCapture<crisp::ui::CtUiEventSet>;

} // namespace crisp::ui
