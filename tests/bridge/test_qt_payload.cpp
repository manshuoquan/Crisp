// Unit tests for CtQtEventPayload (M3, §7.1).
//
// Part of the CRISP project. Covers the RAII/move-only ownership contract,
// qtType()/hasEvent() semantics, release(), and the event-traits
// specialization. Hand-written test doubles only (NO mock framework).

#include "bridge/qt_event_payload.h"

#include <QTimerEvent>

#include "test_harness.h"

namespace {
using namespace crisp::event;

/// QEvent subclass that counts its own destruction (RAII verification).
class CountingEvent : public QEvent {
public:
    explicit CountingEvent(int type, int& alive)
        : QEvent(static_cast<QEvent::Type>(type)), m_alive(alive) {
        ++m_alive;
    }

    ~CountingEvent() override { --m_alive; }

    // Qt 6.2.4 QEvent::clone() returns QEvent* (not unique_ptr): callers
    // take ownership of the returned heap copy.
    QEvent* clone() const override {
        return new CountingEvent(static_cast<int>(type()), m_alive);
    }

private:
    int& m_alive;
};

// --- §7.1 #1: RAII ownership ------------------------------------------------

TEST_CASE(qt_payload_raii_move_and_release)
{
    int alive = 0;
    {
        // Construct from an owned event; destruction frees it.
        auto payload = CtQtEventPayload(
            std::unique_ptr<QEvent>(new CountingEvent(QEvent::User + 1, alive)));
        EXPECT_EQ(alive, 1);
        EXPECT_TRUE(payload.hasEvent());

        // Move transfers ownership without double-free.
        CtQtEventPayload moved(std::move(payload));
        EXPECT_FALSE(payload.hasEvent());
        EXPECT_TRUE(moved.hasEvent());
        EXPECT_EQ(alive, 1);
    }
    // Payload destroyed: the owned event is freed (RAII).
    EXPECT_EQ(alive, 0);
}

TEST_CASE(qt_payload_release_returns_ownership)
{
    int alive = 0;
    auto payload = CtQtEventPayload(
        std::unique_ptr<QEvent>(new CountingEvent(QEvent::User + 2, alive)));
    EXPECT_EQ(alive, 1);

    auto ev = payload.release();
    EXPECT_FALSE(payload.hasEvent());  // source is empty after release
    EXPECT_TRUE(ev != nullptr);
    EXPECT_EQ(alive, 1);

    ev.reset();  // explicit ownership release frees the event
    EXPECT_EQ(alive, 0);
}

TEST_CASE(qt_payload_null_marker)
{
    // A nullptr payload is a legal "empty marker" (§3.1 contract #3).
    CtQtEventPayload empty(nullptr);
    EXPECT_FALSE(empty.hasEvent());
    EXPECT_EQ(empty.event(), nullptr);
    EXPECT_EQ(empty.qtType(), 0);  // QEvent::None for null

    auto rel = empty.release();
    EXPECT_TRUE(rel == nullptr);
}

// --- §7.1 #2: qtType() / hasEvent() ------------------------------------------

TEST_CASE(qt_payload_qt_type_roundtrip)
{
    QTimerEvent timer(42);
    auto payload = CtQtEventPayload(std::unique_ptr<QEvent>(timer.clone()));
    EXPECT_TRUE(payload.hasEvent());
    EXPECT_EQ(payload.qtType(), static_cast<int>(QEvent::Timer));
    EXPECT_TRUE(payload.event() != nullptr);
}

// --- event traits specialization (compile-time) ------------------------------

TEST_CASE(qt_payload_event_traits_specialized)
{
    // Compile-time only: CtEvent<CtQtEventPayload> must be constructible
    // (requires CRISP_EVENT_TYPE). Runtime check keeps the test honest.
    static_assert(CtEventTraits<CtQtEventPayload>::kIsValid,
                  "CtQtEventPayload must specialize CtEventTraits");
    EXPECT_TRUE(CtEventTraits<CtQtEventPayload>::name == "QtEvent");
}

} // namespace (anonymous)
