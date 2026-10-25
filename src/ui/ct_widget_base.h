#pragma once

// ct_widget_base.h — crisp::ui core widget base (UI1).
//
// The root of the crisp::ui object tree: shared_ptr ownership + weak_ptr
// child references + RAII teardown (P5), explicit state struct (P12),
// typed input dispatch (P1), single-rect dirty region (P4, D2) and widget
// trace observability (P6, D3).
//
// Authoritative spec: docs/ui/04-ui1-architecture.md §1.1 + §2 + §3
// (sections of §3 still being finalized by the architecture designer; the
// fully specified surface of §1.1/§2/§4.1 is implemented here, remaining
// surface uses conservative, documented defaults).
//
// §3.9 ruling: CtWidgetBase INHERITS from crisp::event::CtReceiver —
// the receiver provides the stable objectId + thread affinity + the
// bindHandler<T> registration entry point for typed input handlers.

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "event/ct_event.h"         // CtEvent / CtEventOrigin (crisp::event)
#include "event/ct_input.h"         // crisp::input payloads
#include "event/ct_mouse_payload.h" // CtMousePayload (CRISP_EVENT_TYPE applied)
#include "event/ct_key_payload.h"   // CtKeyPayload
#include "event/ct_receiver.h"      // crisp::event::CtReceiver (base class)
#include "event/ct_thread_affinity.h" // crisp::event::CtThreadId
#include "ct_geometry.h"            // CtRect / CtRegion
#include "ct_widget_state.h"        // CtWidgetState / CtFocusPolicy
#include "ct_widget_trace.h"        // CtWidgetPhase / CtWidgetRecord / CtWidgetTrace

namespace crisp::ui {

/// Base class of every widget in the crisp::ui object tree.
///
/// OWNERSHIP CONTRACT (mandatory): every CtWidgetBase is owned by a
/// std::shared_ptr for its entire lifetime (external owners hold the strong
/// references). The tree itself stores ONLY weak references — children keep
/// a weak back-reference to the parent and the parent keeps weak child
/// entries; there are NO strong parent/child edges (no strong cycle, no
/// manual delete, no deleteLater()). The destructor detaches from the
/// parent (RAII) and removes its own entry from the parent's child list.
///
/// THREAD CONTRACT (UI1): the widget is affined to its creating thread
/// (inherited CtThreadAffinity via CtReceiver). All mutating operations
/// assert the calling thread owns the widget (assertSameThread, debug
/// abort / release false). Cross-thread manipulation is out of UI1 scope
/// (UI3's event loop integration routes input on the UI thread only).
///
/// INHERITANCE (UI1): CtWidgetBase derives from crisp::event::CtReceiver
/// ONLY — CtReceiver already brings std::enable_shared_from_this, and its
/// shared_from_this() is what CtReceiver::bindHandler() uses for the
/// registration entry point (UI4). CtWidgetBase deliberately does NOT add a
/// SECOND enable_shared_from_this base: std::shared_ptr's ctor initializes
/// exactly one base's weak reference, and with two bases the other's
/// shared_from_this() throws bad_weak_ptr (silently breaking bindHandler).
/// Tree membership uses m_selfWeak (built from shared_from_this() in the
/// factories, which hold a live strong reference).
class CtWidgetBase : public crisp::event::CtReceiver {
public:
    /// Sanctioned factory: create a widget owned by shared_ptr.
    /// `parent` may be empty (top-level widget).
    static std::shared_ptr<CtWidgetBase> create(
        const std::shared_ptr<CtWidgetBase>& parent = {});

