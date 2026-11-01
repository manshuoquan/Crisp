#pragma once

// ct_input_mapper_bridge.h - UI3 Qt-host mode mapper (PLATFORM TU, Qt
// dependency allowed - MUST-7 keeps bridge types out of the core library).
// Maps M4 bridged payloads (crisp::event, Qt fields) into the core
// crisp::input payloads (P4 full signature).
// Authoritative spec: docs/ui/06-ui3-architecture.md section 4.3 (P4).

#include "event/ct_mouse_payload.h"   // crisp::input payloads
#include "event/ct_key_payload.h"
#include "bridge/qt_mouse_payload.h"  // crisp::event::CtMousePayload (Qt fields, has timestamp per MUST-8)
#include "bridge/qt_key_payload.h"    // crisp::event::CtKeyPayload
#include "../ct_paint_types.h"        // CtSize
#include "../ct_input_mapper.h"       // CtInputMapperResult / crisp::ui::platform

namespace crisp::ui::platform {

/// Map a bridged M4 payload (crisp::event, Qt fields) into the core
/// crisp::input payload (Qt-host mode). Bridge payloads include
/// QString/Qt::Key - PLATFORM TU (MUST-7: core library stays Qt-free).
CtInputMapperResult mapBridgeMouse(
    const crisp::event::CtMousePayload& qtFields, ///< M4 bridge mouse (has timestamp, MUST-8)
    const CtSize& widgetLocalSize) noexcept;

CtInputMapperResult mapBridgeKey(
    const crisp::event::CtKeyPayload& qtFields) noexcept; ///< keeps text (D12)

} // namespace crisp::ui::platform
