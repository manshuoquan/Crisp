#pragma once

// ct_focus_chain.h — UI4 explicit focus chain (crisp::ui).
// Replaces QApplicationPrivate::focus_widget global mutable state +
// focusNextPrevChild_helper tree walk (docs/ui/03-rewrite-proposal.md
// §4.4.4). Explicit linked-list storage (open question #3 ruling:
// next/prev O(1) stepping); focus moves emit CtEvent<CtFocusPayload> to
// the receiving widget and write kFocusIn/kFocusOut widget-trace records.
//
// Part of the CRISP project. Pure C++17, zero Qt/Win32 dependency.
// The chain holds weak references (the tree owns the strong refs); a
// destroyed widget is skipped on step.

#include <cstdint>
#include <memory>
#include <vector>

#include "event/ct_focus_payload.h" // crisp::input::CtFocusDirection
#include "event/ct_thread_affinity.h" // crisp::event::CtThreadAffinity / CtThreadId

namespace crisp::ui {

class CtWidgetBase; ///< fwd: chain items hold weak_ptr<CtWidgetBase>

/// (UI4) Explicit focus chain — replaces QApplicationPrivate::focus_widget
/// global mutable state + focusNextPrevChild_helper tree walk
/// (docs/ui/01-qt-gui-architecture.md §2.4).
///
/// THREAD CONTRACT (UI4): the chain is affined to its creating thread
/// (embedded CtThreadAffinity). add/setFocus/next assert the calling
/// thread owns the chain (debug abort; release degrades to a no-op) — the
/// chain's mutable focus state and the target widgets' focus flags must
/// only be touched on the UI thread.
class CtFocusChain {
public:
    /// Create a chain affined to the current thread.
    CtFocusChain() noexcept = default;

    /// Non-copyable (holds thread affinity + focus state).
    CtFocusChain(const CtFocusChain&) = delete;
    CtFocusChain& operator=(const CtFocusChain&) = delete;

    /// Register a focusable widget into the chain (z/insertion order).
    /// The chain stores a weak_ptr; a dead widget is skipped on step.
    /// Thread rule: must run on the chain's thread.
    void add(const std::shared_ptr<CtWidgetBase>& widget);

    /// Move focus to `widget`; emits typed CtEvent<CtFocusPayload> to the
    /// widget's handlers and writes kFocusOut (old) / kFocusIn (new)
    /// widget-trace records. No-op when the widget is not in the chain or
    /// already focused. Thread rule: must run on the chain's thread.
    void setFocus(const std::shared_ptr<CtWidgetBase>& widget);

    /// Tab/Backtab step; returns the widget that received focus (or empty
    /// when the chain is empty / no focusable widget remains). Wraps
    /// around at the ends. Thread rule: must run on the chain's thread.
    std::shared_ptr<CtWidgetBase> next(crisp::input::CtFocusDirection dir) noexcept;

    /// The current focus owner (explicit data, observable via CtTrace
    /// kFocusIn/kFocusOut phases — pain point P6). Empty weak_ptr when
    /// nothing has focus.
    std::weak_ptr<CtWidgetBase> current() const noexcept {
        if (!m_hasCurrent || m_current >= m_widgets.size())
            return {};
        return m_widgets[m_current];
    }

    /// Number of registered widgets (dead refs pruned lazily on step).
    std::size_t size() const noexcept { return m_widgets.size(); }

private:
    /// Assert the calling thread owns this chain (debug abort; release
    /// returns false so callers degrade to a no-op).
    bool assertSameThread() const noexcept {
        return m_affinity.assertSameThread();
    }

    /// Clear the current focus owner (kFocusOut on it + clear hasFocus).
    void clearCurrent();

    /// Remove expired weak refs (a destroyed widget must not block the
    /// chain); adjusts m_current.
    void pruneDead();

    /// The chain (insertion order). Explicit linear storage — the UI4
    /// ruling prefers the explicit chain over a tree walk (O(n) register,
    /// O(1) step after an index lookup).
    std::vector<std::weak_ptr<CtWidgetBase>> m_widgets;
    /// Index of the current focus owner (valid only when m_hasCurrent).
    std::size_t m_current = 0;
    /// True when a widget currently owns focus (distinguishes "index 0 is
    /// focused" from "nothing focused").
    bool m_hasCurrent = false;
    /// Embedded thread affinity (the chain is UI-thread-affined).
    crisp::event::CtThreadAffinity m_affinity;
};

} // namespace crisp::ui