    /// Sanctioned factory for widget subclasses: create a widget owned by
    /// shared_ptr, attached to `parent` (P5 — the ONLY sanctioned
    /// construction path besides create(); direct make_shared/new of a
    /// CtWidgetBase outside this factory is a contract violation: the weak
    /// self-reference needed for tree membership is set by this factory).
    /// `W`'s ctor must accept a shared_ptr parent as its FIRST argument and
    /// MUST NOT touch the object tree (m_selfWeak is set only after W's ctor
    /// returns; a tree call inside the ctor would store an empty weak_ptr).
    /// Subclass example:
    ///   class CtMyWidget : public CtWidgetBase {
    ///   public:
    ///       explicit CtMyWidget(const std::shared_ptr<CtWidgetBase>& parent)
    ///           : CtWidgetBase(parent) {}
    ///   };
    ///   auto w = CtWidgetBase::createWidget<CtMyWidget>(root);
    template <typename W = CtWidgetBase, typename... Args>
    static std::shared_ptr<W> createWidget(
        const std::shared_ptr<CtWidgetBase>& parent, Args&&... args) {
        static_assert(std::is_base_of_v<CtWidgetBase, W>,
                      "createWidget<W> requires W to derive from CtWidgetBase");
        std::shared_ptr<W> self =
            std::make_shared<W>(parent, std::forward<Args>(args)...);
        self->m_selfWeak = self;
        self->attachTo(parent);
        return self;
    }

    /// RAII: detach from parent, trace kDestroyed, release children.
    virtual ~CtWidgetBase();

    CtWidgetBase(const CtWidgetBase&) = delete;
    CtWidgetBase& operator=(const CtWidgetBase&) = delete;

protected:
    /// Default-construct the widget as a top-level (parent set later via
    /// setParent). PROTECTED: only subclasses may call it. A default-
    /// constructed widget has an EMPTY m_selfWeak until a factory sets it
    /// (creating one on the stack and setParent()ing it would insert a
    /// zombie weak_ptr into the tree). Factories (create/createWidget)
    /// set m_selfWeak before any tree operation.
    CtWidgetBase();

    /// Construct a widget with `parent` as its tree parent (P5: the only
    /// construction path that attaches; setParent is the re-parent entry).
    /// PROTECTED: reachable via the factories only (P5 sanctioned entry).
    explicit CtWidgetBase(const std::shared_ptr<CtWidgetBase>& parent);

public:
    // --- tree -----------------------------------------------------------

    /// The parent (empty weak_ptr for a top-level widget).
    std::weak_ptr<CtWidgetBase> parent() const noexcept { return m_parent; }

    /// Direct children, in z-order (back = topmost; the LAST inserted
    /// child is the topmost one — see childAt's reverse walk).
    const std::vector<std::weak_ptr<CtWidgetBase>>& children() const noexcept {
        return m_children;
    }

    /// Re-parent this widget: ownership MOVES to `newParent`
    /// (P5 — one sanctioned entry point, no new QWidget(parent) duality).
    void setParent(const std::shared_ptr<CtWidgetBase>& newParent);

    /// The top-most child at (px, py) in this widget's coordinates, or
    /// nullptr if none. Iterates children in reverse z-order (topmost
    /// first); a child that does not contain the point is skipped.
    std::shared_ptr<CtWidgetBase> childAt(int px, int py) const;

    /// The widget's rect in parent coordinates (int rect, not QRectF).
    CtRect geometry() const noexcept { return m_geometry; }

    /// Set the widget's geometry; negative width/height are normalized to
    /// zero (§3.4 normalization contract).
    void setGeometry(const CtRect& rect);

    /// Widget-local size.
    int width() const noexcept { return m_geometry.width; }
    int height() const noexcept { return m_geometry.height; }

    // --- state ----------------------------------------------------------

    /// Show/hide the widget (UI1: flag only; layout/paint effects are
    /// UI4/UI2 consumers). Thread rule: must run on the widget's thread
    /// (debug abort / release no-op, §4.2).
    void setVisible(bool visible) noexcept;
    /// Whether the widget participates in layout + painting.
    bool isVisible() const noexcept { return m_state.visible; }

    /// Enable/disable the widget. Disabled widgets reject input
    /// (§3.6 dispatchInput gating). Thread rule: see setVisible.
    void setEnabled(bool enabled) noexcept;
    bool isEnabled() const noexcept { return m_state.enabled; }

    /// UI1: inert focus flags (UI4 consumes); defined for API stability.
    bool focusable() const noexcept { return m_state.focusable; }
    bool hasFocus() const noexcept { return m_state.hasFocus; }

    /// The explicit state struct (read-only view for UI4+).
    const CtWidgetState& widgetState() const noexcept { return m_state; }

