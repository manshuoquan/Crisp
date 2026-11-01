#pragma once

// ct_qt_host_event_source.h - UI3 Qt-host mode event source (platform
// layer; Qt dependency allowed - README exception clause).
//
// W-1/P7 deviation (docs/ui/06-ui3-architecture.md section 4.4):
// CtQtHostEventSource<CtUiEventSet> is a NEW platform-layer wrapper - it
// MUST NOT reuse the M4 CtQtEventSource directly (the bridge EventSet
// CtQtEventSet differs from the UI EventSet CtUiEventSet; the eventSetId
// assertion would fail). Internally it reuses the M4 translator table
// (CtQtTranslatorTable) and then routes the bridged payloads through
// mapBridge* into crisp::input core payloads (the SAME mapping boundary
// the Win32 mode uses - D3).
//
// The capture filter is a plain QObject event filter (installed on
// qApp); captured QEvents are cloned into a buffer; poll() pumps
// processEvents() once, then drains the buffer: Qt type id -> M4
// translator -> bridge payload -> mapBridge* -> crisp::input payload ->
// strong-typed push (CtUiEventSet). The widget layer after this point is
// IDENTICAL to the Win32 mode (dual-run comparable).

#include <QObject>
#include <QCoreApplication>

#include <memory>
#include <string_view>
#include <vector>

#include "event/ct_event_source.h"   // crisp::event::CtEventSource
#include "event/ct_queue.h"          // crisp::event::CtQueueBase
#include "bridge/qt_translator.h"    // crisp::event::CtQtTranslatorTable
#include "../ct_input_mapper.h"      // crisp::ui::CtUiEventSet / mapper

namespace crisp::event { class CtEventLoop; }  // fwd (member reference only)

namespace crisp::ui {

// Forward declaration (template) for the capture's sink pointer.
template <typename EventSet>
class CtQtHostEventSource;

/// Event filter + capture buffer for CtQtHostEventSource (implementation
/// detail; a QObject subclass installed on qApp). ALWAYS returns false
/// (never intercepts - Qt delivery continues). Captured events are cloned
/// into the buffer for the source's poll() to translate.
/// NO Q_OBJECT: moc rejects Q_OBJECT inside a class template; the filter
/// needs no meta-object (eventFilter is a plain virtual) - same approach
/// as the M4 bridge capture.
template <typename EventSet>
class CtQtHostCapture final : public QObject {
public:
    /// The sink that consumes the buffer (set by CtQtHostEventSource).
    void setSink(CtQtHostEventSource<EventSet>* sink) { m_sink = sink; }

    /// Number of events currently buffered (diagnostics/tests).
    std::size_t bufferedCount() const noexcept { return m_buffer.size(); }

    /// True if QEvent::Quit was captured since the last drain.
    bool quitCaptured() const noexcept { return m_quitCaptured; }

    /// Clear the quit flag (poll step, after the quit handler ran).
    void clearQuitCaptured() noexcept { m_quitCaptured = false; }

    /// Clear the capture buffer (deactivate path).
    void clearBuffer() noexcept { m_buffer.clear(); }

    /// Drain the captured-event buffer through the sink's translateAndPush
    /// (public - the source calls it from poll; M4 bridge precedent).
    void drain(crisp::event::CtQueue<EventSet>& queue);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Clone `event` into the buffer (or set the quit flag for Quit).
    /// Always returns false - the filter NEVER intercepts.
    bool captureClone(QEvent* event);

    std::vector<std::unique_ptr<QEvent>> m_buffer;
    CtQtHostEventSource<EventSet>* m_sink = nullptr;  ///< null-guarded in
                                                      ///< eventFilter/drain
    bool m_quitCaptured = false;
};

/// UI3 Qt-host event source: pumps Qt events, translates them through the
/// M4 translator + mapBridge* into crisp::input core payloads, and
/// strong-typed pushes them into the CtUiEventSet queue.
template <typename EventSet>
class CtQtHostEventSource final : public crisp::event::CtEventSource {
public:
    /// Create with the loop whose queue this source injects into. Does
    /// NOT install the event filter yet (done by activate()).
    /// Requires QCoreApplication::instance() != nullptr (asserts).
    explicit CtQtHostEventSource(crisp::event::CtEventLoop& loop);

    /// Deactivates + removes the event filter (RAII).
    ~CtQtHostEventSource() override;

    /// poll: assert queue.eventSetId() == EventSet key, then pump
    /// processEvents() once and drain the capture buffer: M4 translator ->
    /// mapBridge* -> strong-typed push (main injection path).
    void poll(crisp::event::CtQueueBase& queue) override;

    /// Install the event filter and start capturing (idempotent).
    /// Returns false if qApp is null.
    bool activate() override;

    /// Stop capturing + remove the filter + clear the buffer (idempotent).
    void deactivate() noexcept override;

    std::string_view sourceName() const noexcept override;

    /// Qt event type id -> translator (defaults installed on construction).
    crisp::event::CtQtTranslatorTable& translatorTable() noexcept {
        return m_translators;
    }

    /// The capture object (tests access bufferedCount/quitCaptured).
    CtQtHostCapture<EventSet>* capture() const noexcept {
        return m_capture.get();
    }

private:
    /// Translate one buffered event and push it into `queue` (M4
    /// translator -> mapBridge* -> core payload).
    void translateAndPush(crisp::event::CtQueue<EventSet>& queue,
                          std::unique_ptr<QEvent> ev);

    /// The capture filter drains into this source.
    friend class CtQtHostCapture<EventSet>;

    crisp::event::CtEventLoop& m_loop;      ///< owning loop (queue host)
    crisp::event::CtQtTranslatorTable m_translators;
    std::unique_ptr<CtQtHostCapture<EventSet>> m_capture;
    bool m_active = false;
};

} // namespace crisp::ui
