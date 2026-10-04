// Unit tests for CtEvent<T>: value semantics, constructors, acceptance state.
//
// Part of the CRISP event subsystem (crisp::event). Covers §7.3 contracts:
// origin naming, the enable_if-disambiguated ctors, value semantics, the
// accepted flag + accept()/ignore(), and the compile-time traits name.

#include "event/ct_event.h"

#include <string>

#include "test_harness.h"

namespace {
using namespace crisp::event;

struct MouseMovePayload {
    int x = 0;
    int y = 0;
};

struct KeyPressPayload {
    int key = 0;
};

} // namespace (anonymous)

// Customize the trace name for payload types (observability nicety).
namespace crisp::event {
template <>
struct CtEventTraits<MouseMovePayload> {
    static constexpr bool kIsValid = true;
    static constexpr std::string_view name = "MouseMove";
};

template <>
struct CtEventTraits<KeyPressPayload> {
    static constexpr bool kIsValid = true;
    static constexpr std::string_view name = "KeyPress";
};
} // namespace crisp::event

namespace {
using namespace crisp::event;

TEST_CASE(event_payload_ctor_defaults_to_application_origin)
{
    CtEvent<MouseMovePayload> ev(MouseMovePayload{10, 20});
    EXPECT_EQ(ev.payload().x, 10);
    EXPECT_EQ(ev.payload().y, 20);
    EXPECT_EQ(ev.origin(), CtEventOrigin::kApplication);
    EXPECT_EQ(ev.sequence(), 0); // assigned at dispatch, not construction
    EXPECT_EQ(ev.priority(), 0);
}

TEST_CASE(event_origin_first_ctor_sets_origin)
{
    CtEvent<KeyPressPayload> ev(CtEventOrigin::kSystem, KeyPressPayload{65});
    EXPECT_EQ(ev.payload().key, 65);
    EXPECT_EQ(ev.origin(), CtEventOrigin::kSystem);
}

TEST_CASE(event_origin_ctor_with_multiple_args)
{
    // enable_if disambiguation: (origin, args...) must resolve to the
    // origin-first overload, never forward the origin into T.
    CtEvent<MouseMovePayload> ev(CtEventOrigin::kApplication, MouseMovePayload{1, 2});
    EXPECT_EQ(ev.origin(), CtEventOrigin::kApplication);
    EXPECT_EQ(ev.payload().x, 1);
}

TEST_CASE(event_accept_ignore_roundtrip)
{
    CtEvent<KeyPressPayload> ev(KeyPressPayload{65});
    EXPECT_EQ(ev.accepted, false);
    ev.accept();
    EXPECT_EQ(ev.accepted, true);
    ev.ignore();
    EXPECT_EQ(ev.accepted, false);
}

TEST_CASE(event_is_copyable_value_semantics)
{
    CtEvent<MouseMovePayload> ev(MouseMovePayload{1, 2});
    CtEvent<MouseMovePayload> copy = ev; // value copy, not pointer copy
    copy.payload().x;                    // payload is read-only (const)
    EXPECT_EQ(ev.payload().x, 1);        // original unchanged
    EXPECT_EQ(copy.payload().x, 1);
}

TEST_CASE(event_type_name_is_compile_time)
{
    static_assert(CtEventTraits<MouseMovePayload>::kIsValid, "traits specialized");
    static_assert(CtEventTraits<MouseMovePayload>::name == "MouseMove");
    static_assert(CtEventTraits<KeyPressPayload>::name == "KeyPress");
}

TEST_CASE(event_set_priority_before_dispatch)
{
    CtEvent<KeyPressPayload> ev(KeyPressPayload{7});
    EXPECT_EQ(ev.priority(), 0);
    ev.setPriority(42);
    EXPECT_EQ(ev.priority(), 42);
}

} // namespace