    // --- input ----------------------------------------------------------

    /// Dispatch one mouse event to this widget's input handlers.
    /// Disabled widgets reject input (kUnhandled, no handler invocation).
    /// The payload coordinates are widget-local (crisp::input contract).
    /// UI1: no hit-testing beyond the widget itself; the UI3 root
    /// dispatcher walks the tree via childAt()/hitTest().
    /// Thread rule: must run on the widget's thread (§4.2).
    void dispatchInput(crisp::event::CtEvent<crisp::input::CtMousePayload>& event);

    /// Dispatch one key event to this widget's input handlers.
    /// Disabled widgets reject input. UI1: delivered to the widget itself
    /// (focus-chain routing is UI4). Thread rule: see dispatchInput(mouse).
    void dispatchInput(crisp::event::CtEvent<crisp::input::CtKeyPayload>& event);

    /// True if `px`/`py` (widget-local) is inside this widget's bounds.
    bool hitTest(int px, int py) const noexcept {
        return m_geometry.contains(px, py);
    }

    // --- damage (P4 / D2) -----------------------------------------------

    /// Mark `rect` (widget-local) as damaged. Damage accumulates into the
    /// widget's dirty region; ONE repaint pass per frame serves ALL
    /// accumulated damage (QWidgetRepaintManager::markDirty model).
    /// Hot path: O(1), zero heap allocation on the union itself; the
    /// kDamage trace record takes one D3-approved mutex lock + clock read
    /// (docs/ui/04-ui1-architecture.md §2.7 — the lock is NOT the hot path).
    /// Produces a kDamage trace record (with the accumulated count).
    void markDamage(const CtRect& rect) noexcept;

    /// The accumulated dirty region (widget-local, unioned). Consumed by
    /// the backing store at flush time (UI2).
    const CtRegion& dirtyRegion() const noexcept { return m_dirty; }

    /// Damage has been flushed by the backing store; clears the region and
    /// resets the damage count.
    void clearDamage() noexcept;

    // --- observability (P6 / D3) -----------------------------------------

    /// The widget-side trace ring (created at construction).
    CtWidgetTrace& trace() noexcept { return m_trace; }

    /// This widget's stable object id (inherited from CtReceiver).
    std::uint64_t objectId() const noexcept;

    /// The thread this widget is affined to (inherited from CtReceiver).
    crisp::event::CtThreadId affinityThread() const noexcept;

private:
    /// Record a widget trace entry for this widget (thread-safe ring).
    /// `damageRectCount` is only meaningful for kDamage records.
    void recordTrace(CtWidgetPhase phase, std::uint32_t damageRectCount = 0);

private:

    /// Attach this widget as a child of `newParent` (tree surgery; callers
    /// hold the thread assertion and the parent's liveness). The child
    /// entry is built from m_selfWeak (set by the factories) — deliberately
    /// NOT from shared_from_this(): the widget's ONLY enable_shared_from_this
    /// base is CtReceiver, whose shared_from_this() would lock() the strong
    /// reference held by the CALLER (fine) but the weak entry must NOT keep
    /// a strong reference — m_selfWeak is the pure-weak self handle. Callers
    /// must hold a live strong reference (factory context).
    void attachTo(const std::shared_ptr<CtWidgetBase>& newParent);

    /// Detach from the current parent (tree surgery).
    void detachFromParent();

    std::weak_ptr<CtWidgetBase> m_parent; ///< weak back-reference (no strong cycle)
    std::weak_ptr<CtWidgetBase> m_selfWeak; ///< weak SELF-reference, set by the
                                            ///< factories BEFORE any tree
                                            ///< operation (P5 sanctioned entry)
    std::vector<std::weak_ptr<CtWidgetBase>> m_children; ///< z-order, back = topmost
    CtRect m_geometry;            ///< parent-local geometry
    CtWidgetState m_state;        ///< explicit state (P12)
    CtRegion m_dirty;             ///< accumulated damage (D2)
    std::uint32_t m_damageRectCount = 0; ///< markDamage calls since clearDamage
    CtWidgetTrace m_trace;        ///< widget-side trace ring (D3)
};

} // namespace crisp::ui
