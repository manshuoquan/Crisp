#pragma once

// ct_click_payload.h — CRISP-native strong-typed click payload (core layer,
// UI4). The semantic event emitted when a push button completes a
// press+release inside its bounds (docs/ui/03-rewrite-proposal.md §4.4.2).
//
// Part of the CRISP project. ZERO Qt dependency, lives in the core library
// (src/event/). Value semantics, hot-path zero allocation (stored by value
// inside CtEvent<CtClickPayload>).

#include <chrono>
#include <cstdint>
#include <optional>

#include "ct_event.h"    // CtEventTraits primary template (CRISP_EVENT_TYPE)
#include "ct_input.h"    // CtMouseButton / CtMouseButtons / CtModifiers
#include "ct_type_key.h" // CRISP_EVENT_TYPE macro

namespace crisp::input {

/// Strong-typed click payload (UI4): emitted when a push button completes
/// a press+release inside its bounds. `x`/`y` are widget-local coordinates
/// of the PRESS; `button` is the button that engaged the press; `buttons`
/// is the aggregate pressed set at release; `modifiers` the keyboard
/// modifier state at release. Value semantics, zero heap allocation.
struct CtClickPayload {
    int x = 0;                            ///< widget-local x of the press
    int y = 0;                            ///< widget-local y of the press
    CtMouseButton button = CtMouseButton::kPrimary; ///< the clicked button
    CtMouseButtons buttons;               ///< all pressed buttons at release
    CtModifiers modifiers;                ///< keyboard modifiers at release
    std::chrono::steady_clock::time_point timestamp{}; ///< release time (D6)
};

} // namespace crisp::input

/// EventTraits specialization — required by CtEvent<CtClickPayload>.
/// (File scope, OUTSIDE namespace crisp::input: the CRISP_EVENT_TYPE macro
/// opens `namespace crisp::event { ... }` itself, see ct_type_key.h.)
CRISP_EVENT_TYPE(crisp::input::CtClickPayload, "UiClick");
