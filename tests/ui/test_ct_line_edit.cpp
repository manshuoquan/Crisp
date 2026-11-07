// Unit tests: crisp::ui line edit (CtLineEdit, UI4 §4.4.2).
//
// Covers the editing logic (pure, no platform): text input via onKeyInput,
// backspace/delete, caret movement (arrows/home/end), mouse caret
// placement, setText clamping, and the onTextChanged semantic event
// (CtTextChangedPayload).
//
// Hand-written test doubles, no mock framework (project rule).

#include <memory>
#include <string>

#include "ui/ct_line_edit.h"
#include "event/ct_dispatcher.h"
#include "event/ct_event.h"
#include "event/ct_input.h"

#include "test_harness.h"

using crisp::event::CtEvent;
using crisp::event::CtEventOrigin;
using crisp::input::CtKeyAction;
using crisp::input::CtKeyButton;
using crisp::input::CtKeyPayload;
using crisp::input::CtMouseAction;
using crisp::input::CtMousePayload;
using crisp::input::CtTextChangedPayload;
using crisp::ui::CtLineEdit;
using crisp::ui::CtRect;
using crisp::ui::CtWidgetBase;

namespace {

/// Dispatch a synthetic key press to `w`.
void sendKey(const std::shared_ptr<CtWidgetBase>& w, CtKeyButton key,
             std::u16string text = {}) {
    CtKeyPayload p;
    p.action = CtKeyAction::kPress;
    p.key = key;
    p.text = std::move(text);
    CtEvent<CtKeyPayload> ev(CtEventOrigin::kSystem, p);
    w->dispatchInput(ev);
}

TEST_CASE(lineedit_initial_state)
{
    auto root = CtWidgetBase::create();
    auto edit = CtLineEdit::create(root, u"hello");
    EXPECT_EQ(edit->text(), u"hello");
    EXPECT_EQ(edit->cursorPos(), 0u); // caret at the start by default
}

TEST_CASE(lineedit_types_text_at_caret)
{
    auto root = CtWidgetBase::create();
    auto edit = CtLineEdit::create(root);

    int changes = 0;
    std::u16string lastText;
    std::size_t lastCursor = 0;
    auto token = edit->onTextChanged([&](CtEvent<CtTextChangedPayload>& ev) {
        ++changes;
        lastText = ev.payload().text;
        lastCursor = ev.payload().cursorPos;
        ev.accept();
    });

    sendKey(edit, CtKeyButton::kA, u"a");
    EXPECT_EQ(edit->text(), u"a");
    EXPECT_EQ(edit->cursorPos(), 1u);
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(lastText, u"a");
    EXPECT_EQ(lastCursor, 1u);

    sendKey(edit, CtKeyButton::kB, u"b");
    EXPECT_EQ(edit->text(), u"ab");
    EXPECT_EQ(edit->cursorPos(), 2u);
    EXPECT_EQ(changes, 2);
    EXPECT_EQ(lastText, u"ab");
}

TEST_CASE(lineedit_backspace_and_delete)
{
    auto root = CtWidgetBase::create();
    auto edit = CtLineEdit::create(root, u"abc");
    edit->setCursorPos(2u); // caret between b and c

    sendKey(edit, CtKeyButton::kBackspace);
    EXPECT_EQ(edit->text(), u"ac");
    EXPECT_EQ(edit->cursorPos(), 1u);

    sendKey(edit, CtKeyButton::kDelete);
    EXPECT_EQ(edit->text(), u"a");
    EXPECT_EQ(edit->cursorPos(), 1u);

    // Backspace at position 0: no-op.
    edit->setCursorPos(0u);
    sendKey(edit, CtKeyButton::kBackspace);
    EXPECT_EQ(edit->text(), u"a");
}

TEST_CASE(lineedit_caret_movement)
{
    auto root = CtWidgetBase::create();
    auto edit = CtLineEdit::create(root, u"abcd");

    sendKey(edit, CtKeyButton::kEnd);
    EXPECT_EQ(edit->cursorPos(), 4u);
    sendKey(edit, CtKeyButton::kArrowLeft);
    EXPECT_EQ(edit->cursorPos(), 3u);
    sendKey(edit, CtKeyButton::kArrowRight);
    EXPECT_EQ(edit->cursorPos(), 4u);
    sendKey(edit, CtKeyButton::kHome);
    EXPECT_EQ(edit->cursorPos(), 0u);
    sendKey(edit, CtKeyButton::kArrowLeft); // at 0: no-op
    EXPECT_EQ(edit->cursorPos(), 0u);
}

TEST_CASE(lineedit_mouse_press_places_caret)
{
    auto root = CtWidgetBase::create();
    auto edit = CtLineEdit::create(root, u"abcdefgh"); // 8 chars x 8px
    edit->setGeometry(CtRect{0, 0, 100, 20});

    // UI5 (P1-1): the click x is widget-local and INCLUDES the style's
    // left frame (padding+border). textX = rect.x + frame.width/2, with
    // the default style frame.width = 2*8 + 2*1 = 18 → textX = 9. A click
    // at x=9+32=41 lands on character 4 (the 5th char boundary).
    CtMousePayload press;
    press.action = CtMouseAction::kPress;
    press.x = 9 + 4 * 8; // 41: 4 chars in from the TEXT origin
    press.y = 10;
    press.button = crisp::input::CtMouseButton::kPrimary;
    CtEvent<CtMousePayload> ev(CtEventOrigin::kSystem, press);
    edit->dispatchInput(ev);
    EXPECT_EQ(edit->cursorPos(), 4u);

    // A click in the LEFT PADDING (x=5, inside the frame) maps to column 0.
    press.x = 5;
    CtEvent<CtMousePayload> ev2(CtEventOrigin::kSystem, press);
    edit->dispatchInput(ev2);
    EXPECT_EQ(edit->cursorPos(), 0u);

    // A click PAST the last character clamps to the text end (8).
    press.x = 9 + 12 * 8; // far beyond "abcdefgh"
    CtEvent<CtMousePayload> ev3(CtEventOrigin::kSystem, press);
    edit->dispatchInput(ev3);
    EXPECT_EQ(edit->cursorPos(), 8u);
}

TEST_CASE(lineedit_set_text_clamps_caret_and_emits)
{
    auto root = CtWidgetBase::create();
    auto edit = CtLineEdit::create(root, u"old");
    edit->setCursorPos(3u);

    int changes = 0;
    auto token = edit->onTextChanged([&](CtEvent<CtTextChangedPayload>&) {
        ++changes;
    });

    edit->setText(u"new text longer");
    EXPECT_EQ(edit->text(), u"new text longer");
    // setText CLAMPS the caret to the new size (does NOT jump to end).
    EXPECT_EQ(edit->cursorPos(), 3u);
    EXPECT_EQ(changes, 1);
}

TEST_CASE(lineedit_same_text_set_no_change)
{
    auto root = CtWidgetBase::create();
    auto edit = CtLineEdit::create(root, u"same");

    int changes = 0;
    auto token = edit->onTextChanged([&](CtEvent<CtTextChangedPayload>&) {
        ++changes;
    });
    edit->setText(u"same");
    EXPECT_EQ(changes, 0);
}

TEST_CASE(lineedit_non_printing_keys_do_not_edit)
{
    auto root = CtWidgetBase::create();
    auto edit = CtLineEdit::create(root, u"text");

    // Enter/Escape/Tab carry no text: no edit, no event.
    int changes = 0;
    auto token = edit->onTextChanged([&](CtEvent<CtTextChangedPayload>&) {
        ++changes;
    });
    sendKey(edit, CtKeyButton::kEnter);
    sendKey(edit, CtKeyButton::kEscape);
    sendKey(edit, CtKeyButton::kTab);
    EXPECT_EQ(edit->text(), u"text");
    EXPECT_EQ(changes, 0);
}

} // namespace (anonymous)
