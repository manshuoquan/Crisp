#pragma once

// ct_paint_cmd.h — UI2 record-style paint command (crisp::ui).
// One command = one recorded primitive in CtPainter's bounded command
// buffer (D1); value semantics, POD-sized (u16string_view refs into
// caller-owned text — NO copy, NO allocation).
// Authoritative spec: docs/ui/05-ui2-architecture.md §3.3 / §10 D1.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "ct_geometry.h"     // CtRect
#include "ct_paint_types.h"  // CtColor / CtPoint

namespace crisp::ui {

/// The bounded capacity of CtPainter's record-style command buffer (D1),
/// mirroring the kMaxQueued = 8192 discipline of the event queue.
inline constexpr std::size_t kMaxPaintCmds = 8192;

/// The kind of a recorded paint command.
enum class CtPaintCmdType : std::uint8_t {
    kFillRect = 0, ///< fillRect(rect, color)
    kDrawText = 1, ///< drawText(origin, text, font, color)
};

/// One recorded paint command (UI2 record-style command buffer, D1).
/// Discriminated union by CtPaintCmdType; value semantics, POD-sized
/// (u16string_view refs into caller-owned text — NO copy, NO allocation).
/// Authoritative spec: docs/ui/05-ui2-architecture.md §3.3 / §10 D1.
struct CtPaintCmd {
    CtPaintCmdType type = CtPaintCmdType::kFillRect;
    CtRect rect;              ///< kFillRect: target rect (device coords)
    CtPoint origin;           ///< kDrawText: baseline origin
    CtColor color;            ///< both: fill / text color
    // kDrawText only: font identity WITHOUT owning the family string.
    // pixelSize is a value; family is a u16string_view REF into
    // caller-owned storage (same lifetime discipline as `text`) — the
    // command must NOT copy the family (zero-allocation hot path, MUST-1
    // ruling: see docs/ui/05-ui2-architecture.md §3.3 / §10 D1).
    std::uint32_t fontPixelSize = 14;
    std::u16string_view family; ///< refs caller-owned storage (may be empty)
    std::u16string_view text;   ///< kDrawText only; refs caller-owned storage
};

} // namespace crisp::ui
