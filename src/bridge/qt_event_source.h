#pragma once

// qt_event_source.h — template Qt bridge source + capture filter (M3, §3.4).
//
// Part of the CRISP project. Scheme A (Rev 10 P2-A, ruled by review):
// CtQtEventSource<EventSet> carries the EventSet type parameter so poll()
// can assert the queue's eventSetId() and STATIC_CAST to CtQueue<EventSet>&
// for a strong-typed push — the built-in sources never go through the
// erased pushAny boundary (§2.2).
//
// The capture filter (detail::CtQtEventCapture<EventSet>) is a QObject
// subclass WITHOUT Q_OBJECT: Qt 6.2.4's moc rejects Q_OBJECT inside a class
// template ("Template classes not supported by Q_OBJECT"), and the filter
// needs no signals/slots/metadata — it only overrides the plain virtual
// eventFilter(). Template member definitions MUST live in the header
// (language exception to the strict .h/.cpp split, AGENTS.md); non-template
// logic (ignored-list lookup) is delegated to qt_translator.cpp.

#include <QCoreApplication>
#include <QEvent>
#include <QObject>

#include <cassert>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "ct_event.h"
#include "ct_event_loop.h"
#include "ct_event_source.h"
#include "ct_errors.h"
#include "ct_queue.h"
#include "ct_type_key.h"
#include "qt_translator.h"

namespace crisp::event {

/// Forward declaration: the capture filter names the concrete source
/// (CtQtEventSource<EventSet>* m_sink, §3.4 P1-1). Defined below.
template <typename EventSet>
class CtQtEventSource;

namespace detail {

/// Application-level event filter + capture buffer (implementation detail of
/// CtQtEventSource; QObject subclass). eventFilter() ALWAYS returns false
/// (never intercepts — Qt delivery continues normally; §1.3 boundary #4).
/// Captured events are cloned into the buffer for the source's poll() to
/// translate.
/// (Rev 2, P1-1) TEMPLATED on EventSet: the sink pointer must name the
/// concrete CtQtEventSource<EventSet> (a non-template "CtQtEventSourceBase"
/// does not exist — the source is only ever instantiated with an EventSet).
/// NO Q_OBJECT: moc rejects Q_OBJECT inside a class template (Qt 6.2.4),
/// and the filter needs no meta-object — eventFilter() is a plain virtual.
template <typename EventSet>
class CtQtEventCapture : public QObject {
public:
    /// The source that consumes the buffer (set by CtQtEventSource).
    void setSink(CtQtEventSource<EventSet>* sink) { m_sink = sink; }

    /// Drain the captured-event buffer into the sink's queue (called by
    /// CtQtEventSource::poll AFTER processEvents() returns; §3.4 契约 #2).
    void drain(CtQueue<EventSet>& queue);

    /// Number of events currently buffered (diagnostics/tests).
    std::size_t bufferedCount() const noexcept { return m_buffer.size(); }

    /// True if QEvent::Quit was captured since the last drain (cleared by
    /// the source's poll step 5 after the quit handler runs, §3.4a #3).
    bool quitCaptured() const noexcept { return m_quitCaptured; }

    /// Clear the quit flag (source's poll step 5, after the handler ran).
    void clearQuitCaptured() noexcept { m_quitCaptured = false; }

    /// Clear the capture buffer (deactivate path, §3.4 contract #6).
    void clearBuffer() noexcept { m_buffer.clear(); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Clone `event` into the buffer (or set the quit flag for Quit).
    /// Always returns false — the filter NEVER intercepts (§3.4 contract #3).
    bool captureClone(QEvent* event);

    /// Buffer of owned, cloned Qt events captured since the last drain().
    /// (Rev 2, P1-2) Container FIXED: std::vector<std::unique_ptr<QEvent>>.
    std::vector<std::unique_ptr<QEvent>> m_buffer;
    CtQtEventSource<EventSet>* m_sink = nullptr;  ///< consuming source (owning)
    /// (Rev 2, P1-2) Set by eventFilter() on QEvent::Quit (never cloned);
    /// cleared by poll() step 5 after the quit handler runs (§3.4a #3).
    bool m_quitCaptured = false;
};

} // namespace detail

/// Template bridge source: pumps Qt events and injects them into a
/// CtQueue<EventSet> (scheme A — EventSet-carrying source, §7.12 P2-A).
template <typename EventSet>
class CtQtEventSource final : public CtEventSource {
public:
    // (M4 contract, 06 §4) The source's translateAndPush std::visits the
    // three-member CtQtTypedPayload and pushes each branch strong-typed —
    // the EventSet MUST contain all three (M3 required only
    // CtQtEventPayload). Explicit static_asserts make the compile error
    // direct (the std::visit instantiations would report it indirectly).
    static_assert(EventSet::template contains<CtQtEventPayload>,
                  "CtQtEventSource<EventSet> requires CtQtEventPayload in the "
                  "event set");
    static_assert(EventSet::template contains<CtMousePayload>,
                  "CtQtEventSource<EventSet> requires CtMousePayload in the "
                  "event set (M4 typed translation, 06 §4)");
    static_assert(EventSet::template contains<CtKeyPayload>,
                  "CtQtEventSource<EventSet> requires CtKeyPayload in the "
                  "event set (M4 typed translation, 06 §4)");

