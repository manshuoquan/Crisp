// ct_paint_types.cpp — UI2 paint value type implementations (crisp::ui).
// CtFont's operator== lives here (non-inline); all other UI2 value types
// are header-inline (CtColor/CtPoint/CtSize/CtPixelFormat/CtPaintMetrics).
// Authoritative spec: docs/ui/05-ui2-architecture.md §3.

#include "ct_paint_types.h"

namespace crisp::ui {

bool operator==(const CtFont& a, const CtFont& b) noexcept {
    // Fonts are equal when family + pixelSize match (§3.2 contract).
    return a.pixelSize == b.pixelSize && a.family == b.family;
}

} // namespace crisp::ui
