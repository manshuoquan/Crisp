// dual_main_qt.cpp — widget-tree dual-run, QT-HOST mapper backend
// (UI3 O4: 控件树级完整双跑). Maps the FIXED semantic sequence through
// mapBridgeKey/mapBridgeMouse (M4 bridge payloads with Qt fields) into
// core crisp::input payloads, then drives the shared widget tree
// (ui_dual_driver.cpp). The semantic-event log must be byte-identical to
// the Win32 backend's (dual_main_win32.cpp).
//
// Part of the CRISP project. Authoritative spec:
// docs/ui/06-ui3-architecture.md §17 (O4); M3 §6.2 (byte compare).

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

#include <QApplication>

#include "ui_dual_model.h"
#include "ui_dual_common.h" // parseLogPath (shared, SUGGEST-3)
#include "ui/ct_input_mapper.h"
#include "ui/platform/ct_input_mapper_bridge.h"
#include "bridge/qt_mouse_payload.h"
#include "bridge/qt_key_payload.h"

namespace {
using crisp::ui::dual::CtDualInput;
using crisp::ui::dual::CtUiDualModel;
using crisp::ui::platform::mapBridgeKey;
using crisp::ui::platform::mapBridgeMouse;

/// Append the mapped result (if it is an input payload) to `out`.
void appendIfInput(const crisp::ui::platform::CtInputMapperResult& r,
                   std::vector<CtDualInput>& out) {
    if (!crisp::ui::platform::isInput(r))
        return;
    if (std::holds_alternative<crisp::input::CtMousePayload>(r))
        out.emplace_back(std::get<crisp::input::CtMousePayload>(r));
    else
        out.emplace_back(std::get<crisp::input::CtKeyPayload>(r));
}

/// Build the semantic sequence as Qt-field bridge payloads → mapped core
/// payloads (the SAME sequence as the Win32 backend).
std::vector<CtDualInput> qtSequence() {
    std::vector<CtDualInput> out;
    const crisp::ui::CtSize widgetSize{300, 120};

    // 1) Button press at (50,55): Qt::LeftButton press.
    crisp::event::CtMousePayload down;
    down.action = crisp::event::CtMouseAction::kPress;
    down.x = 50;
    down.y = 55;
    down.button = Qt::LeftButton;
    down.buttons = Qt::LeftButton;
    down.modifiers = Qt::NoModifier;
    appendIfInput(mapBridgeMouse(down, widgetSize), out);

    // 2) Button release at (50,55) → click "C 50 55".
    crisp::event::CtMousePayload up;
    up.action = crisp::event::CtMouseAction::kRelease;
    up.x = 50;
    up.y = 55;
    up.button = Qt::LeftButton;
    up.buttons = Qt::NoButton;
    up.modifiers = Qt::NoModifier;
    appendIfInput(mapBridgeMouse(up, widgetSize), out);

    // 3) Tab twice: steps the focus chain edit→button→edit (empty text
    // in BOTH modes — MUST-3/D12/WARN-2: text excluded, the focus
    // SEMANTIC event is comparable).
    crisp::event::CtKeyPayload keyTab;
    keyTab.action = crisp::event::CtKeyAction::kPress;
    keyTab.key = Qt::Key_Tab;
    keyTab.text = QStringLiteral("");
    keyTab.modifiers = Qt::NoModifier;
    appendIfInput(mapBridgeKey(keyTab), out);
    appendIfInput(mapBridgeKey(keyTab), out);

    return out;
}

/// Parse --log=<path> (shared helper, SUGGEST-3).
std::string parseLogPath(int argc, char** argv) {
    return crisp::ui::dual::parseLogPath(argc, argv);
}

} // namespace (anonymous)

int main(int argc, char** argv) {
    // QApplication (NOT QCoreApplication): mapBridge* uses Qt GUI types
    // (Qt::Key etc.), and Qt 6 fail-fasts without a QGuiApplication.
    QApplication app(argc, argv);

    CtUiDualModel model;
    auto root = model.buildTree();  // keep the tree alive

    // Both backends do the SAME focus setup: focus the edit box first.
    model.focusChain()->setFocus(model.edit());

    // Map the fixed sequence via the Qt-host mapper.
    const std::vector<CtDualInput> inputs = qtSequence();
    crisp::ui::dual::drivePayloads(model, inputs);

    const std::string path = parseLogPath(argc, argv);
    // WARN-2 fix: remove stale log + fail loudly on open failure (see the
    // win32 backend — a silently-empty log would be a false green).
    std::remove(path.c_str());
    std::ofstream out(path.c_str(), std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "qt-host backend: cannot open log '%s'\n",
                     path.c_str());
        return 1;
    }
    for (const std::string& line : model.log())
        out << line << "\n";
    out.close();

    // Exit code: 0 on a consistent state.
    std::printf("qt-host backend: log=%zu lines\n", model.log().size());
    for (const auto& l : model.log())
        std::printf("  %s\n", l.c_str());
    return 0;
}