    /// Create with the loop whose queue this source injects into.
    /// Does NOT install the event filter yet (done by activate(), §3.4 契约 #6).
    /// Requires QCoreApplication::instance() != nullptr (asserts otherwise).
    explicit CtQtEventSource(CtEventLoop& loop);

    /// Deactivates + removes the event filter (RAII).
    ~CtQtEventSource() override;

    /// poll: assert queue.eventSetId() == CtTypeKey<EventSet>::value(), then
    /// pump QCoreApplication::processEvents() once and translate captured
    /// events into strong-typed pushes (main injection path, §2.2).
    /// (Rev 2, P1-3) Sets/clears m_inPoll around the whole body.
    void poll(CtQueueBase& queue) override;

    /// Install the event filter and start capturing (idempotent: no-op if
    /// already active; §3.4 契约 #6). Returns false if qApp is null.
    bool activate() override;

    /// Stop capturing + remove the event filter + clear the capture buffer
    /// (idempotent; noexcept). Subsequent poll() calls do NOT pump Qt events.
    void deactivate() noexcept override;

    std::string_view sourceName() const noexcept override;

    /// Qt event type id -> translator (defaults installed on construction).
    CtQtTranslatorTable& translatorTable() noexcept { return m_translators; }

    /// Called on QEvent::Quit capture (default: loop->quit()).
    void setQuitHandler(std::function<void()> quitHandler);

    /// The capture object (tests access bufferedCount/quitCaptured).
    detail::CtQtEventCapture<EventSet>* capture() const noexcept {
        return m_capture.get();
    }

    /// (Rev 3, gap close) Kind of the last source-side error
    /// (kNone = no error since construction/reset). Set by the poll()
    /// re-entry guard in a release build (§4.2 契约 #2).
    CtEventErrorKind lastErrorKind() const noexcept { return m_lastErrorKind; }

    /// (Rev 3, gap close) Number of source-side errors recorded since
    /// construction (spec §7.4 #16's "release 断言错误计数"). Non-atomic:
    /// affinity-thread-only (poll is single-threaded, §4.2 契约 #7).
    std::uint64_t errorCount() const noexcept { return m_errorCount; }

private:
    /// Translate one buffered event and push it into `queue`.
    /// Records kDropped (queue trace) when no translator is registered.
    void translateAndPush(CtQueue<EventSet>& queue, std::unique_ptr<QEvent> ev);

    /// The capture filter drains into this source (calls translateAndPush);
    /// friend: the source's translateAndPush is private (§3.4).
    friend class detail::CtQtEventCapture<EventSet>;

    /// Handle a captured Quit: run the quit handler and clear the flag.
    void handleQuit();

