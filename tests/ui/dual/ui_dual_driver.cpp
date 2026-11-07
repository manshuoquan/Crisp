// ui_dual_driver.cpp — shared widget-tree dual-run driver (tests/ui/dual).
// Builds the canonical tree, wires the semantic handlers (click /
// text-changed / focus) into the CtUiDualModel, and feeds a FIXED
// semantic input stream (core crisp::input payloads) into the tree via
// CtWidgetBase::dispatchInput. The same tree + same stream run in BOTH
// mapper modes (win32 / qt-host) — the two logs must be byte-identical
// (UI3 O4: 控件树级完整双跑).
//
// Part of the CRISP project. Authoritative spec:
// docs/ui/06-ui3-architecture.md §17; docs/ui/08-ui5-architecture.md §5.

#include "ui_dual_model.h"

#include <cassert>   // assert (explicit include, P3)
#include <variant>

#include "event/ct_event.h"      // CtEvent / CtEventOrigin

namespace crisp::ui::dual {

std::shared_ptr<CtWidgetBase> CtUiDualModel::buildTree() {
    auto root = CtWidgetBase::create();
    root->setGeometry(CtRect{0, 0, 300, 120});

    auto label = CtLabel::create(root, u"Dual");
    label->setGeometry(CtRect{10, 10, 80, 20});

    auto button = CtPushButton::create(root, u"Go");
    button->setGeometry(CtRect{10, 40, 80, 30});
    button->setFocusable(true); // participates in the focus chain

    auto edit = CtLineEdit::create(root, u"");
    edit->setGeometry(CtRect{110, 40, 120, 24});
    edit->setFocusable(true); // participates in the focus chain

    auto chain = std::make_shared<CtFocusChain>();
    chain->add(button);
    chain->add(edit);

    // Wire the semantic events into the model (deterministic order).
    // IMPORTANT (RAII, §7.7): the returned CtHandlerTokens must be KEPT
    // ALIVE — their destructor UNREGISTERS the handler. Stored in the
    // model (m_tokens) for its lifetime.
    m_tokens.push_back(
        button->onClick([this](crisp::event::CtEvent<crisp::input::CtClickPayload>& ev) {
            onButtonClick(ev.payload().x, ev.payload().y);
            ev.accept();
        }));
    m_tokens.push_back(
        edit->onTextChanged(
            [this](crisp::event::CtEvent<crisp::input::CtTextChangedPayload>& ev) {
                onTextChanged(ev.payload().text);
                ev.accept();
            }));
    m_tokens.push_back(
        edit->bindFocusHandler(
            [this](crisp::event::CtEvent<crisp::input::CtFocusPayload>& ev) {
                onFocusGained(ev.payload().targetObjectId);
                ev.accept();
            }));
    m_tokens.push_back(
        button->bindFocusHandler(
            [this](crisp::event::CtEvent<crisp::input::CtFocusPayload>& ev) {
                onFocusGained(ev.payload().targetObjectId);
                ev.accept();
            }));

    m_rootStrong = root;
    m_buttonStrong = button;
    m_editStrong = edit;
    m_focusChain = chain;
    return root;
}

void drivePayloads(CtUiDualModel& model,
                   const std::vector<CtDualInput>& payloads) {
    for (const auto& input : payloads) {
        std::visit(
            [&](const auto& payload) {
                using T = std::decay_t<decltype(payload)>;
                // Mouse: dispatch to the widget at the LOCAL coords (the
                // root's childAt does the hit-test; the button/edit
                // consume what they own).
                if constexpr (std::is_same_v<T, crisp::input::CtMousePayload>) {
                    auto root = model.root();
                    if (!root)
                        return;
                    // UI3 root dispatch: hit-test the ROOT-LOCAL coords
                    // against the tree (childAt) and deliver to the owning
                    // widget, TRANSLATED to the target's local coords
                    // (payloads are widget-local, crisp::input contract).
                    // WARN-7: a disabled target consumes NOTHING (the
                    // dispatchInput gate would ignore) — explicit here so
                    // the disabled-subtree semantics are testable.
                    auto target = root->childAt(payload.x, payload.y);
                    if (!target || !target->isEnabled())
                        return; // no live widget owns the point
                    auto translated = payload;
                    const auto g = target->geometry();
                    translated.x = payload.x - g.x;
                    translated.y = payload.y - g.y;
                    crisp::event::CtEvent<crisp::input::CtMousePayload> ev(
                        crisp::event::CtEventOrigin::kSystem, translated);
                    target->dispatchInput(ev);
                } else {
                    // Key: only PRESSES route (WARN-8: a kRelease must not
                    // step the focus chain — the Win32/Qt sources deliver
                    // presses to the tree; releases are the widgets' own
                    // gate in onKeyInput).
                    if (payload.action != crisp::input::CtKeyAction::kPress)
                        return;
                    // The FOCUSED widget consumes it (the chain routes);
                    // kTab steps the chain. WARN-4/6 fix: the chain MUST
                    // have a current focusable owner (the tree setup calls
                    // setFocus(edit) first) — a null current means a chain
                    // misconfiguration, assert loudly.
                    auto edit = model.edit();
                    auto chain = model.focusChain();
                    if (!edit || !chain)
                        return;
                    if (payload.key == crisp::input::CtKeyButton::kTab) {
                        chain->next(crisp::input::CtFocusDirection::kNext);
                        return;
                    }
                    auto focused = chain->current().lock();
                    assert(focused && "dual-run: focus chain has no current "
                                      "owner (tree setup misconfiguration)");
                    if (!focused)
                        focused = edit; // release fallback (defensive)
                    crisp::event::CtEvent<crisp::input::CtKeyPayload> ev(
                        crisp::event::CtEventOrigin::kSystem, payload);
                    focused->dispatchInput(ev);
                }
            },
            input);
    }
}

} // namespace crisp::ui::dual
