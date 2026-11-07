// CRISP UI subsystem demo (UI1-UI5, off-screen).
//
// Shows the widget layer without any platform window:
//   UI1: CtWidgetBase object tree (shared_ptr ownership, weak tree refs)
//   UI4: CtPushButton / CtLabel / CtLineEdit + CtBoxLayout + CtFocusChain
//   UI2: CtPainter + CtPaintBackendPixelBuffer (off-screen RGBA8888 render)
//   UI4: semantic events (onClick / onTextChanged / bindFocusHandler)
//        driven by synthetic crisp::input payloads via dispatchInput
//
// New-API invariants:
//   - widgets are created ONLY via the factories (create / createWidget);
//   - the painter carries a ~0.5MB command buffer: heap-allocate it;
//   - CtBoxLayout::relayout needs a non-null CtPaintBackend for size hints;
//   - semantic handlers bind against the per-thread registry dispatcher.
//
// Build & run:
//   cmake -S . -B build -G "MinGW Makefiles"
//   cmake --build build --parallel 2
//   ./build/examples/ui_demo/ui_demo.exe

#include <cstddef>
#include <cstdio>
#include <memory>
#include <utility>

#include "event/ct_click_payload.h"
#include "event/ct_event.h"
#include "event/ct_focus_payload.h"
#include "event/ct_input.h"
#include "event/ct_key_payload.h"
#include "event/ct_mouse_payload.h"
#include "event/ct_text_changed_payload.h"
#include "ui/ct_box_layout.h"
#include "ui/ct_focus_chain.h"
#include "ui/ct_label.h"
#include "ui/ct_line_edit.h"
#include "ui/ct_paint_backend_pixel.h"
#include "ui/ct_painter.h"
#include "ui/ct_push_button.h"
#include "ui/ct_widget_base.h"

using crisp::event::CtEvent;
using crisp::event::CtEventOrigin;
using crisp::input::CtClickPayload;
using crisp::input::CtFocusDirection;
using crisp::input::CtFocusPayload;
using crisp::input::CtKeyAction;
using crisp::input::CtKeyButton;
using crisp::input::CtKeyPayload;
using crisp::input::CtMouseAction;
using crisp::input::CtMouseButton;
using crisp::input::CtMouseButtons;
using crisp::input::CtMousePayload;
using crisp::input::CtTextChangedPayload;
using crisp::ui::CtBoxLayout;
using crisp::ui::CtColor;
using crisp::ui::CtFocusChain;
using crisp::ui::CtLabel;
using crisp::ui::CtLineEdit;
using crisp::ui::CtPaintBackendPixelBuffer;
using crisp::ui::CtPainter;
using crisp::ui::CtPushButton;
using crisp::ui::CtRect;
using crisp::ui::CtSize;
using crisp::ui::CtWidgetBase;