    CtEventLoop& m_loop;             ///< owning loop (source's queue host)
    CtQtTranslatorTable m_translators;
    /// (Rev 2, P1-1) Concrete capture of the SAME EventSet (templated).
    std::unique_ptr<detail::CtQtEventCapture<EventSet>> m_capture;
    bool m_active = false;
    /// (Rev 2, P1-3) Re-entry guard: true while poll() is executing on the
    /// stack. Debug: assert(false) on re-entry. Release: record an error and
    /// skip the nested processEvents() call (§4.2 契约 #2 / §7.4 #16).
    bool m_inPoll = false;
    /// (Rev 3, gap close) Source-side error observability: poll() re-entry
    /// in a release build records kOther here (spec §4.2 契约 #2 — the
    /// "record an error" leg the original implementation skipped; the
    /// bridge cannot construct CtDispatchContext, so the error state lives
    /// on the source itself). Non-atomic: poll() is affinity-thread-only.
    CtEventErrorKind m_lastErrorKind = CtEventErrorKind::kNone;
    std::uint64_t m_errorCount = 0;      ///< source errors since construction
    std::function<void()> m_quitHandler;  ///< invoked on captured Quit
};

// ---------------------------------------------------------------------------
// Template member definitions (language exception to the .h/.cpp split).
// ---------------------------------------------------------------------------

template <typename EventSet>
CtQtEventSource<EventSet>::CtQtEventSource(CtEventLoop& loop)
    : m_loop(loop) {
    // §3.4 contract #1: construction requires a live QCoreApplication.
    assert(QCoreApplication::instance() != nullptr &&
           "CtQtEventSource: QCoreApplication::instance() must be non-null");
    if (QCoreApplication::instance() == nullptr)
        return;  // release: degrade — no capture, poll pumps nothing

    m_capture = std::make_unique<detail::CtQtEventCapture<EventSet>>();
    m_capture->setSink(this);
    CtQtTranslatorTable::installDefaults(m_translators);

    // §3.4 contract #8: default quit handler terminates the loop.
    m_quitHandler = [this]() { m_loop.quit(); };
}

template <typename EventSet>
CtQtEventSource<EventSet>::~CtQtEventSource() {
    deactivate();  // removes the event filter while qApp may still be alive
}

template <typename EventSet>
void CtQtEventSource<EventSet>::poll(CtQueueBase& queue) {
    // (Rev 2, P1-3) Re-entry guard — §4.2 contract #2 / §7.4 #16.
    // Debug: assert(false) terminates. Release: skip the nested poll body
    // (never pumps, never enqueues, never drains).
    if (m_inPoll) {
        assert(false && "CtQtEventSource::poll re-entered (nested "
                        "processEvents()/exec() inside a handler)");
        // (Rev 3, gap close) Release degrade: record the kOther error per
        // §4.2 契约 #2 (previously a silent skip — the spec's "record an
        // error" leg was missing because CtDispatchContext is private and
        // the source holds no queue-side error channel; the error state now
        // lives on the source, observable via lastErrorKind()/errorCount()).
        // Then skip the body: never pump, never enqueue, never drain.
        m_lastErrorKind = CtEventErrorKind::kOther;
        ++m_errorCount;
        return;
    }
    m_inPoll = true;

    // Step 2: EventSet identity assert before the downcast (§7.12 contract
    // #6 — a cross-set static_cast would be unchecked UB).
    if (queue.eventSetId() != CtTypeKey<EventSet>::value()) {
        assert(false && "CtQtEventSource polled against a queue of a "
                        "different EventSet");
        m_inPoll = false;
        return;
    }

    auto& concrete = static_cast<CtQueue<EventSet>&>(queue);

    // Step 3: pump Qt events (only while active, §3.4 contract #6).
    // Step 4: drain the capture buffer (translate + strong-typed push).
    if (m_active) {
        QCoreApplication::processEvents();
        m_capture->drain(concrete);
    }

    // Step 5: captured Quit -> quit handler (default: loop.quit()).
    handleQuit();

    // Step 6: exit — clear the guard.
    m_inPoll = false;
}

template <typename EventSet>
bool CtQtEventSource<EventSet>::activate() {
    // §3.4 contract #6: idempotent.
    if (m_active)
        return true;

    QCoreApplication* app = QCoreApplication::instance();
    if (app == nullptr)
        return false;  // install failure: no qApp, do NOT set m_active

    app->installEventFilter(m_capture.get());
    m_active = true;
    return true;
}

template <typename EventSet>
void CtQtEventSource<EventSet>::deactivate() noexcept {
    // §3.4 contract #6: idempotent.
    if (!m_active)
        return;

    // §4.2 contract #5: remove the filter ONLY while qApp is still alive
    // (accessing a null qApp in eventFilter would be UB).
    if (QCoreApplication::instance() != nullptr && m_capture)
        QCoreApplication::instance()->removeEventFilter(m_capture.get());

    if (m_capture) {
        m_capture->clearQuitCaptured();
        m_capture->clearBuffer();  // clear the capture buffer (§3.4 #6)
    }
    m_active = false;
}

template <typename EventSet>
std::string_view CtQtEventSource<EventSet>::sourceName() const noexcept {
    return "CtQtEventSource";
}

template <typename EventSet>
void CtQtEventSource<EventSet>::setQuitHandler(std::function<void()> handler) {
    m_quitHandler = std::move(handler);
}

template <typename EventSet>
void CtQtEventSource<EventSet>::translateAndPush(
    CtQueue<EventSet>& queue, std::unique_ptr<QEvent> ev) {
    // Null marker = a captured event whose clone() failed (non-cloneable
    // custom type, §3.4a #6): the event is a real observable loss, so it is
    // dropped and recorded in the QUEUE trace via recordAsyncDrop (the
    // bridge cannot construct CtDispatchContext — Rev 3 ruling).
    if (!ev) {
        queue.recordAsyncDrop(CtTypeKey<CtQtEventPayload>::value());
        return;
    }

    const CtQtTypeKey qtType = static_cast<CtQtTypeKey>(ev->type());

    // Registry lookup first. Unregistered but BRIDGEABLE (non-ignored)
    // types fall back to the identity translator — this covers ALL built-in
    // mouse/key/focus/etc. events below User (§3.4a #4 default capture set)
    // plus any User+ custom event (§3.3 contract #4). Only ignored/internal
    // types (which the capture filter already skips, never reaching here —
    // defensive) are dropped.
    const CtQtTranslator* translator = m_translators.find(qtType);
    if (translator == nullptr && !isIgnoredQtEvent(qtType))
        translator = CtQtTranslatorTable::identityTranslator();

    if (translator == nullptr) {
        // §3.3 contract #5: unregistered type -> kDropped (queue trace),
        // event does NOT enter the queue. Recorded via the queue's public
        // async-drop hook (Rev 13, P1) — the bridge cannot construct
        // CtDispatchContext itself (private ctor, core friends only).
        queue.recordAsyncDrop(CtTypeKey<CtQtEventPayload>::value());
        return;
    }

    const bool spontaneous = ev->spontaneous();
    CtQtTypedPayload payload = (*translator)(std::move(ev), spontaneous);

    // §3.3 contract #3: spontaneous() -> kSystem, else kApplication.
    const CtEventOrigin origin =
        spontaneous ? CtEventOrigin::kSystem : CtEventOrigin::kApplication;

    // (M4) std::visit: construct the STRONG-TYPED CtEvent<P> per branch and
    // push it (06 §3.3). P ∈ {CtQtEventPayload, CtMousePayload, CtKeyPayload}
    // is guaranteed by the class static_asserts above. Broadcast delivery
    // (empty target), §8.2 semantics — bridge events go to every handler
    // registered for the payload. Zero heap allocation: the variant moves
    // into CtEvent<P> by value (M3 unchanged).
    std::visit(
        [&](auto&& p) {
            using P = std::decay_t<decltype(p)>;
            queue.push(std::weak_ptr<CtReceiver>{},
                       CtEvent<P>(origin, std::move(p)));
        },
        payload);
}

template <typename EventSet>
void CtQtEventSource<EventSet>::handleQuit() {
    if (m_capture && m_capture->quitCaptured()) {
        if (m_quitHandler)
            m_quitHandler();
        m_capture->clearQuitCaptured();
    }
}

// --- detail::CtQtEventCapture<EventSet> template members --------------------

template <typename EventSet>
void detail::CtQtEventCapture<EventSet>::drain(CtQueue<EventSet>& queue) {
    if (m_sink == nullptr)
        return;  // no sink: nothing to translate into

    std::vector<std::unique_ptr<QEvent>> batch;
    batch.swap(m_buffer);  // move the whole buffer out (drain point, §3.4a #8)

    for (auto& ev : batch) {
        // Pass the (possibly null) marker through: a null entry is a
        // captured-but-non-cloneable event — translateAndPush records
        // kDropped for it (§3.4a #6). Filtering nulls here would silently
        // lose the drop observability.
        m_sink->translateAndPush(queue, std::move(ev));
    }
    // Clear point: batch destructs here — the buffer is empty again
    // (§3.4a #8: "排空点 = poll 内 drain 调用处；清空点 = drain 返回前").
}

template <typename EventSet>
bool detail::CtQtEventCapture<EventSet>::eventFilter(
    [[maybe_unused]] QObject* watched, QEvent* event) {
    // §3.4a #1: no sink (source not constructed / destroying / direct test
    // use) -> do NOT capture. NEVER throws (Qt filters must not throw).
    if (m_sink == nullptr || event == nullptr)
        return false;

    const CtQtTypeKey qtType = static_cast<CtQtTypeKey>(event->type());

    // §3.4a #5 merge order (P3 #2): registry lookup FIRST — a registered
    // translator re-enables an otherwise-ignored type.
    if (m_sink->translatorTable().find(qtType) != nullptr)
        return captureClone(event);

    // §3.4a #3: Quit is special — never cloned, tracked via the quit flag.
    if (event->type() == QEvent::Quit) {
        m_quitCaptured = true;
        return false;  // never intercept
    }

    // Unregistered: ignored list -> skip (never cloned, never buffered).
    if (isIgnoredQtEvent(qtType))
        return false;

    // Everything else (Timer, User+, and built-in types below User that are
    // not ignored): default capture as raw payload (§3.3 #4 / §3.4a #4).
    return captureClone(event);
}

namespace detail {

template <typename EventSet>
bool CtQtEventCapture<EventSet>::captureClone(QEvent* event) {
    // §3.4a #6: clone() may return nullptr (non-cloneable custom type) ->
    // buffer a NULL marker instead of the clone; the drain path then
    // records kDropped for it (see CtQtEventSource::translateAndPush).
    // The filter itself has no queue reference, so the drop record must
    // happen at drain time — the null marker is the "captured-but-dropped"
    // token that survives from the filter to the drain.
    std::unique_ptr<QEvent> clone(event->clone());
    m_buffer.push_back(std::move(clone));
    return false;  // ALWAYS let Qt continue delivery (§3.4 contract #3)
}

} // namespace detail

} // namespace crisp::event
