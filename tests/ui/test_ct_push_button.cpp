// Unit tests: crisp::ui push button (CtPushButton, UI4 §4.4.2).
//
// Covers the EXPLICIT state machine: normal→hovered→pressed→released→
// clicked (pure logic, no platform), press-outside-ignore, release-
// outside cancel, disabled transitions, and the onClick semantic event
// (CtClickPayload via CtWidgetBase::emitSemantic).
//
// Hand-written test doubles, no mock framework (project rule).

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>

#include "ui/ct_push_button.h"
#include "event/ct_dispatcher.h"
#include "event/ct_event.h"
#include "event/ct_input.h"

#include "test_harness.h"

using crisp::event::CtEvent;
using crisp::event::CtEventOrigin;
using crisp::input::CtClickPayload;
using crisp::input::CtMouseAction;
using crisp::input::CtMouseButton;
using crisp::input::CtMouseButtons;
using crisp::input::CtMousePayload;
using crisp::ui::CtPushButton;
using crisp::ui::CtRect;
using crisp::ui::CtWidgetBase;

namespace {

/// Dispatch a synthetic mouse event to `w` (widget-local coords).
void sendMouse(const std::shared_ptr<CtWidgetBase>& w, CtMousePayload p) {
    CtEvent<CtMousePayload> ev(CtEventOrigin::kSystem, p);
    w->dispatchInput(ev);
}

TEST_CASE(button_initial_state_is_normal)
{
    auto root = CtWidgetBase::create();
    auto btn = CtPushButton::create(root, u"OK");
    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kNormal));
    EXPECT_EQ(btn->text(), u"OK");
    // Default geometry: zero size (the owner places it).
    EXPECT_EQ(btn->width(), 0);
    EXPECT_EQ(btn->height(), 0);
}

TEST_CASE(button_press_inside_arms_then_release_inside_clicks)
{
    auto root = CtWidgetBase::create();
    root->setGeometry(CtRect{0, 0, 200, 200});
    auto btn = CtPushButton::create(root, u"OK");
    btn->setGeometry(CtRect{10, 10, 100, 40});

    int clicks = 0;
    auto token = btn->onClick([&](CtEvent<CtClickPayload>& ev) {
        ++clicks;
        EXPECT_EQ(ev.payload().x, 30);
        EXPECT_EQ(ev.payload().y, 25);
        EXPECT_EQ(ev.payload().button, CtMouseButton::kPrimary);
        ev.accept();
    });

    CtMousePayload press;
    press.action = CtMouseAction::kPress;
    press.x = 30;
    press.y = 25;
    press.button = CtMouseButton::kPrimary;
    press.buttons.bits = CtMouseButtons::kPrimary;
    sendMouse(btn, press);
    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kPressed));

    CtMousePayload release;
    release.action = CtMouseAction::kRelease;
    release.x = 35;
    release.y = 30;
    release.button = CtMouseButton::kPrimary;
    release.buttons.bits = CtMouseButtons::kNone;
    sendMouse(btn, release);

    EXPECT_EQ(clicks, 1);
    // After a completed click: hovered (pointer still inside).
    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kHovered));
}

TEST_CASE(button_release_outside_cancels)
{
    auto root = CtWidgetBase::create();
    auto btn = CtPushButton::create(root, u"Cancel");
    btn->setGeometry(CtRect{0, 0, 80, 30});

    int clicks = 0;
    auto token = btn->onClick([&](CtEvent<CtClickPayload>&) { ++clicks; });

    CtMousePayload press;
    press.action = CtMouseAction::kPress;
    press.x = 40;
    press.y = 15;
    press.button = CtMouseButton::kPrimary;
    sendMouse(btn, press);
    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kPressed));

    CtMousePayload release;
    release.action = CtMouseAction::kRelease;
    release.x = 200; // outside the button
    release.y = 200;
    release.button = CtMouseButton::kPrimary;
    sendMouse(btn, release);

    EXPECT_EQ(clicks, 0); // cancelled: no semantic click
    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kNormal));
}

TEST_CASE(button_press_outside_is_ignored)
{
    auto root = CtWidgetBase::create();
    auto btn = CtPushButton::create(root, u"X");
    btn->setGeometry(CtRect{0, 0, 50, 20});

    CtMousePayload press;
    press.action = CtMouseAction::kPress;
    press.x = 100; // outside
    press.y = 100;
    press.button = CtMouseButton::kPrimary;
    sendMouse(btn, press);

    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kNormal));
    // Release without an armed press: no click, stays normal.
    CtMousePayload release;
    release.action = CtMouseAction::kRelease;
    release.x = 100;
    release.y = 100;
    release.button = CtMouseButton::kPrimary;
    sendMouse(btn, release);
    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kNormal));
}

TEST_CASE(button_hover_tracks_inside_outside)
{
    auto root = CtWidgetBase::create();
    auto btn = CtPushButton::create(root, u"H");
    btn->setGeometry(CtRect{0, 0, 60, 20});

    CtMousePayload move;
    move.action = CtMouseAction::kMove;
    move.x = 30;
    move.y = 10;
    sendMouse(btn, move);
    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kHovered));

    move.x = 100; // outside
    move.y = 100;
    sendMouse(btn, move);
    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kNormal));
}

TEST_CASE(button_disable_transitions)
{
    auto root = CtWidgetBase::create();
    auto btn = CtPushButton::create(root, u"D");
    btn->setGeometry(CtRect{0, 0, 40, 20});

    // Enabled hover -> disabled.
    CtMousePayload move;
    move.action = CtMouseAction::kMove;
    move.x = 20;
    move.y = 10;
    sendMouse(btn, move);
    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kHovered));

    btn->setEnabled(false);
    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kDisabled));
    EXPECT_FALSE(btn->isEnabled());

    // Disabled: input is gated (CtWidgetBase::dispatchInput rejects) —
    // the state machine never runs, state stays kDisabled.
    CtMousePayload press;
    press.action = CtMouseAction::kPress;
    press.x = 20;
    press.y = 10;
    press.button = CtMouseButton::kPrimary;
    sendMouse(btn, press);
    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kDisabled));

    // Re-enable: kDisabled -> kNormal.
    btn->setEnabled(true);
    EXPECT_EQ(static_cast<int>(btn->state()),
              static_cast<int>(CtPushButton::CtButtonState::kNormal));
}

TEST_CASE(button_text_set_text_updates_and_marks_damage)
{
    auto root = CtWidgetBase::create();
    auto btn = CtPushButton::create(root, u"Old");
    btn->setGeometry(CtRect{0, 0, 60, 20});

    btn->setText(u"New");
    EXPECT_EQ(btn->text(), u"New");
    EXPECT_FALSE(btn->dirtyRegion().isEmpty()); // setText marks damage
    btn->clearDamage();
}

} // namespace (anonymous)
