#pragma once

// ct_widget_base.h — crisp::ui core widget base (UI1).
//
// The root of the crisp::ui object tree: shared_ptr ownership + weak_ptr
// child references + RAII teardown (P5), explicit state struct (P12),
// typed input dispatch (P1), single-rect dirty region (P4, D2) and widget
// trace observability (P6, D3).
//
// Authoritative spec: docs/ui/04-ui1-architecture.md §1.1 + §2 + §3
// (§3 fully finalized; this header implements the §3 surface verbatim).
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
#include "event/ct_click_payload.h"     // crisp::input::CtClickPayload (UI4)
#include "event/ct_focus_payload.h"     // crisp::input::CtFocusPayload (UI4)
#include "event/ct_receiver.h"      // crisp::event::CtReceiver (base class)
#include "event/ct_text_changed_payload.h" // crisp::input::CtTextChangedPayload (UI4)
#include "event/ct_thread_affinity.h" // crisp::event::CtThreadId
#include "ct_dispatcher_registry.h" // CtSemanticEventSet (UI4)
#include "ct_geometry.h"            // CtRect / CtRegion
#include "ct_paint_backend.h"       // CtPaintBackend (sizeHint signature, UI4)
#include "ct_paint_types.h"         // CtSize (sizeHint signature, UI4)
#include "ct_widget_state.h"        // CtWidgetState / CtFocusPolicy
#include "ct_widget_trace.h"        // CtWidgetPhase / CtWidgetRecord / CtWidgetTrace

namespace crisp::ui {
class CtStyle; ///< fwd: widgets hold a style pointer (UI5); full definition
               ///< lives in ct_style.h (which includes this header — the
               ///< pointer-only member keeps the include graph acyclic).
/// fwd: the default style (defined in ct_style_basic.h); effectiveStyle()
/// uses it when the widget has no custom style.
const CtStyle& defaultStyle() noexcept;

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

    /// (UI4) Set the focusable flag (participates in the focus chain).
    /// The chain steps only focusable widgets; the focus policy (click vs
    /// tab) is UI4's CtFocusChain concern.
    void setFocusable(bool focusable) noexcept;

    /// The explicit state struct (read-only view for UI4+).
    const CtWidgetState& widgetState() const noexcept { return m_state; }

    // --- input ----------------------------------------------------------

    /// Dispatch one mouse event to this widget's input handlers.
    /// Disabled widgets reject input (kUnhandled, no handler invocation).
    /// The payload coordinates are widget-local (crisp::input contract).
    /// UI4: after the gate, the payload is handed to the virtual
    /// onMouseInput() — the widget's own consumption (the button state
    /// machine / line-edit cursor live there); the input event itself is
    /// NOT accepted (the app's raw-input handlers consume raw input via
    /// the UI3 event-set queue; the semantic events are the widget-level
    /// product of the virtual hook).
    /// Thread rule: must run on the widget's thread (§4.2).
    void dispatchInput(crisp::event::CtEvent<crisp::input::CtMousePayload>& event);

    /// Dispatch one key event to this widget's input handlers.
    /// Disabled widgets reject input. UI1: delivered to the widget itself
    /// (focus-chain routing is UI4). Thread rule: see dispatchInput(mouse).
    void dispatchInput(crisp::event::CtEvent<crisp::input::CtKeyPayload>& event);

    // --- UI4 typed handler registration (docs/ui/03-rewrite-proposal.md §4.4)
    //
    // Bind a handler to one of the UI4 semantic payload types. The binding
    // is a THIN convenience over CtReceiver::bindHandler: the default
    // dispatcher is looked up per call (CtDispatcher registry, one per
    // affinity thread) and the widget's weak_ptr is passed as the
    // receiver. The returned RAII token unregisters on destruction.
    //
    // Handlers on a given widget see events from the WIDGET'S OWN semantic
    // emission only (emitSemantic) — they do NOT see raw events routed to
    // other widgets, and the widget layer deliberately never registers a
    // broadcast handler.

