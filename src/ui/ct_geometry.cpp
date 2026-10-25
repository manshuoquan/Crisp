// ct_geometry.cpp — implementations of the UI1 geometry primitives
// (crisp::ui). All operations are value-semantic and allocation-free.
// Authoritative spec: docs/ui/04-ui1-architecture.md §2.1/§2.2.

#include "ct_geometry.h"

#include <algorithm>

namespace crisp::ui {

bool CtRect::intersects(const CtRect& other) const noexcept {
    // Half-open interval math: [x, right()) x [y, bottom()).
    // Contract: empty rects NEVER intersect — short-circuit both operands
    // (an empty rect has width/height <= 0 and must not report overlap even
    // when its coordinates sit inside the other rect).
    if (isEmpty() || other.isEmpty())
        return false;
    return x < other.right() && other.x < right() &&
           y < other.bottom() && other.y < bottom();
}

CtRect CtRect::intersect(const CtRect& other) const noexcept {
    // Half-open interval math: max(x1,x2) .. min(right1,right2); a
    // negative result width/height is normalized to zero (empty).
    const int nx = std::max(x, other.x);
    const int ny = std::max(y, other.y);
    const int nr = std::min(right(), other.right());
    const int nb = std::min(bottom(), other.bottom());
    const int nw = (nr > nx) ? (nr - nx) : 0;
    const int nh = (nb > ny) ? (nb - ny) : 0;
    return CtRect{nx, ny, nw, nh};
}

CtRect CtRect::united(const CtRect& other) const noexcept {
    // Single bounding box; an empty operand contributes nothing.
    if (isEmpty())
        return other;
    if (other.isEmpty())
        return *this;
    const int nx = std::min(x, other.x);
    const int ny = std::min(y, other.y);
    const int nr = std::max(right(), other.right());
    const int nb = std::max(bottom(), other.bottom());
    return CtRect{nx, ny, nr - nx, nb - ny};
}

void CtRegion::add(const CtRect& rect) noexcept {
    // Bounding-box approximation (D2): union the single rectangle.
    // An EMPTY `rect` is a no-op — explicit guard so a negative-size
    // (empty) rect cannot be stored verbatim into `bounds` (which would
    // violate CtRect's canonical width/height >= 0 form).
    if (rect.isEmpty())
        return;
    bounds = bounds.united(rect);
}

} // namespace crisp::ui
