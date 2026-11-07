#pragma once

// ct_focus_payload.h — CRISP-native strong-typed focus payload (core layer,
// UI4). Emitted when focus moves between widgets (docs/ui/03-rewrite-
// proposal.md §4.4.4: the explicit CtFocusChain emits CtEvent<CtFocusPayload>).
//
// Part of the CRISP project. ZERO Qt dependency, lives in the core library
// (src/event/). Value semantics, hot-path zero allocation (stored by value
// inside CtEvent<CtFocusPayload>).

#include <cstdint>

#include "ct_event.h"    // CtEventTraits primary template (CRISP_EVENT_TYPE)
#include "ct_type_key.h" // CRISP_EVENT_TYPE macro

namespace crisp::input {

/// Focus change direction (UI4): the step direction used by
/// CtFocusChain::next() for Tab/Backtab stepping.
enum class CtFocusDirection : std::uint8_t {
    kNext = 0,     ///< forward step (Tab)
    kPrevious = 1, ///< backward step (Backtab / Shift+Tab)
};

/// Strong-typed focus payload (UI4): emitted when focus enters a widget.
/// `targetObjectId` identifies the widget that RECEIVED focus (the payload
/// is delivered to that widget's handlers); `direction` records how focus
/// moved (kNext for Tab, kPrevious for Backtab, kNext for a direct
/// setFocus — the neutral default).
struct CtFocusPayload {
    CtFocusDirection direction = CtFocusDirection::kNext; ///< step direction
    std::uint64_t targetObjectId = 0; ///< objectId of the focus receiver
};

} // namespace crisp::input

/// EventTraits specialization — required by CtEvent<CtFocusPayload>.
/// (File scope, OUTSIDE namespace crisp::input: the CRISP_EVENT_TYPE macro
/// opens `namespace crisp::event { ... }` itself, see ct_type_key.h.)
CRISP_EVENT_TYPE(crisp::input::CtFocusPayload, "UiFocus");
