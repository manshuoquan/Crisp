// Unit tests: crisp::ui widget base (CtWidgetBase, UI1 §1.1/§3).
//
// Covers: object tree + ownership (P5), explicit state (P12), geometry
// normalization, dirty region + trace records (P4/D2 + P6/D3), input
// gating by disabled state, cycle-reparent rejection, createWidget<W>
// factory path, and hot-path zero-allocation on markDamage (§5.2).
//
// Hand-written test doubles, no mock framework (project rule).

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include "ui/ct_widget_base.h"
#include "event/ct_event.h"
#include "event/ct_input.h"

#include "test_harness.h"

using crisp::event::CtEvent;
using crisp::event::CtEventOrigin;
using crisp::input::CtKeyPayload;
using crisp::input::CtMouseAction;
using crisp::input::CtMouseButton;
using crisp::input::CtMousePayload;
using crisp::ui::CtRect;
using crisp::ui::CtWidgetBase;
using crisp::ui::CtWidgetPhase;

#include "alloc_tracking.h" // g_allocCount / g_counting (defined below)

// Global counting operators (scoped to this test binary — the single
// definition, see alloc_tracking.h). markDamage's hot path must not
// allocate; registration/construction allocate BEFORE the counter is
// armed.
std::atomic<std::size_t> g_allocCount{0};
std::atomic<bool> g_counting{false};

void* operator new(std::size_t size) {
    if (g_counting.load(std::memory_order_relaxed))
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size);
    if (!p)
        throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t size) {
    if (g_counting.load(std::memory_order_relaxed))
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size);
    if (!p)
        throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

TEST_CASE(widget_create_attaches_to_parent_and_records_created)
{
    auto root = CtWidgetBase::create();
    EXPECT_TRUE(root->parent().expired());
    EXPECT_EQ(root->children().size(), 0u);

    auto child = CtWidgetBase::create(root);
    EXPECT_EQ(child->parent().lock(), root);
    EXPECT_EQ(root->children().size(), 1u);

    // kCreated trace records exist for root (top-level: parentObjectId 0)
    // and child (parentObjectId == root->objectId()).
    crisp::ui::CtWidgetRecord out[8];
    const std::size_t n = child->trace().copyOut(out, 8);
    EXPECT_GE(n, 1u);
    // The child's OWN trace contains its kCreated (parent recorded).
    EXPECT_EQ(static_cast<int>(out[0].phase),
              static_cast<int>(CtWidgetPhase::kCreated));
    EXPECT_EQ(out[0].parentObjectId, root->objectId());
}

TEST_CASE(widget_set_parent_moves_ownership)
{
    auto a = CtWidgetBase::create();
    auto b = CtWidgetBase::create();
    auto child = CtWidgetBase::create(a);
    EXPECT_EQ(a->children().size(), 1u);

    child->setParent(b);
    EXPECT_EQ(child->parent().lock(), b);
    EXPECT_EQ(a->children().size(), 0u); // removed from old parent
    EXPECT_EQ(b->children().size(), 1u); // added to new parent

    // Top-level: setParent(nullptr) detaches.
    child->setParent(nullptr);
    EXPECT_TRUE(child->parent().expired());
    EXPECT_EQ(b->children().size(), 0u);
}

TEST_CASE(widget_dtor_detaches_from_parent)
{
    auto root = CtWidgetBase::create();
    auto child = CtWidgetBase::create(root);
    EXPECT_EQ(root->children().size(), 1u);

    // The root holds a weak_ptr; the child's shared_ptr is the only strong
    // owner. Dropping it destroys the child AND removes the entry from the
    // parent's z-order list (P5 RAII detach — owner_before identity match
    // works even with the strong count at zero).
    child.reset();
    EXPECT_EQ(root->children().size(), 0u);
    EXPECT_EQ(root->childAt(5, 5), nullptr);
}

TEST_CASE(widget_geometry_normalizes_and_hit_tests)
{
    auto w = CtWidgetBase::create();
    w->setGeometry(CtRect{10, 20, 100, 50});
    EXPECT_EQ(w->geometry().x, 10);
    EXPECT_EQ(w->width(), 100);
    EXPECT_EQ(w->height(), 50);
    EXPECT_TRUE(w->hitTest(10, 20));
    EXPECT_TRUE(w->hitTest(109, 69));
    EXPECT_FALSE(w->hitTest(110, 70));

    // Negative size normalizes to zero.
    w->setGeometry(CtRect{0, 0, -5, -5});
    EXPECT_EQ(w->width(), 0);
    EXPECT_EQ(w->height(), 0);
    EXPECT_FALSE(w->hitTest(0, 0));
}

TEST_CASE(widget_child_at_returns_topmost)
{
    auto root = CtWidgetBase::create();
    root->setGeometry(CtRect{0, 0, 200, 200});
    auto bottom = CtWidgetBase::create(root);
    bottom->setGeometry(CtRect{0, 0, 100, 100});
    auto top = CtWidgetBase::create(root);
    top->setGeometry(CtRect{0, 0, 100, 100});

    // top was created last -> appended last -> topmost (front of z-order).
    EXPECT_EQ(root->childAt(50, 50), top);
}