    /// Bind a click handler (CtClickPayload).
    crisp::event::CtHandlerToken bindClickHandler(
        crisp::event::CtHandler<crisp::input::CtClickPayload> handler,
        std::string_view name = {}) {
        return bindSemantic<crisp::input::CtClickPayload>(std::move(handler),
                                                          name);
    }

    /// Bind a text-changed handler (CtTextChangedPayload).
    crisp::event::CtHandlerToken bindTextChangedHandler(
        crisp::event::CtHandler<crisp::input::CtTextChangedPayload> handler,
        std::string_view name = {}) {
        return bindSemantic<crisp::input::CtTextChangedPayload>(
            std::move(handler), name);
    }

    /// Bind a focus handler (CtFocusPayload).
    crisp::event::CtHandlerToken bindFocusHandler(
        crisp::event::CtHandler<crisp::input::CtFocusPayload> handler,
        std::string_view name = {}) {
        return bindSemantic<crisp::input::CtFocusPayload>(std::move(handler),
                                                          name);
    }

    /// True if `px`/`py` (widget-local) is inside this widget's bounds.
    bool hitTest(int px, int py) const noexcept {
        return m_geometry.contains(px, py);
    }

    /// (UI4) True if a WIDGET-LOCAL point is inside the widget's LOCAL
    /// extent. UI1's hitTest is PARENT-local (m_geometry carries the
    /// parent offset); the input hooks consume WIDGET-LOCAL payloads
    /// (crisp::input contract), so they must test the LOCAL extent
    /// (0,0,width,height) — dual-run O4 fix.
    bool localHitTest(int px, int py) const noexcept {
        return px >= 0 && py >= 0 && px < m_geometry.width &&
               py < m_geometry.height;
    }

    /// (UI4) True if the key action is a press (not a release).
    static bool isKeyPress(const crisp::input::CtKeyPayload& p) noexcept {
        return p.action == crisp::input::CtKeyAction::kPress;
    }

    /// (UI4) True if the mouse action is a press (not a move/release/dblclick).
    static bool isMousePress(const crisp::input::CtMousePayload& p) noexcept {
        return p.action == crisp::input::CtMouseAction::kPress;
    }

    /// (UI4) Set the focus flag (hasFocus) and record the widget trace
    /// (kFocusIn with the focus direction). Called by CtFocusChain only —
    /// the chain is the SINGLE focus owner.
    void setFocusFlag(bool focused) noexcept;

    /// (UI4) Emit a CtFocusPayload (gained) to this widget's handlers —
    /// called by CtFocusChain when focus enters. The direction is the
    /// chain's step direction.
    void emitFocusGained(crisp::input::CtFocusDirection direction);

    /// (UI4) Emit a CtFocusPayload (lost) to this widget's handlers —
    /// called by CtFocusChain when focus leaves.
    void emitFocusLost();

    /// (UI4) Preferred size from the backend's font metrics (one measure
    /// pass). Default: zero (plain widgets have no intrinsic size; the
    /// layout gives them the container's size). Text widgets override
    /// (CtLabel uses backend metrics; CtPushButton/CtLineEdit use the
    /// backend's fixed text metrics). `backend` must be non-null.
    /// noexcept (WARN-3 fix): a throwing backend fontMetrics degrades to a
    /// zero hint — never crosses the layout's noexcept relayout boundary.
    virtual CtSize sizeHint(CtPaintBackend& backend) const noexcept {
        (void)backend;
        return CtSize{};
    }

    // --- UI5 style -------------------------------------------------------

    /// The widget's style (nullptr = the default CtBasicStyle is used by
    /// the widget's paint/sizeHint — see setStyle).
    const CtStyle* style() const noexcept { return m_style; }

