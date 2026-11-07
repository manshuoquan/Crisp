// ct_focus_chain.cpp — UI4 explicit focus chain implementation (crisp::ui).
//
// Explicit linear storage (docs/ui/03-rewrite-proposal.md §4.4.4 open
// question #3 ruling: next/prev O(1) stepping after an index lookup).
// Focus moves emit CtEvent<CtFocusPayload> to the receiving widget
// (via CtWidgetBase::emitSemantic) and write kFocusIn/kFocusOut
// widget-trace records.

#include "ct_focus_chain.h"

#include <algorithm>
#include <utility>

#include "ct_widget_base.h" // CtWidgetBase (focus flags + emitSemantic)

namespace crisp::ui {

void CtFocusChain::add(const std::shared_ptr<CtWidgetBase>& widget) {
    if (!assertSameThread())
        return; // release: degrade to no-op
    if (!widget)
        return;
    // Dedup by owner identity (same discipline as the tree's child list).
    for (const auto& w : m_widgets) {
        if (!w.owner_before(widget) && !widget.owner_before(w)) {
            return; // already registered
        }
    }
    m_widgets.emplace_back(widget);
}

void CtFocusChain::setFocus(const std::shared_ptr<CtWidgetBase>& widget) {
    if (!assertSameThread())
        return; // release: degrade to no-op
    if (!widget)
        return;
    // P1-5 fix: a chain may only focus FOCUSABLE widgets (the step path
    // filters focusable(); setFocus must apply the same rule).
    if (!widget->focusable())
        return;
    // The widget must be registered (a chain can only focus its members).
    std::size_t index = m_widgets.size();
    for (std::size_t i = 0; i < m_widgets.size(); ++i) {
        if (!m_widgets[i].owner_before(widget) &&
            !widget.owner_before(m_widgets[i])) {
            index = i;
            break;
        }
    }
    if (index == m_widgets.size())
        return; // not in the chain: no-op

    // Same-owner no-op (already focused).
    if (m_hasCurrent && m_current < m_widgets.size() &&
        !m_widgets[m_current].owner_before(widget) &&
        !widget.owner_before(m_widgets[m_current]))
        return;

    // Emit kFocusOut on the old owner (if any) + clear its hasFocus.
    clearCurrent();

    // Focus the new owner: set the flag, emit the semantic event, write
    // the kFocusIn trace record.
    m_current = index;
    m_hasCurrent = true;
    widget->setFocusFlag(true); // CtWidgetBase::setFocusFlag (UI4)
    widget->emitFocusGained(crisp::input::CtFocusDirection::kNext);
}

std::shared_ptr<CtWidgetBase> CtFocusChain::next(
    crisp::input::CtFocusDirection dir) noexcept {
    if (!assertSameThread())
        return nullptr; // release: degrade to no-op
    if (m_widgets.empty())
        return nullptr;

    // Prune dead refs lazily (a destroyed widget must not block the
    // chain; the surviving widgets keep their relative order).
    pruneDead();

    if (m_widgets.empty())
        return nullptr;

    const std::size_t n = m_widgets.size();
    // Step from the CURRENT owner (or from the start/end when nothing has
    // focus). Wraps around at the ends.
    const std::size_t start =
        m_hasCurrent ? m_current
                     : (dir == crisp::input::CtFocusDirection::kNext ? n - 1u : 0u);
    std::size_t step = dir == crisp::input::CtFocusDirection::kNext ? 1u : n - 1u;
    std::size_t idx = start;
    for (std::size_t k = 0; k < n; ++k) {
        idx = (idx + step) % n;
        auto w = m_widgets[idx].lock();
        if (w && w->focusable()) {
            setFocus(w);
            return w;
        }
    }
    return nullptr; // no focusable widget remains
}

void CtFocusChain::clearCurrent() {
    if (!m_hasCurrent || m_current >= m_widgets.size()) {
        m_hasCurrent = false;
        return;
    }
    auto old = m_widgets[m_current].lock();
    if (old) {
        old->setFocusFlag(false);
        old->emitFocusLost();
    }
    m_hasCurrent = false;
}

void CtFocusChain::pruneDead() {
    // P1-1/P2-7 fix: prune dead refs AND keep m_current valid. Removing an
    // item BEFORE the current index shifts the current widget's position;
    // removing the current item itself must drop focus. Re-locate the
    // current widget by owner identity after the prune.
    auto currentWidget = m_hasCurrent ? m_widgets[m_current].lock() : nullptr;

    m_widgets.erase(
        std::remove_if(m_widgets.begin(), m_widgets.end(),
                       [](const std::weak_ptr<CtWidgetBase>& w) {
                           return w.expired();
                       }),
        m_widgets.end());

    if (!currentWidget) {
        // The focus owner was removed (or nothing had focus): drop focus.
        m_hasCurrent = false;
        m_current = 0;
        return;
    }
    // Re-locate the surviving focus owner by owner identity.
    std::size_t found = m_widgets.size();
    for (std::size_t i = 0; i < m_widgets.size(); ++i) {
        if (!m_widgets[i].owner_before(currentWidget) &&
            !currentWidget.owner_before(m_widgets[i])) {
            found = i;
            break;
        }
    }
    m_hasCurrent = (found < m_widgets.size());
    m_current = m_hasCurrent ? found : 0;
}

} // namespace crisp::ui