TEST_CASE(widget_damage_unions_and_traces)
{
    auto w = CtWidgetBase::create();
    w->setGeometry(CtRect{0, 0, 200, 200});

    EXPECT_TRUE(w->dirtyRegion().isEmpty());
    w->markDamage(CtRect{0, 0, 10, 10});
    w->markDamage(CtRect{50, 60, 20, 20});
    EXPECT_FALSE(w->dirtyRegion().isEmpty());
    EXPECT_EQ(w->dirtyRegion().bounds.x, 0);
    EXPECT_EQ(w->dirtyRegion().bounds.y, 0);
    EXPECT_EQ(w->dirtyRegion().bounds.width, 70);
    EXPECT_EQ(w->dirtyRegion().bounds.height, 80);

    // kDamage trace: accumulated count == 2.
    crisp::ui::CtWidgetRecord out[4];
    const std::size_t n = w->trace().copyOut(out, 4);
    EXPECT_GE(n, 2u);
    EXPECT_EQ(static_cast<int>(out[n - 1].phase),
              static_cast<int>(CtWidgetPhase::kDamage));
    EXPECT_EQ(out[n - 1].damageRectCount, 2u);

    w->clearDamage();
    EXPECT_TRUE(w->dirtyRegion().isEmpty());
    w->markDamage(CtRect{0, 0, 5, 5});
    crisp::ui::CtWidgetRecord out2[8];
    const std::size_t n2 = w->trace().copyOut(out2, 8);
    // Last record has count == 1 after the reset.
    EXPECT_EQ(out2[n2 - 1].damageRectCount, 1u);
}

TEST_CASE(widget_disabled_rejects_input)
{
    auto w = CtWidgetBase::create();
    w->setEnabled(false);

    CtEvent<CtMousePayload> mouse(CtEventOrigin::kApplication,
                                  CtMousePayload{});
    w->dispatchInput(mouse);
    EXPECT_FALSE(mouse.accepted); // disabled: ignored, not delivered

    CtEvent<CtKeyPayload> key(CtEventOrigin::kApplication,
                              CtKeyPayload{});
    w->dispatchInput(key);
    EXPECT_FALSE(key.accepted);

    // Re-enabled widgets pass the gate (UI1: no handlers yet -> unhandled).
    w->setEnabled(true);
    CtEvent<CtMousePayload> mouse2(CtEventOrigin::kApplication,
                                   CtMousePayload{});
    w->dispatchInput(mouse2);
    EXPECT_FALSE(mouse2.accepted);
}

TEST_CASE(widget_state_defaults_are_explicit)
{
    auto w = CtWidgetBase::create();
    EXPECT_TRUE(w->isVisible());
    EXPECT_TRUE(w->isEnabled());
    EXPECT_FALSE(w->focusable());
    EXPECT_FALSE(w->hasFocus());
    EXPECT_EQ(static_cast<int>(w->widgetState().focusPolicy), 0); // kNoFocus

    w->setVisible(false);
    w->setEnabled(false);
    EXPECT_FALSE(w->isVisible());
    EXPECT_FALSE(w->isEnabled());
}

// A minimal UI1 widget subclass exercising the createWidget<W> factory.
class CtProbeWidget : public CtWidgetBase {
public:
    explicit CtProbeWidget(const std::shared_ptr<CtWidgetBase>& parent)
        : CtWidgetBase(parent) {}
};

TEST_CASE(widget_create_widget_factory_attaches_subclass)
{
    auto root = CtWidgetBase::create();
    root->setGeometry(CtRect{0, 0, 200, 200});
    auto probe = CtWidgetBase::createWidget<CtProbeWidget>(root);
    probe->setGeometry(CtRect{0, 0, 100, 100});
    EXPECT_TRUE(probe->parent().lock() == root);
    EXPECT_EQ(root->children().size(), 1u);
    EXPECT_EQ(root->childAt(50, 50), probe);
    // Parent association recorded in the subclass's own trace.
    crisp::ui::CtWidgetRecord out[4];
    const std::size_t n = probe->trace().copyOut(out, 4);
    EXPECT_GE(n, 1u);
    EXPECT_EQ(static_cast<int>(out[0].phase),
              static_cast<int>(CtWidgetPhase::kCreated));
    EXPECT_EQ(out[0].parentObjectId, root->objectId());
}

TEST_CASE(widget_cycle_reparent_is_rejected)
{
    auto a = CtWidgetBase::create();
    auto b = CtWidgetBase::create(a);
    auto c = CtWidgetBase::create(b);
    // a is an ANCESTOR of c: c->setParent(a) would create a cycle -> rejected.
    a->setParent(c);
    EXPECT_TRUE(a->parent().expired()); // unchanged (still top-level)
    EXPECT_EQ(c->children().size(), 0u);
    // Self-parenting is also rejected.
    a->setParent(a);
    EXPECT_TRUE(a->parent().expired());
}

