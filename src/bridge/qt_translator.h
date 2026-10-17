#pragma once

// qt_translator.h — runtime mapping table: QEvent::Type -> CRISP payload
// (M3, §3.3; M4 typed translation, 06-m4-typed-translation.md).
//
// Part of the CRISP project. Type-safety principle (AGENTS.md #1): the int
// enumeration QEvent::Type is confined to the bridge layer — the ONLY
// sanctioned place that sees it. CRISP handlers always face strong-typed
// CtEvent<T>; the mapping table lives inside CtQtEventSource (a controlled
// single point), never in dispatcher/queue/handler code.
//
// (M4) The translation product is CtQtTypedPayload — a CLOSED three-member
// variant (CtQtEventPayload / CtMousePayload / CtKeyPayload). The M3
// identity path (raw QEvent -> CtQtEventPayload) remains a member, so
// unregistered-but-bridgeable types still fall back to identity (§3.3
// contract #4); mouse/key types registered by installDefaults go through
// the typed translators (§5.1/§5.2 of 06).

#include <QEvent>
#include <functional>
#include <memory>
#include <unordered_map>
#include <variant>
#include <vector>

#include "qt_event_payload.h"
#include "qt_mouse_payload.h"
#include "qt_key_payload.h"

namespace crisp::event {

/// Qt event type id (int) used as the runtime map key — Qt's own identity,
/// confined to the bridge layer (the only sanctioned place that sees it).
using CtQtTypeKey = int;

/// (M4) Typed translation product: one of the bridge's typed payloads.
/// The M3 identity path (raw QEvent -> CtQtEventPayload) remains a member.
/// CLOSED variant: application-defined payloads cannot pass through the
/// default translators (06 §8 #3 — out of M4 scope).
using CtQtTypedPayload =
    std::variant<CtQtEventPayload, CtMousePayload, CtKeyPayload>;

/// Callback: build a CRISP payload from an owned Qt event.
/// (M4) Return type changed from the fixed CtQtEventPayload to the typed
/// variant — the source's translateAndPush std::visits it and pushes the
/// STRONG-TYPED CtEvent<T> per branch (06 §3.3). `spontaneous` marks Qt
/// spontaneous events (QEvent::spontaneous()) as CtEventOrigin::kSystem
/// (mapped by the source's poll, §3.3 contract #3).
using CtQtTranslator = std::function<CtQtTypedPayload(
    std::unique_ptr<QEvent>, bool spontaneous)>;

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
