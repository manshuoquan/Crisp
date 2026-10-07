#pragma once

// qt_translator.h — runtime mapping table: QEvent::Type -> CRISP payload
// (M3, §3.3).
//
// Part of the CRISP project. Type-safety principle (AGENTS.md #1): the int
// enumeration QEvent::Type is confined to the bridge layer — the ONLY
// sanctioned place that sees it. CRISP handlers always face the strong-typed
// CtEvent<CtQtEventPayload>; the mapping table lives inside
// CtQtEventSource (a controlled single point), never in dispatcher/queue/
// handler code.

#include <QEvent>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "qt_event_payload.h"

namespace crisp::event {

/// Qt event type id (int) used as the runtime map key — Qt's own identity,
/// confined to the bridge layer (the only sanctioned place that sees it).
using CtQtTypeKey = int;

/// Callback: build a CRISP payload from an owned Qt event.
/// The return type is FIXED to CtQtEventPayload (§3.3 contract #1 — M3
/// simplification: translation products are uniformly CtQtEventPayload;
/// multi-type passthrough is deferred to M4). `spontaneous` marks Qt
/// spontaneous events (QEvent::spontaneous()) as CtEventOrigin::kSystem
/// (mapped by the source's poll, §3.3 contract #3).
using CtQtTranslator =
    std::function<CtQtEventPayload(std::unique_ptr<QEvent>, bool spontaneous)>;

/// The bridge's type-safe mapping table: Qt type id -> translator.
/// Implemented in qt_translator.cpp; hot path = one unordered_map lookup
/// per captured event (bridge path is not the CRISP dispatch hot path).
class CtQtTranslatorTable {
public:
    /// Register a translator for a Qt event type. Returns false if the type
    /// is already registered (single translator per Qt type — no ambiguity).
    bool registerTranslator(CtQtTypeKey qtType, CtQtTranslator translator);

    /// Look up a translator; nullptr if unregistered (event not bridged).
    const CtQtTranslator* find(CtQtTypeKey qtType) const noexcept;

    /// The identity translator: wraps an owned Qt event as-is into a
    /// CtQtEventPayload — the M3 default for EVERY bridgeable Qt event type
    /// (§3.3 contract #4). Registered types use their own translator; a
    /// bridgeable-but-unregistered type falls back to THIS in
    /// CtQtEventSource::translateAndPush (see §3.4a #4 — built-in mouse/key/
    /// focus/etc. types below User are captured AND identity-translated).
    static const CtQtTranslator* identityTranslator() noexcept;

    /// Built-in defaults (identity translations), populated by the default
    /// constructor of CtQtEventSource (§3.4). Timer and User+ get identity
    /// translators; kIgnoredQtEvents types get NONE (capture skips them).
    static void installDefaults(CtQtTranslatorTable& table);

private:
    std::unordered_map<CtQtTypeKey, CtQtTranslator> m_table;
};

/// List of Qt event types the bridge never captures (Qt-internal/synthetic
/// and application-level/system-notification events; §3.4a #5 final
/// definition). Timer/User+ and the remaining built-in types are captured.
/// Defined in qt_translator.cpp.
extern const std::vector<CtQtTypeKey>& kIgnoredQtEvents();

/// True if `qtType` is in the ignored set. SINGLE decision point shared by
/// the capture filter and the drain path (§3.4a #5 merge order: registry
/// lookup wins — an app re-enables an ignored type via registerTranslator).
bool isIgnoredQtEvent(CtQtTypeKey qtType) noexcept;

} // namespace crisp::event
