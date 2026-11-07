#pragma once

// ct_line_edit.h — UI4 single-line text editor (crisp::ui).
// Explicit states + a small undo-free text buffer; cursor blink driven by
// CtTimerEventSource (UI3 loop). docs/ui/03-rewrite-proposal.md §4.4.2.
//
// Part of the CRISP project. Pure C++17, zero Qt/Win32 dependency. The
// editing logic is pure (unit-testable without a platform): key input
// arrives via CtWidgetBase::dispatchInput -> onKeyInput, text changes are
// emitted as CtTextChangedPayload semantic events.

#include <cstddef>
#include <memory>
#include <string>

#include "ct_widget_base.h"

namespace crisp::ui {
class CtPainter; ///< fwd: paint() takes the owner-provided painter (UI5)

/// (UI4) Single-line text editor. Explicit states + a small undo-free text
/// buffer; cursor blink driven by CtTimerEventSource (UI3 loop).
class CtLineEdit : public CtWidgetBase {
public:
    /// Sanctioned factory (P5): create an editor attached to `parent`.
    static std::shared_ptr<CtLineEdit> create(
        const std::shared_ptr<CtWidgetBase>& parent,
        std::u16string initial = {});

    /// Constructor used by the factory (P5: ctor must not touch the tree).
    explicit CtLineEdit(const std::shared_ptr<CtWidgetBase>& parent,
                        std::u16string initial);

    /// The current text (UTF-16; empty = no text).
    std::u16string_view text() const noexcept;

    /// Set the text; emits CtTextChangedPayload (semantic event) when the
    /// text actually changes. Triggers markDamage + relayout.
    void setText(std::u16string text);

    /// Semantic event on text change (typed payload, no signal/slot).
    crisp::event::CtHandlerToken onTextChanged(
        crisp::event::CtHandler<crisp::input::CtTextChangedPayload> handler,
        std::string_view name = {}) {
        return bindTextChangedHandler(std::move(handler), name);
    }

    /// Cursor caret position (0..text length).
    std::size_t cursorPos() const noexcept;

    /// Set the caret position (clamped to 0..text length).
    void setCursorPos(std::size_t pos) noexcept;

    /// (UI4) Preferred size: the backend's font metrics for the current
    /// text plus the default editor padding (fixed in UI4; UI5 wires the
    /// style's sizeHint contribution).
    CtSize sizeHint(CtPaintBackend& backend) const noexcept override;

    /// Paint the editor into `painter` within `rect` (widget-local).
    /// UI5 paints the style palette (background + border + text + caret
    /// when focused). The painter is PROVIDED by the owner (backing
    /// store — WARN-2 ruling, see CtPushButton::paint). `painter` must be
    /// active with the widget-local transform.
    void paint(CtPainter& painter, const CtRect& rect) const;

protected:
    /// (UI4) The editor's key-input hook: editing keys (text input /
    /// backspace / delete / arrows / home / end) mutate the buffer and
    /// emit CtTextChangedPayload; Enter is ignored (the app binds its own
    /// action). Protected (P2-3 fix): input hooks are internal; the public
    /// entry is CtWidgetBase::dispatchInput.
    void onKeyInput(const crisp::input::CtKeyPayload& payload) override;

    /// (UI4) The editor's mouse-input hook: a press positions the caret
    /// (character index from the backend's fixed-width font metrics; the
    /// GDI backend resolves real metrics — see CtPaintBackend::fontMetrics).
    void onMouseInput(const crisp::input::CtMousePayload& payload) override;

private:
    /// Insert `text` at the caret (advances the caret); emits the
    /// text-changed semantic event.
    void insertAtCaret(std::u16string_view text);

    /// Erase one code unit before the caret (backspace); emits the
    /// text-changed semantic event.
    void eraseBeforeCaret();

    /// Erase one code unit after the caret (delete); emits the
    /// text-changed semantic event.
    void eraseAfterCaret();

    /// Emit CtTextChangedPayload (current text + caret) to this editor's
    /// handlers.
    void emitTextChanged();

    /// The text buffer (UTF-16). Not on the hot path (a keystroke
    /// allocates once — same precedent as CtKeyPayload::text; the buffer
    /// is reused across edits).
    std::u16string m_text;
    /// The caret position (0..m_text.size()).
    std::size_t m_cursor = 0;
};

} // namespace crisp::ui
