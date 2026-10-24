// Unit tests for the CRISP-native input payloads (UI pre-M1).
//
// Part of the CRISP project. Covers the core-layer input types that the
// crisp::ui subsystem consumes: CtInput enums (CtMouseButton/CtKeyButton/
// CtModifiers/CtMouseAction/CtKeyAction), the zero-Qt CtMousePayload /
// CtKeyPayload, their CtEventTraits, and end-to-end dispatch through a
// closed CtEventSet. Hand-written test doubles only (NO mock framework).

#include "event/ct_key_payload.h"
#include "event/ct_mouse_payload.h"

#include <cstdint>
#include <memory>
#include <string>

#include "event/ct_dispatcher.h"
#include "test_harness.h"

namespace {
using namespace crisp::event;
using namespace crisp::input;

using InputEventSet = CtEventSet<CtMousePayload, CtKeyPayload>;
using InputDispatcher = CtDispatcher<InputEventSet>;

// --- CtModifiers / CtMouseButtons ------------------------------------------

TEST_CASE(input_modifiers_bits_combine_and_query)
{
    CtModifiers mods;
    EXPECT_EQ(mods.bits, CtModifiers::kNone);
    EXPECT_EQ(mods.any(), false);

    mods.bits = CtModifiers::kShift | CtModifiers::kControl;
    EXPECT_EQ(mods.any(), true);
    EXPECT_EQ(mods.has(CtModifiers::kShift), true);
    EXPECT_EQ(mods.has(CtModifiers::kControl), true);
    EXPECT_EQ(mods.has(CtModifiers::kAlt), false);
    // Combo query: both must be set.
    EXPECT_EQ(mods.has(CtModifiers::kShift | CtModifiers::kControl), true);
    EXPECT_EQ(mods.has(CtModifiers::kShift | CtModifiers::kAlt), false);
}

TEST_CASE(input_mouse_buttons_bits_combine_and_query)
{
    CtMouseButtons btns;
    EXPECT_EQ(btns.bits, CtMouseButtons::kNone);
    EXPECT_EQ(btns.any(), false);

    btns.bits = CtMouseButtons::kPrimary | CtMouseButtons::kX1;
    EXPECT_EQ(btns.any(), true);
    EXPECT_EQ(btns.has(CtMouseButtons::kPrimary), true);
    EXPECT_EQ(btns.has(CtMouseButtons::kSecondary), false);
    // Combo query: both must be held.
    EXPECT_EQ(btns.has(CtMouseButtons::kPrimary | CtMouseButtons::kX1), true);
    EXPECT_EQ(btns.has(CtMouseButtons::kPrimary | CtMouseButtons::kX2), false);
}

// --- CtMousePayload --------------------------------------------------------

TEST_CASE(input_mouse_payload_defaults)
{
    CtMousePayload p;
    EXPECT_EQ(static_cast<int>(p.action), static_cast<int>(CtMouseAction::kMove));
    EXPECT_EQ(p.x, 0);
    EXPECT_EQ(p.y, 0);
    EXPECT_EQ(p.button.has_value(), false);
    EXPECT_EQ(p.buttons.bits, CtMouseButtons::kNone);
    EXPECT_EQ(p.modifiers.bits, CtModifiers::kNone);
}

TEST_CASE(input_mouse_payload_value_semantics)
{
    CtMousePayload p;
    p.action = CtMouseAction::kPress;
    p.x = 12;
    p.y = 34;
    p.button = CtMouseButton::kPrimary;
    p.buttons.bits = CtMouseButtons::kPrimary;
    p.modifiers.bits = CtModifiers::kControl;

    CtMousePayload copy = p;
    EXPECT_EQ(static_cast<int>(copy.action), static_cast<int>(CtMouseAction::kPress));
    EXPECT_EQ(copy.x, 12);
    EXPECT_EQ(copy.y, 34);
    EXPECT_EQ(copy.button.value(), CtMouseButton::kPrimary);
    EXPECT_EQ(copy.buttons.bits, CtMouseButtons::kPrimary);
    EXPECT_EQ(copy.modifiers.bits, CtModifiers::kControl);
}

TEST_CASE(input_mouse_event_dispatches_typed)
{
    auto dispatcher = std::make_shared<InputDispatcher>();
    auto receiver = createReceiver<CtReceiver>();

    // Token lifetime (§7.7): registering inside a scope and letting the
    // token die must UNREGISTER the handler — a later send must be
    // unhandled, not silently delivered.
    CtDispatchResult inside;
    {
        int calls = 0;
        auto token = dispatcher->addHandler<CtMousePayload>(
            receiver, [&](CtEvent<CtMousePayload>& ev) {
                ++calls;
                EXPECT_EQ(ev.payload().x, 7);
                EXPECT_EQ(ev.payload().y, 9);
                EXPECT_EQ(ev.payload().button.value(), CtMouseButton::kSecondary);
                ev.accept();
            });

        CtMousePayload p;
        p.action = CtMouseAction::kPress;
        p.x = 7;
        p.y = 9;
        p.button = CtMouseButton::kSecondary;
        p.buttons.bits = CtMouseButtons::kSecondary;
        p.modifiers.bits = CtModifiers::kAlt;
        CtEvent<CtMousePayload> ev(CtEventOrigin::kSystem, p);
        inside = dispatcher->send(receiver, ev);
        EXPECT_EQ(calls, 1);
    }

    EXPECT_EQ(static_cast<int>(inside), static_cast<int>(CtDispatchResult::kHandled));

    CtEvent<CtMousePayload> after(CtMousePayload{});
    const auto result = dispatcher->send(receiver, after);
    EXPECT_EQ(static_cast<int>(result), static_cast<int>(CtDispatchResult::kUnhandled));
}

// --- CtKeyPayload ----------------------------------------------------------

TEST_CASE(input_key_payload_defaults)
{
    CtKeyPayload p;
    EXPECT_EQ(static_cast<int>(p.action), static_cast<int>(CtKeyAction::kPress));
    EXPECT_EQ(p.key, CtKeyButton::kUnknown);
    EXPECT_EQ(p.text.empty(), true);
    EXPECT_EQ(p.modifiers.bits, CtModifiers::kNone);
    EXPECT_EQ(p.isAutoRepeat, false);
}

TEST_CASE(input_key_payload_utf16_text)
{
    // "A" in UTF-16; the core payload must NOT depend on QString (06 §8 #2).
    CtKeyPayload p;
    p.action = CtKeyAction::kPress;
    p.key = CtKeyButton::kA;
    p.text = u"A";
    p.modifiers.bits = CtModifiers::kShift;
    p.isAutoRepeat = false;

    CtKeyPayload copy = p;
    EXPECT_EQ(copy.key, CtKeyButton::kA);
    EXPECT_EQ(copy.text, u"A");
    EXPECT_EQ(copy.modifiers.has(CtModifiers::kShift), true);
}

TEST_CASE(input_key_event_dispatches_typed)
{
    auto dispatcher = std::make_shared<InputDispatcher>();
    auto receiver = createReceiver<CtReceiver>();

    // Token lifetime (§7.7): same scope-bound pattern as the mouse test.
    CtDispatchResult inside;
    {
        int calls = 0;
        auto token = dispatcher->addHandler<CtKeyPayload>(
            receiver, [&](CtEvent<CtKeyPayload>& ev) {
                ++calls;
                EXPECT_EQ(ev.payload().key, CtKeyButton::kEnter);
                EXPECT_EQ(ev.payload().modifiers.has(CtModifiers::kControl), true);
                EXPECT_EQ(ev.payload().isAutoRepeat, true);
                ev.accept();
            });

        CtKeyPayload p;
        p.action = CtKeyAction::kPress;
        p.key = CtKeyButton::kEnter;
        p.text = u"";
        p.modifiers.bits = CtModifiers::kControl;
        p.isAutoRepeat = true;
        CtEvent<CtKeyPayload> ev(CtEventOrigin::kSystem, p);
        inside = dispatcher->send(receiver, ev);
        EXPECT_EQ(calls, 1);
    }

    EXPECT_EQ(static_cast<int>(inside), static_cast<int>(CtDispatchResult::kHandled));

    CtEvent<CtKeyPayload> after(CtKeyPayload{});
    const auto result = dispatcher->send(receiver, after);
    EXPECT_EQ(static_cast<int>(result), static_cast<int>(CtDispatchResult::kUnhandled));
}

// --- trait name / closed set -----------------------------------------------

TEST_CASE(input_traits_names_are_compile_time)
{
    static_assert(CtEventTraits<CtMousePayload>::kIsValid, "mouse traits");
    static_assert(CtEventTraits<CtMousePayload>::name == "InputMouse");
    static_assert(CtEventTraits<CtKeyPayload>::kIsValid, "key traits");
    static_assert(CtEventTraits<CtKeyPayload>::name == "InputKey");
}

TEST_CASE(input_closed_set_contains_both_payloads)
{
    static_assert(InputEventSet::contains<CtMousePayload>, "mouse in set");
    static_assert(InputEventSet::contains<CtKeyPayload>, "key in set");
    static_assert(!InputEventSet::contains<int>, "int not in set");
}

} // namespace (anonymous)
