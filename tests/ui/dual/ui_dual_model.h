#pragma once

// ui_dual_model.h — shared widget-tree dual-run model (UI3 O4 ruling:
// the "控件树级完整双跑" — same widget tree, two mapper modes, identical
// semantic event sequences, text/timestamp excluded per MUST-3/D12/WARN-2).
//
// Part of the CRISP project. The model owns the canonical widget tree and
// wires its SEMANTIC events (click/text-changed/focus) into a
// deterministic log. Each backend (win32 / qt-host) maps the SAME
// semantic input list into core crisp::input payloads via its own mapper,
// then calls drivePayloads() — the tree's dispatchInput consumes the
// payloads identically, so the two logs must be byte-identical (mirrors
// M3 §6.2 dual-run; UI3 §17 O4).
//
// Authoritative spec: docs/ui/06-ui3-architecture.md §17;
// docs/ui/08-ui5-architecture.md §5.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ui/ct_widget_base.h"
#include "ui/ct_push_button.h"
#include "ui/ct_line_edit.h"
#include "ui/ct_label.h"
#include "ui/ct_focus_chain.h"
#include "event/ct_click_payload.h"
#include "event/ct_focus_payload.h"
#include "event/ct_handler_token.h" // CtHandlerToken (RAII, kept alive)
#include "event/ct_key_payload.h"
#include "event/ct_mouse_payload.h"
#include "event/ct_text_changed_payload.h"

namespace crisp::ui::dual {

/// The core payloads a backend can feed (mouse / key) — the input stream
/// that BOTH mapper modes must produce from the same semantic input.
using CtDualInput =
    std::variant<crisp::input::CtMousePayload, crisp::input::CtKeyPayload>;

/// A widget tree + its semantic-event recorder (the SHARED dual-run model).
/// Deterministic log lines:
///   C x y          button click at widget-local (x,y)
///   E <len>        line-edit text-changed (event KIND + text LENGTH —
///                   text BYTES excluded per MUST-3/D12/WARN-2)
///   F <objectId>   focus entered a widget (its objectId)
/// text/timestamp are mode-agnostic in this log (MUST-3/D12/WARN-2: the
/// raw payload's text bytes differ between modes by design — the SEMANTIC
/// event's KIND + length are compared, which both modes produce
/// identically).
struct CtUiDualModel {
    /// Build the canonical widget tree (label + button + lineedit + focus
    /// chain) and wire the semantic handlers into `log()`. Geometry is
    /// explicit (UI4: layout is owner-driven) so hit-testing is
    /// deterministic. Returns the root (the caller keeps it alive).
    std::shared_ptr<CtWidgetBase> buildTree();

    /// The root widget (built by buildTree; model-owned).
    std::shared_ptr<CtWidgetBase> root() const { return m_rootStrong; }
    /// The button (model-owned).
    std::shared_ptr<CtPushButton> button() const { return m_buttonStrong; }
    /// The line edit (model-owned).
    std::shared_ptr<CtLineEdit> edit() const { return m_editStrong; }
    /// The focus chain (owned by the model — the tree does not hold it).
    std::shared_ptr<CtFocusChain> focusChain() const { return m_focusChain; }

    /// Button click recorded (widget-local press coords).
    void onButtonClick(int x, int y) {
        mLog.push_back("C " + std::to_string(x) + " " + std::to_string(y));
    }

    /// Line-edit text changed — recorded as the EVENT KIND + the text
    /// LENGTH (MUST-3/D12/WARN-2: the raw text BYTES differ between modes
    /// by design — the Win32 mapper leaves `text` empty per MUST-3; the
    /// Qt-host mapper carries it. The SEMANTIC event fires in both, with
    /// the same length for the same key sequence, so the length is the
    /// mode-agnostic comparable).
    void onTextChanged(const std::u16string& text) {
        mLog.push_back("E " + std::to_string(text.size()));
    }

    /// Focus entered a widget (its objectId — mode-agnostic).
    void onFocusGained(std::uint64_t objectId) {
        mLog.push_back("F " + std::to_string(objectId));
    }

    /// Deterministic log (stable order). Const access (WARN-5: the log is
    /// model-owned; callers must not mutate it).
    const std::vector<std::string>& log() const { return mLog; }

private:
    // The model OWNS the widgets it needs beyond the tree (the tree
    // stores weak refs only — the P5 contract: strong ownership lives
    // outside the tree). root/button/edit are held strongly; the LABEL
    // is not (it participates in no hit-testing and dies with the tree
    // scope — P3 note: the ownership comment covers the held widgets).
    std::shared_ptr<CtWidgetBase> m_rootStrong;
    std::shared_ptr<CtPushButton> m_buttonStrong;
    std::shared_ptr<CtLineEdit> m_editStrong;
    std::shared_ptr<CtFocusChain> m_focusChain;
    /// The semantic-handler tokens (RAII, §7.7): KEPT alive so the
    /// handlers stay registered for the model's lifetime.
    std::vector<crisp::event::CtHandlerToken> m_tokens;
    std::vector<std::string> mLog;
};

/// Drive the payload stream into the tree (shared by both backends):
/// mouse payloads → dispatched to the widget at the payload's local
/// coordinates (the root dispatches via childAt, so the button/edit
/// consume what their hit-test says); key payloads → the FOCUSED widget
/// consumes (the focus chain routes); a kTab key steps the focus chain.
/// Deterministic — both modes produce the same semantic events.
void drivePayloads(CtUiDualModel& model,
                   const std::vector<CtDualInput>& payloads);

} // namespace crisp::ui::dual
