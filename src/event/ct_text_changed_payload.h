#pragma once

// ct_text_changed_payload.h — CRISP-native strong-typed text-changed
// payload (core layer, UI4). Emitted by CtLineEdit whenever its text
// changes (docs/ui/03-rewrite-proposal.md §4.4.2 onTextChanged).
//
// Part of the CRISP project. ZERO Qt dependency, lives in the core library
// (src/event/). Value semantics. NOTE: the payload CARRIES a u16string
// (the text field) — this is NOT on the hot zero-allocation path (the
// event is emitted on user edit, one allocation per change, same precedent
// as CtKeyPayload::text in the input path).

#include <cstdint>
#include <string>

#include "ct_event.h"    // CtEventTraits primary template (CRISP_EVENT_TYPE)
#include "ct_type_key.h" // CRISP_EVENT_TYPE macro

namespace crisp::input {

/// Strong-typed text-changed payload (UI4): emitted when a CtLineEdit's
/// text changes. `text` is the new FULL text after the edit;
/// `cursorPos` the caret position after the edit.
struct CtTextChangedPayload {
    std::u16string text;   ///< the new full text (UTF-16)
    std::size_t cursorPos = 0; ///< caret position after the edit (0..size)
};

} // namespace crisp::input

/// EventTraits specialization — required by CtEvent<CtTextChangedPayload>.
/// (File scope, OUTSIDE namespace crisp::input: the CRISP_EVENT_TYPE macro
/// opens `namespace crisp::event { ... }` itself, see ct_type_key.h.)
CRISP_EVENT_TYPE(crisp::input::CtTextChangedPayload, "UiTextChanged");