// ---------------------------------------------------------------------------
// Helpers to synthesize input events (widget-local coordinates).
//
// The payload structs are value-initialized (CtMousePayload p; etc.): any
// field we do not touch is zero/empty, which is the neutral value the
// widget hooks expect (e.g. CtModifiers{}.bits == kNone). We still set the
// fields that matter for the demo explicitly.
// ---------------------------------------------------------------------------
namespace {

/// Send one synthetic mouse event to `w` (widget-local coords).
void sendMouse(const std::shared_ptr<CtWidgetBase>& w, CtMousePayload p) {
    CtEvent<CtMousePayload> ev(CtEventOrigin::kSystem, std::move(p));
    w->dispatchInput(ev);
}

/// Send one synthetic key event to `w`.
void sendKey(const std::shared_ptr<CtWidgetBase>& w, CtKeyPayload p) {
    CtEvent<CtKeyPayload> ev(CtEventOrigin::kSystem, std::move(p));
    w->dispatchInput(ev);
}

/// A press of the primary button at widget-local (x, y).
CtMousePayload mousePress(int x, int y) {
    CtMousePayload p;
    p.action = CtMouseAction::kPress;
    p.x = x;
    p.y = y;
    p.button = CtMouseButton::kPrimary;
    p.buttons.bits = CtMouseButtons::kPrimary;
    p.modifiers.bits = CtModifiers::kNone; // explicit neutral modifier state
    return p;
}

/// A release of the primary button at widget-local (x, y).
CtMousePayload mouseRelease(int x, int y) {
    CtMousePayload p;
    p.action = CtMouseAction::kRelease;
    p.x = x;
    p.y = y;
    p.button = CtMouseButton::kPrimary;
    p.buttons.bits = CtMouseButtons::kNone;
    p.modifiers.bits = CtModifiers::kNone; // explicit neutral modifier state
    return p;
}

/// A printable key press (types `ch` into the focused line edit).
/// The CRISP line edit inserts `payload.text` for printable keys; the
/// `key` enum is only consulted for the editing keys (Backspace/arrows).
/// NOTE: BMP-only (single char16_t); a non-BMP code point would need a
/// surrogate pair.
CtKeyPayload keyType(char16_t ch) {
    CtKeyPayload p;
    p.action = CtKeyAction::kPress;
    p.key = CtKeyButton::kUnknown; // printable text goes via `text`
    p.text.assign(1, ch);
    p.modifiers.bits = CtModifiers::kNone; // explicit neutral modifier state
    return p;
}

/// A non-printing key press (Tab / Enter / Backspace / ...).
CtKeyPayload keyPress(CtKeyButton key) {
    CtKeyPayload p;
    p.action = CtKeyAction::kPress;
    p.key = key;
    p.modifiers.bits = CtModifiers::kNone; // explicit neutral modifier state
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    // ---- UI1: build the widget tree ------------------------------------
    // Root owns nothing strongly; children keep weak back-refs, so the
    // tree stays alive through the shared_ptrs below.
    auto root = CtWidgetBase::create();
    root->setGeometry(CtRect{0, 0, 360, 160});

    auto title = CtLabel::create(root, u"CRISP UI demo");
    auto goButton = CtPushButton::create(root, u"Go");
    auto nameEdit = CtLineEdit::create(root, u"world");

    // ---- UI4: explicit two-pass box layout (measure + place) -----------
    CtBoxLayout vbox(CtBoxLayout::Direction::kVertical);
    vbox.addWidget(title);
    vbox.addWidget(goButton);
    vbox.addWidget(nameEdit);

    // The layout needs a backend only for font metrics (size hints); the
    // PixelBuffer backend provides fixed metrics.
    CtPaintBackendPixelBuffer metricsBackend(CtSize{360, 160});
    vbox.relayout(root->geometry(), &metricsBackend);

    // ---- UI4: explicit focus chain (Tab / Shift+Tab stepping) ----------
    auto chain = std::make_shared<CtFocusChain>();
    goButton->setFocusable(true);
    nameEdit->setFocusable(true);
    chain->add(goButton);
    chain->add(nameEdit);
    chain->setFocus(goButton);

    // ---- UI4: semantic handlers (typed, registered per thread) ---------
    int clickCount = 0;
    auto clickToken = goButton->onClick([&](CtEvent<CtClickPayload>& ev) {
        ++clickCount;
        std::printf("  [ui] Go clicked at (%d, %d) -> count=%d\n",
                    ev.payload().x, ev.payload().y, clickCount);
        ev.accept();
    });

    auto textToken = nameEdit->onTextChanged(
        [&](CtEvent<CtTextChangedPayload>& ev) {
            std::printf("  [ui] text changed -> length=%zu (caret %zu)\n",
                        ev.payload().text.size(), ev.payload().cursorPos);
            ev.accept();
        });

    auto focusTokenA = goButton->bindFocusHandler(
        [&](CtEvent<CtFocusPayload>& ev) {
            // NOTE: setFocus always reports kNext (the chain's step
            // direction; see ct_focus_chain.cpp) - the payload records the
            // direction used by the focus move.
            std::printf("  [ui] focus gained by button (objectId=%llu, dir=%d)\n",
                        static_cast<unsigned long long>(ev.payload().targetObjectId),
                        static_cast<int>(ev.payload().direction));
            ev.accept();
        });
    auto focusTokenB = nameEdit->bindFocusHandler(
        [&](CtEvent<CtFocusPayload>& ev) {
            std::printf("  [ui] focus gained by line edit (objectId=%llu, dir=%d)\n",
                        static_cast<unsigned long long>(ev.payload().targetObjectId),
                        static_cast<int>(ev.payload().direction));
            ev.accept();
        });

    // ---- Drive the scene with synthetic input ---------------------------
    std::printf("== click the Go button ==\n");
    // Press + release INSIDE the button completes a click.
    sendMouse(goButton, mousePress(20, 10));
    sendMouse(goButton, mouseRelease(22, 12));
    // Press inside, release OUTSIDE: the click is cancelled.
    sendMouse(goButton, mousePress(20, 10));
    sendMouse(goButton, mouseRelease(300, 300));

    std::printf("\n== Tab to the line edit and type ==\n");
    chain->next(CtFocusDirection::kNext); // button -> edit (kNext)
    // The widget layer does NOT consume raw keys (dispatchInput hands the
    // payload to onKeyInput without accepting): the line edit's onKeyInput
    // edits its buffer directly from the payload (text/editing keys).
    sendKey(nameEdit, keyType('h'));
    sendKey(nameEdit, keyType('i'));
    sendKey(nameEdit, keyPress(CtKeyButton::kBackspace));
    sendKey(nameEdit, keyType('!'));
    sendKey(nameEdit, keyPress(CtKeyButton::kEnter)); // ignored by edit

    std::printf("\n== Shift+Tab back to the button ==\n");
    chain->next(CtFocusDirection::kPrevious);

    // ---- UI2: paint the scene off-screen (heap-allocated painter) ------
    std::printf("\n== paint into the off-screen PixelBuffer backend ==\n");
    auto painter = std::make_unique<CtPainter>();
    CtPaintBackendPixelBuffer buffer(CtSize{360, 160});
    std::size_t painted = 0;
    if (painter->begin(buffer)) {
        painter->fillRect(root->geometry(), CtColor{0x20, 0x20, 0x28, 0xFF});
        title->paint(*painter, title->geometry());
        goButton->paint(*painter, goButton->geometry());
        nameEdit->paint(*painter, nameEdit->geometry());
        // Count the recorded commands BEFORE end() - end() flushes and
        // clears the buffer (the count resets to zero).
        painted = painter->commandCount();
        painter->end();
        std::printf("  painted %zu commands; sample pixel(5,5)=0x%02X%02X%02X%02X\n",
                    painted,
                    static_cast<unsigned int>(buffer.pixel(5, 5).r),
                    static_cast<unsigned int>(buffer.pixel(5, 5).g),
                    static_cast<unsigned int>(buffer.pixel(5, 5).b),
                    static_cast<unsigned int>(buffer.pixel(5, 5).a));
    }

    // ---- Wrap up ---------------------------------------------------------
    // The handler tokens are RAII: keeping them alive keeps the handlers
    // registered until main() ends (destroying one would unregister).
    (void)clickToken; (void)textToken; (void)focusTokenA; (void)focusTokenB;
    std::printf("\nui demo done (clicks=%d, text-length=%zu)\n",
                clickCount, nameEdit->text().size());
    return 0;
}
