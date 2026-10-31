#pragma once

// ct_paint_types.h — UI2 paint value types (crisp::ui).
// Value semantics; hot-path zero heap allocation everywhere.
// Authoritative spec: docs/ui/05-ui2-architecture.md §3.

#include <cstdint>
#include <string>

namespace crisp::ui {

/// RGBA8888 color (UI2 core; PixelBuffer format kRgba8888).
/// Value semantics; no allocation. Premultiplied-alpha NOT used in UI2
/// (only solid fills — no blending in UI2 scope).
struct CtColor {
    std::uint8_t r = 0;   ///< red 0..255
    std::uint8_t g = 0;   ///< green 0..255
    std::uint8_t b = 0;   ///< blue 0..255
    std::uint8_t a = 255; ///< alpha 0..255 (255 = opaque)

    /// True if the color is fully opaque.
    bool isOpaque() const noexcept { return a == 255; }

    /// Named colors (UI2 minimal set; UI5 style system may extend).
    static CtColor black() noexcept { return CtColor{0, 0, 0, 255}; }
    static CtColor white() noexcept { return CtColor{255, 255, 255, 255}; }
};

/// 2D integer point (pixel coordinates).
struct CtPoint {
    int x = 0;
    int y = 0;
};

/// 2D integer size (width/height >= 0; non-negative by convention).
struct CtSize {
    int width = 0;
    int height = 0;

    /// True if width <= 0 || height <= 0.
    bool isEmpty() const noexcept { return width <= 0 || height <= 0; }
};

/// Pixel buffer format. UI2 core supports exactly ONE format.
enum class CtPixelFormat : std::uint8_t {
    kRgba8888 = 0, ///< 4 bytes/pixel, byte order R,G,B,A (UI2 only format)
};

/// Font identity (UI2 minimal set). NO metrics here — metrics live on the
/// backend's optional fontMetrics() capability (D7). Font is a value
/// describing family + size; the backend resolves it.
struct CtFont {
    std::uint32_t pixelSize = 14; ///< pixel height (positive)
    /// Family name (UTF-16; empty = backend default). Value semantics;
    /// NOT on the hot path (drawText carries it, resolved by backend).
    std::u16string family;
};

/// Equality: fonts are equal when family + pixelSize match.
bool operator==(const CtFont& a, const CtFont& b) noexcept;

/// Text metrics result from the backend's optional fontMetrics() capability
/// (D7). Fixed values in the PixelBuffer backend (testable off-screen);
/// GDI backend uses GetTextExtentPoint32W.
struct CtPaintMetrics {
    int width = 0;  ///< advance width (px)
    int height = 0; ///< line height (px)
};

} // namespace crisp::ui