TEST_CASE(widget_set_parent_to_same_parent_is_noop)
{
    auto root = CtWidgetBase::create();
    root->setGeometry(CtRect{0, 0, 200, 200});
    auto child = CtWidgetBase::create(root);
    child->setGeometry(CtRect{0, 0, 100, 100});
    EXPECT_EQ(root->children().size(), 1u);

    // Re-parenting to the SAME parent must be an idempotent no-op: the
    // child stays listed exactly once (P5 tree invariant), hit-testable.
    child->setParent(root);
    EXPECT_EQ(child->parent().lock(), root);
    EXPECT_EQ(root->children().size(), 1u);
    EXPECT_EQ(root->childAt(50, 50), child);
}

TEST_CASE(widget_create_widget_factory_destroy_subclass_cleans_tree)
{
    auto root = CtWidgetBase::create();
    {
        auto probe = CtWidgetBase::createWidget<CtProbeWidget>(root);
        EXPECT_EQ(root->children().size(), 1u);
    } // subclass destroyed: its dtor runs, then the base dtor detaches
    EXPECT_EQ(root->children().size(), 0u); // P5 RAII detach through the
                                            // createWidget<W> path
    EXPECT_EQ(root->childAt(5, 5), nullptr);
}

TEST_CASE(widget_destroyed_trace_keeps_parent_association)
{
    auto root = CtWidgetBase::create();
    std::uint64_t rootId = root->objectId();
    {
        auto child = CtWidgetBase::createWidget<CtProbeWidget>(root);
        EXPECT_EQ(root->children().size(), 1u);
        // While alive, the child's ring already shows kCreated with the
        // parent association (the kDestroyed record is written by the base
        // dtor at destruction; the ring is unreachable after destruction —
        // documented per-widget ring limitation, docs/ui/04-ui1-architecture.md
        // §2.7 ruling).
        crisp::ui::CtWidgetRecord out[4];
        const std::size_t n = child->trace().copyOut(out, 4);
        EXPECT_GE(n, 1u);
        EXPECT_EQ(static_cast<int>(out[0].phase),
                  static_cast<int>(crisp::ui::CtWidgetPhase::kCreated));
        EXPECT_EQ(out[0].parentObjectId, rootId);
    } // child destroyed here: base dtor records kDestroyed then detaches
    EXPECT_EQ(root->children().size(), 0u); // P5 RAII detach (no zombie entry)
    EXPECT_EQ(root->childAt(5, 5), nullptr);
}

TEST_CASE(widget_mark_damage_hot_path_allocates_zero)
{    auto w = CtWidgetBase::create();
    w->setGeometry(CtRect{0, 0, 200, 200});

    // Warm-up: the trace ring and dirty region are fully constructed; the
    // markDamage path must perform ZERO heap allocations (§5.2 acceptance).
    w->markDamage(CtRect{0, 0, 1, 1});
    w->clearDamage();

    g_allocCount.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    w->markDamage(CtRect{10, 10, 20, 20});
    w->markDamage(CtRect{50, 50, 10, 10});
    w->dirtyRegion();   // pure accessor: must not allocate either

    g_counting.store(false, std::memory_order_relaxed);
    const std::size_t allocs = g_allocCount.load(std::memory_order_relaxed);

    // NOTE: the counting operator new only observes allocations made from
    // THIS test TU; the hot path's members (CtRegion/CtRect/CtWidgetTrace
    // ring) are all value/stack members with no hidden allocation, so a
    // zero count here is a faithful "zero allocation on the damage path"
    // assertion (review D1 ruling).
    EXPECT_EQ(w->dirtyRegion().bounds.width, 50); // unioned (10..60)
    EXPECT_EQ(w->dirtyRegion().bounds.height, 50);
    EXPECT_EQ(allocs, 0u);

    // clearDamage is also a zero-allocation reset (review D1): assert AFTER
    // the union checks so the region content is verified first.
    g_allocCount.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);
    w->clearDamage();
    g_counting.store(false, std::memory_order_relaxed);
    EXPECT_EQ(g_allocCount.load(std::memory_order_relaxed), 0u);
    EXPECT_TRUE(w->dirtyRegion().isEmpty());
}

// Cross-thread degradation (§4.2): in RELEASE builds assertSameThread()
// returns false and mutating calls degrade to no-ops. This contract CANNOT
// be exercised from a single-threaded test binary: the widget's affinity is
// inherited from CtReceiver and fixed at construction (no public re-bind
// surface on CtWidgetBase; rebindToCurrentThread is reserved for deliberate
// receiver moves, event-system §10.2). The release no-op path is therefore
// covered by code inspection + the assertSameThread contract (debug abort),
// and full cross-thread input injection is UI3's input-mapper scope (§4.3).
// Tracked in §16 acceptance checklist as a UI3 regression item (review D3
// ruling — documented, not stubbed).

} // namespace (anonymous)
