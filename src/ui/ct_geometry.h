#pragma once

// ct_geometry.h — UI1 core geometry primitives (crisp::ui).
// Value semantics; int coordinates (widget-local and parent-local in UI1,
// root/window coordinates from UI3). Zero heap allocation everywhere.
// Authoritative spec: docs/ui/04-ui1-architecture.md §2.1/§2.2.

#include <cstdint>

namespace crisp::ui {

/// Axis-aligned integer rectangle. Canonical form: width/height >= 0
/// (an empty rect may have width/height == 0 and any x/y).
/// Value semantics; hot-path operations are constexpr-friendly, allocation-free.
struct CtRect {
    int x = 0;        ///< left edge (inclusive)
    int y = 0;        ///< top edge (inclusive)
    int width = 0;    ///< width in pixels (>= 0)
    int height = 0;   ///< height in pixels (>= 0)

    /// Right edge, exclusive. Precondition: x + width fits in int
    /// (UI1 geometry uses widget-local coordinates, far below INT_MAX).
    int right() const noexcept { return x + width; }
    /// Bottom edge, exclusive. Precondition: y + height fits in int.
    int bottom() const noexcept { return y + height; }
    /// True if width <= 0 || height <= 0.
    bool isEmpty() const noexcept { return width <= 0 || height <= 0; }

    /// True if `px`/`py` lies inside [x, right()) x [y, bottom()).
    bool contains(int px, int py) const noexcept {
        return px >= x && px < right() && py >= y && py < bottom();
    }

    /// True if `other` intersects this rect (non-empty overlap).
    /// Empty rects never intersect.
    bool intersects(const CtRect& other) const noexcept;

    /// The intersection of this and `other`; empty if disjoint.
    /// Result may be empty (caller checks isEmpty()).
    CtRect intersect(const CtRect& other) const noexcept;

    /// The smallest rect covering both (union bounding box).
    /// If either operand is empty, returns the other (copy).
    CtRect united(const CtRect& other) const noexcept;
};

/// Accumulated dirty region (UI1: single-rectangle union approximation).
///
/// DECISION D2 (authoritative, docs/ui/04-ui1-architecture.md §8): UI1
/// represents the dirty region as ONE bounding rectangle — markDamage()
/// unions into it via CtRect::united(). Rationale: (1) hot-path zero heap
/// allocation (a multi-rect list would need storage growth on the damage
/// path); (2) simple batching — one repaint pass covers all accumulated
/// damage; (3) UI2's backing store flush consumes exactly one rect per
/// widget. UI2 may evolve this into a multi-rect list (rect count bounded,
/// preallocated) WITHOUT changing the public API shape (dirtyRegion() still
/// returns a single CtRect).
struct CtRegion {
    CtRect bounds;   ///< union bounding box of all marked damage (may be empty)

    /// True if no damage has been accumulated.
    bool isEmpty() const noexcept { return bounds.isEmpty(); }

    /// Union `rect` into the region (bounding-box approximation).
    /// `rect` may be empty (incl. negative-size): NO-OP — an empty rect
    /// never affects the region, and the bounds stay canonical
    /// (width/height >= 0).
    void add(const CtRect& rect) noexcept;

    /// Reset to empty.
    void clear() noexcept { bounds = CtRect{}; }
};

} // namespace crisp::ui
