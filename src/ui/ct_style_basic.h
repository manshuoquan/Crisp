#pragma once

// ct_style_basic.h — UI5 basic style (crisp::ui).
// Compile-time constant palette (kPalette), deterministic per-state
// mapping. NO CSS, NO runtime parsing — the palette is data, and changing
// it recompiles (explicit, observable).
// Authoritative spec: docs/ui/03-rewrite-proposal.md §4.5.

#include <cstdint>

#include "ct_button_state.h" // CtButtonState (shared state enum)
#include "ct_paint_types.h" // CtColor / CtSize
#include "ct_style.h"       // CtStyle (base)

namespace crisp::ui {

/// (UI5) The basic style: compile-time constant palette (kPalette),
/// deterministic per-state mapping. NO CSS, NO runtime parsing — the
/// palette is data, and changing it recompiles (explicit, observable).
class CtBasicStyle final : public CtStyle {
public:
    /// The fixed palette (compile-time constants). Per-state text colors:
    /// kNormal / kHovered / kPressed / kDisabled.
    struct Palette {
        CtColor background;                 ///< widget background
        CtColor textNormal;                 ///< text, normal state
        CtColor textHovered;                ///< text, hovered state
        CtColor textPressed;                ///< text, pressed state
        CtColor textDisabled;               ///< text, disabled state
        CtColor border;                     ///< border color
        int borderWidth = 0;                ///< border width (px, 0 = none)
        int horizontalPadding = 0;          ///< fixed horizontal padding (px)
        int verticalPadding = 0;            ///< fixed vertical padding (px)
    };

    /// The DEFAULT palette (compile-time constant; deterministic).
    static constexpr Palette kDefaultPalette{
        /*background*/ CtColor{240, 240, 240, 255},
        /*textNormal*/ CtColor{0, 0, 0, 255},
        /*textHovered*/ CtColor{0, 0, 0, 255},
        /*textPressed*/ CtColor{0, 0, 0, 255},
        /*textDisabled*/ CtColor{160, 160, 160, 255},
        /*border*/ CtColor{120, 120, 120, 255},
        /*borderWidth*/ 1,
        /*horizontalPadding*/ 8,
        /*verticalPadding*/ 4,
    };

    /// Construct with the default palette.
    CtBasicStyle() noexcept = default;

    /// Construct with an explicit palette (still compile-time data).
    explicit CtBasicStyle(Palette palette) noexcept : m_palette(palette) {}

    CtColor textColor(const CtWidgetBase& w) const noexcept override;
    CtColor backgroundColor(const CtWidgetBase& w) const noexcept override;
    CtColor borderColor(const CtWidgetBase& w) const noexcept override;
    int borderWidth(const CtWidgetBase& w) const noexcept override;
    CtColor textColorForState(
        const CtWidgetBase& w,
        CtButtonState state) const noexcept override;
    CtSize sizeHint(const CtWidgetBase& w) const noexcept override;

    /// The active palette (read-only; a different palette is a NEW style).
    const Palette& palette() const noexcept { return m_palette; }

private:
    Palette m_palette{kDefaultPalette};
};

/// (UI5) The default style used when a widget has none set (a single
/// static CtBasicStyle — the default palette). Inline so all widgets
/// share ONE instance.
/// LIFETIME + THREAD CONTRACT (WARN-3 ruling): the static lives for the
/// whole program; it is const (no mutable state), so sharing across
/// threads is safe. A widget with no style points at it implicitly —
/// never destroy it.
inline const CtStyle& defaultStyle() noexcept {
    static const CtBasicStyle s_default;
    return s_default;
}

} // namespace crisp::ui
