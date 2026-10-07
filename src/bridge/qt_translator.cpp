// qt_translator.cpp — translator table + kIgnoredQtEvents definitions.
//
// Part of the CRISP project, M3 Qt bridge (docs/event-system/05-qt-bridge.md
// §3.3/§3.4a). Non-template bridge-layer logic lives here; the template
// capture class (qt_event_source.h) calls the free helper
// isIgnoredQtEvent() instead of duplicating the boundary decision
// (P3 recommendation #2 — capture layer first checks the registry, then the
// ignored list; both must agree, so the decision lives in ONE place).

#include "qt_translator.h"

#include <vector>

namespace crisp::event {

namespace {

/// Identity translator: wraps the owned event as-is (Timer/User+ default).
CtQtEventPayload identityTranslate(std::unique_ptr<QEvent> event, bool) {
    return CtQtEventPayload(std::move(event));
}

/// The FINAL ignored-type list (§3.4a #5): Qt-internal/synthetic events plus
/// the application-level/system-notification events that are explicitly NOT
/// captured by default (Rev 2 P3-3 boundary decision). Quit is special —
/// never cloned, but tracked via m_quitCaptured (§3.4a #3); it lives here so
/// the capture filter skips the clone path for it.
const std::vector<CtQtTypeKey>& ignoredTypes() {
    static const std::vector<CtQtTypeKey> kIgnored = {
        QEvent::Paint,          // 12
        QEvent::UpdateRequest,  // 77
        QEvent::UpdateLater,    // 78
        QEvent::LayoutRequest,  // 76
        QEvent::Polish,         // 75
        QEvent::PolishRequest,  // 74
        QEvent::MetaCall,       // 43
        QEvent::DeferredDelete, // 52
        QEvent::ThreadChange,   // 22
        QEvent::ChildAdded,     // 68
        QEvent::ChildRemoved,   // 71
        QEvent::ChildPolished,  // 69
        QEvent::Quit,           // 20 (special: quit flag, never cloned)
        // Application-level / system-notification events — explicitly NOT
        // captured by default (P3-3): an app re-enables any of these by
        // registerTranslator (registry lookup wins, §3.4a #5 merge order).
        QEvent::ApplicationStateChange,      // 214
        QEvent::ApplicationFontChange,       // 36
        QEvent::ApplicationPaletteChange,    // 38
        QEvent::LocaleChange,                // 88
        QEvent::LanguageChange,              // 89
        QEvent::LayoutDirectionChange,       // 90
        QEvent::ApplicationLayoutDirectionChange, // 37
        QEvent::ApplicationWindowIconChange,       // 35
        QEvent::ApplicationActivate,               // 121 (deprecated alias)
        QEvent::ApplicationDeactivate,             // 122 (deprecated alias)
    };
    return kIgnored;
}

} // namespace (anonymous)

bool CtQtTranslatorTable::registerTranslator(CtQtTypeKey qtType,
                                             CtQtTranslator translator) {
    if (!translator)
        return false;
    if (m_table.find(qtType) != m_table.end())
        return false;  // single translator per Qt type (no ambiguity)
    m_table.emplace(qtType, std::move(translator));
    return true;
}

const CtQtTranslator* CtQtTranslatorTable::find(CtQtTypeKey qtType) const
    noexcept {
    const auto it = m_table.find(qtType);
    return it == m_table.end() ? nullptr : &it->second;
}

void CtQtTranslatorTable::installDefaults(CtQtTranslatorTable& table) {
    // Timer: identity translation (the timer payload stays a raw QEvent —
    // Qt pumps the timer, §3.3 contract #4).
    const CtQtTranslator* identity = identityTranslator();
    table.registerTranslator(QEvent::Timer, *identity);

    // Any User-level or above custom event: identity translation.
    // (Registered generically here: a single entry at User would not cover
    // User+N, so the source's poll treats qtType >= User as identity via
    // this table only if registered; installDefaults registers the generic
    // User slot AND the poll falls back to identity for any qtType >= User,
    // see qt_event_source.h §3.4 contract #4.)
    table.registerTranslator(QEvent::User, *identity);
}

const CtQtTranslator* CtQtTranslatorTable::identityTranslator() noexcept {
    // The default translation for every bridgeable Qt event type (identity
    // passthrough, §3.3 contract #4 / §3.4a #4). Defined in the anonymous
    // namespace above; returning its address keeps the fallback allocation-
    // free (no std::function copy per lookup).
    static const CtQtTranslator kIdentity = &identityTranslate;
    return &kIdentity;
}

/// Free helper shared with the capture filter (single decision point, P3 #2).
bool isIgnoredQtEvent(CtQtTypeKey qtType) noexcept {
    const auto& ignored = ignoredTypes();
    for (CtQtTypeKey t : ignored)
        if (t == qtType)
            return true;
    return false;
}

const std::vector<CtQtTypeKey>& kIgnoredQtEvents() {
    return ignoredTypes();
}

} // namespace crisp::event
