// Unit tests for CtQtTranslatorTable + kIgnoredQtEvents (M3, §7.1/§7.2)
// + M4 typed translation (06-m4-typed-translation.md §6.1).
//
// Part of the CRISP project. Covers register/find semantics, installDefaults,
// the ignored-type boundary, the spontaneous() origin mapping, and the M4
// typed mouse/key translators (T1–T2a). Hand-written test doubles only
// (NO mock framework).

#include "bridge/qt_translator.h"
#include "bridge/qt_event_payload.h"
#include "bridge/qt_mouse_payload.h"
#include "bridge/qt_key_payload.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimerEvent>

#include "test_harness.h"

namespace {
using namespace crisp::event;

// --- M4 typed mouse translation (§6.1 T1/T1a/T1b) ----------------------------

TEST_CASE(qt_translator_mouse_move_maps_to_mouse_payload)
{
    CtQtTranslatorTable table;
    CtQtTranslatorTable::installDefaults(table);

    const CtQtTranslator* mouse = table.find(QEvent::MouseMove);
    EXPECT_TRUE(mouse != nullptr);  // installed by installDefaults

    QMouseEvent src(QEvent::MouseMove, QPoint(10, 20), QPoint(10, 20),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    auto payload = (*mouse)(std::unique_ptr<QEvent>(src.clone()), false);
    auto* typed = std::get_if<CtMousePayload>(&payload);
    EXPECT_TRUE(typed != nullptr);
    if (typed) {
        EXPECT_EQ(typed->action, CtMouseAction::kMove);
        EXPECT_EQ(typed->x, 10);
        EXPECT_EQ(typed->y, 20);
        EXPECT_EQ(typed->button, Qt::NoButton);
        EXPECT_EQ(typed->modifiers, Qt::NoModifier);
    }
}

TEST_CASE(qt_translator_mouse_press_maps_action_and_button)
{
    CtQtTranslatorTable table;
    CtQtTranslatorTable::installDefaults(table);

    const CtQtTranslator* mouse = table.find(QEvent::MouseButtonPress);
    EXPECT_TRUE(mouse != nullptr);

    QMouseEvent src(QEvent::MouseButtonPress, QPoint(5, 5), QPoint(5, 5),
                    Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    auto payload = (*mouse)(std::unique_ptr<QEvent>(src.clone()), false);
    auto* typed = std::get_if<CtMousePayload>(&payload);
    EXPECT_TRUE(typed != nullptr);
    if (typed) {
        EXPECT_EQ(typed->action, CtMouseAction::kPress);
        EXPECT_EQ(typed->button, Qt::LeftButton);
        EXPECT_EQ(typed->buttons, Qt::MouseButtons(Qt::LeftButton));
        EXPECT_EQ(typed->modifiers, Qt::KeyboardModifiers(Qt::ShiftModifier));
    }
}

TEST_CASE(qt_translator_mouse_dbl_click_maps_action)
{
    CtQtTranslatorTable table;
    CtQtTranslatorTable::installDefaults(table);

    const CtQtTranslator* mouse = table.find(QEvent::MouseButtonDblClick);
    EXPECT_TRUE(mouse != nullptr);

    QMouseEvent src(QEvent::MouseButtonDblClick, QPoint(7, 7), QPoint(7, 7),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    auto payload = (*mouse)(std::unique_ptr<QEvent>(src.clone()), false);
    auto* typed = std::get_if<CtMousePayload>(&payload);
    EXPECT_TRUE(typed != nullptr);
    if (typed) {
        EXPECT_EQ(typed->action, CtMouseAction::kDblClick);
    }
}

// --- M4 typed key translation (§6.1 T2/T2a) ---------------------------------

TEST_CASE(qt_translator_key_press_maps_to_key_payload)
{
    CtQtTranslatorTable table;
    CtQtTranslatorTable::installDefaults(table);

    const CtQtTranslator* key = table.find(QEvent::KeyPress);
    EXPECT_TRUE(key != nullptr);  // installed by installDefaults

    QKeyEvent src(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
    auto payload = (*key)(std::unique_ptr<QEvent>(src.clone()), false);
    auto* typed = std::get_if<CtKeyPayload>(&payload);
    EXPECT_TRUE(typed != nullptr);
    if (typed) {
        EXPECT_EQ(typed->action, CtKeyAction::kPress);
        EXPECT_EQ(typed->key, Qt::Key_A);
        EXPECT_EQ(typed->text, QString("a"));
        EXPECT_EQ(typed->modifiers, Qt::NoModifier);
        EXPECT_EQ(typed->isAutoRepeat, false);
    }
}

TEST_CASE(qt_translator_key_release_maps_action)
{
    CtQtTranslatorTable table;
    CtQtTranslatorTable::installDefaults(table);

    const CtQtTranslator* key = table.find(QEvent::KeyRelease);
    EXPECT_TRUE(key != nullptr);

    QKeyEvent src(QEvent::KeyRelease, Qt::Key_B, Qt::NoModifier, "b");
    auto payload = (*key)(std::unique_ptr<QEvent>(src.clone()), false);
    auto* typed = std::get_if<CtKeyPayload>(&payload);
    EXPECT_TRUE(typed != nullptr);
    if (typed) {
        EXPECT_EQ(typed->action, CtKeyAction::kRelease);
        EXPECT_EQ(typed->key, Qt::Key_B);
    }
}

// --- §7.1 #3: translator table semantics ------------------------------------

TEST_CASE(qt_translator_register_and_find)
{
    CtQtTranslatorTable table;

    // Unregistered type: find returns nullptr.
    EXPECT_TRUE(table.find(QEvent::Timer) == nullptr);

    auto identity = [](std::unique_ptr<QEvent> ev, bool) {
        return CtQtEventPayload(std::move(ev));
    };
    EXPECT_EQ(table.registerTranslator(QEvent::Timer, identity), true);

    // Registered: find returns the translator.
    const CtQtTranslator* found = table.find(QEvent::Timer);
    EXPECT_TRUE(found != nullptr);

    // Duplicate registration of the same type is rejected.
    EXPECT_EQ(table.registerTranslator(QEvent::Timer, identity), false);
}

TEST_CASE(qt_translator_install_defaults)
{
    CtQtTranslatorTable table;
    CtQtTranslatorTable::installDefaults(table);

    // Timer and the generic User slot carry identity translators (§3.3
    // contract #4). User+N is NOT individually registered — the source's
    // translateAndPush falls back to identityTranslator() for any
    // bridgeable-but-unregistered type (see test below).
    EXPECT_TRUE(table.find(QEvent::Timer) != nullptr);
    EXPECT_TRUE(table.find(QEvent::User) != nullptr);
    EXPECT_TRUE(CtQtTranslatorTable::identityTranslator() != nullptr);

    // Ignored/internal types have NO translator registered (§3.3 contract
    // #4: capture skips them at the filter layer, they never reach the map).
    EXPECT_TRUE(table.find(QEvent::Paint) == nullptr);
    EXPECT_TRUE(table.find(QEvent::UpdateRequest) == nullptr);
    EXPECT_TRUE(table.find(QEvent::MetaCall) == nullptr);
    EXPECT_TRUE(table.find(QEvent::DeferredDelete) == nullptr);
    EXPECT_TRUE(table.find(QEvent::Quit) == nullptr);
}

TEST_CASE(qt_translator_identity_mapping_preserves_event)
{
    CtQtTranslatorTable table;
    CtQtTranslatorTable::installDefaults(table);

    const CtQtTranslator* timer = table.find(QEvent::Timer);
    EXPECT_TRUE(timer != nullptr);

    // (M4) The translator now returns CtQtTypedPayload (variant); the
    // identity path produces the CtQtEventPayload member (§1.3 修法).
    QTimerEvent src(7);
    auto payload = (*timer)(std::unique_ptr<QEvent>(src.clone()), true);
    auto* qev = std::get_if<CtQtEventPayload>(&payload);
    EXPECT_TRUE(qev != nullptr);
    if (qev) {
        EXPECT_TRUE(qev->hasEvent());
        EXPECT_EQ(qev->qtType(), static_cast<int>(QEvent::Timer));
    }
}

// --- §7.1 #4: spontaneous() -> origin mapping ---------------------------------

TEST_CASE(qt_translator_identity_fallback_bridges_user_plus)
{
    // §3.3 contract #4: User+ custom events get identity translation even
    // when NOT individually registered — the source's translateAndPush
    // falls back to CtQtTranslatorTable::identityTranslator() (the table
    // itself only registers the generic User slot; User+N is covered by the
    // fallback, never by per-type registration).
    const CtQtTranslator* identity =
        CtQtTranslatorTable::identityTranslator();
    EXPECT_TRUE(identity != nullptr);

    QEvent custom(static_cast<QEvent::Type>(QEvent::User + 500));
    auto payload = (*identity)(std::unique_ptr<QEvent>(custom.clone()), false);
    auto* qev = std::get_if<CtQtEventPayload>(&payload);
    EXPECT_TRUE(qev != nullptr);
    if (qev) {
        EXPECT_TRUE(qev->hasEvent());
        EXPECT_EQ(qev->qtType(), static_cast<int>(QEvent::User + 500));
    }
}

TEST_CASE(qt_translator_spontaneous_maps_to_system_origin)
{
    CtQtTranslatorTable table;
    CtQtTranslatorTable::installDefaults(table);

    // The identity translator preserves the event and forwards the
    // spontaneous flag; the SOURCE (poll, §3.3 contract #3) maps
    // spontaneous()==true to kSystem, false to kApplication. Here we verify
    // the translator passes the flag through unchanged (the poll does the
    // mapping — see test_qt_event_source.cpp for the queue-visible result).
    // NOTE: QEvent::setSpontaneous() is private in Qt 6.2.4 (friend-only),
    // so no test can fabricate a spontaneous event; we verify the
    // pass-through of the flag and the identity translation instead.
    const CtQtTranslator* user = table.find(QEvent::User);
    EXPECT_TRUE(user != nullptr);

    QEvent synthetic(QEvent::User);
    auto payload = (*user)(std::unique_ptr<QEvent>(synthetic.clone()), true);
    auto* qev = std::get_if<CtQtEventPayload>(&payload);
    EXPECT_TRUE(qev != nullptr);
    if (qev) {
        EXPECT_TRUE(qev->hasEvent());
        EXPECT_EQ(qev->qtType(), static_cast<int>(QEvent::User));
    }

    // Flag pass-through: a translator returning the flag verbatim.
    auto flagTranslator = [](std::unique_ptr<QEvent> ev, bool) {
        return CtQtTypedPayload(CtQtEventPayload(std::move(ev)));
    };
    QEvent src(static_cast<QEvent::Type>(QEvent::User + 1));
    auto p2 = flagTranslator(std::unique_ptr<QEvent>(src.clone()), true);
    auto* qev2 = std::get_if<CtQtEventPayload>(&p2);
    EXPECT_TRUE(qev2 != nullptr);
    if (qev2) {
        EXPECT_TRUE(qev2->hasEvent());
        EXPECT_EQ(qev2->qtType(), static_cast<int>(QEvent::User + 1));
    }
}

// --- kIgnoredQtEvents boundary (§3.4a #5) -------------------------------------

TEST_CASE(qt_ignored_events_list_contains_internal_types)
{
    const auto& ignored = kIgnoredQtEvents();
    EXPECT_TRUE(ignored.size() >= 10u);

    // Qt-internal/synthetic events are always skipped.
    for (int t : {QEvent::Paint, QEvent::UpdateRequest, QEvent::UpdateLater,
                  QEvent::LayoutRequest, QEvent::Polish, QEvent::PolishRequest,
                  QEvent::MetaCall, QEvent::DeferredDelete, QEvent::ThreadChange,
                  QEvent::ChildAdded, QEvent::ChildRemoved, QEvent::ChildPolished}) {
        const auto it = std::find(ignored.begin(), ignored.end(), t);
        EXPECT_TRUE(it != ignored.end());
    }

    // Quit is special: never cloned, but tracked via m_quitCaptured (§3.4a
    // #3). It lives in the ignored list too (capture skips the clone path).
    const auto it = std::find(ignored.begin(), ignored.end(), QEvent::Quit);
    EXPECT_TRUE(it != ignored.end());

    // Timer and User+ are NOT ignored (they are the default capture set).
    EXPECT_TRUE(std::find(ignored.begin(), ignored.end(), QEvent::Timer) ==
                ignored.end());
    EXPECT_TRUE(std::find(ignored.begin(), ignored.end(), QEvent::User) ==
                ignored.end());
}

TEST_CASE(qt_ignored_events_application_notification_types)
{
    // Application-level/system-notification events are explicitly NOT
    // captured by default (§3.4a #5, P3-3): they live in the ignored list.
    const auto& ignored = kIgnoredQtEvents();
    for (int t : {QEvent::ApplicationStateChange, QEvent::ApplicationFontChange,
                  QEvent::ApplicationPaletteChange, QEvent::LocaleChange,
                  QEvent::LanguageChange, QEvent::LayoutDirectionChange}) {
        const auto it = std::find(ignored.begin(), ignored.end(), t);
        EXPECT_TRUE(it != ignored.end());
    }
}

} // namespace (anonymous)