    /// The EFFECTIVE style (the widget's, or the default when none is
    /// set — SUGGEST-1 ruling: one shared resolution for paint/sizeHint).
    const CtStyle& effectiveStyle() const noexcept {
        return m_style ? *m_style : defaultStyle();
    }

    /// Set the widget's style (the widget does NOT own it — the caller
    /// MUST keep the style alive for at least as long as the widget; a
    /// null style reverts to the default CtBasicStyle, which is a
    /// program-lifetime static). Thread rule: must run on the widget's
    /// thread.
    void setStyle(const CtStyle* style) noexcept;

protected:
    // --- UI4 widget input hooks ------------------------------------------
    //
    // The widget's OWN consumption of the input payloads (after the
    // disabled gate). Default: no-op — plain widgets do not consume input.
    // Interactive widgets (CtPushButton/CtLineEdit) override these to run
    // their explicit state machines; they emit SEMANTIC events (click /
    // text-changed / focus) via emitSemantic<T>. Called on the widget's
    // thread only.

    /// Mouse input hook (UI4): the payload of the dispatched mouse event.
    /// Button state machine / hover / press-and-release live here.
    virtual void onMouseInput(const crisp::input::CtMousePayload& payload);

    /// Key input hook (UI4): the payload of the dispatched key event.
    /// Line-edit text editing / focus stepping live here.
    virtual void onKeyInput(const crisp::input::CtKeyPayload& payload);

    /// (UI4) Emit a semantic event to THIS widget's handlers of type T.
    /// Synchronous (CtDispatcher::send on the UI thread); the payload is
    /// forwarded by value. Requires T ∈ CtSemanticEventSet.
    template <typename T>
    void emitSemantic(const T& payload) {
        auto dispatcher = semanticDispatcher();
        if (!dispatcher)
            return;
        static_assert(CtSemanticEventSet::template contains<T>,
                      "emitSemantic requires a semantic payload type");
        crisp::event::CtEvent<T> ev(crisp::event::CtEventOrigin::kApplication,
                                    payload);
        dispatcher->send(shared_from_this(), ev);
    }

public:
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

    /// (UI4) The default semantic-event dispatcher for this widget's
    /// affinity thread (cached on first use — WARN-4 fix: the registry
    /// lookup + lock is paid once per widget, not per semantic event).
    /// The registry creates one dispatcher per thread and never closes it,
    /// so the cached shared_ptr stays valid for the widget's lifetime.
    std::shared_ptr<crisp::event::CtDispatcher<CtSemanticEventSet>>
    semanticDispatcher() const {
        if (!m_semanticDispatcher) {
            m_semanticDispatcher = semanticDispatcherForCurrentThread();
        }
        return m_semanticDispatcher;
    }

    /// (UI4) Shared implementation of the bind*Handler templates: looks up
    /// the semantic dispatcher and delegates to CtReceiver::bindHandler.
    /// Thread rule (P2-1 fix): binding registers a handler on THIS widget —
    /// a mutation of the widget's dispatch state, so the calling thread
    /// must own the widget (debug abort; release returns a null token).
    template <typename T>
    crisp::event::CtHandlerToken bindSemantic(crisp::event::CtHandler<T> handler,
                                              std::string_view name) {
        if (!assertSameThread())
            return {}; // release: degrade (null token, nothing bound)
        auto dispatcher = semanticDispatcher();
        if (!dispatcher)
            return {}; // no loop thread: nothing to bind to (null token)
        return bindHandler<CtSemanticEventSet, T>(dispatcher, std::move(handler),
                                                  name);
    }

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
    /// (UI4) Cached semantic dispatcher (mutable: filled on first use by
    /// the const semanticDispatcher(); the registry never closes it).
    mutable std::shared_ptr<crisp::event::CtDispatcher<CtSemanticEventSet>>
        m_semanticDispatcher;
    /// (UI5) The widget's style (NOT owned — the caller keeps it alive;
    /// null = default CtBasicStyle).
    const CtStyle* m_style = nullptr;
};

} // namespace crisp::ui
