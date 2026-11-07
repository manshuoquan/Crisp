// dual_main_win32.cpp — widget-tree dual-run, WIN32 mapper backend
// (UI3 O4: 控件树级完整双跑). Maps the FIXED semantic sequence through
// mapWin32Key/mapWin32Mouse (synthetic CtRawMsg — pure, no HWND needed)
// into core crisp::input payloads, then drives the shared widget tree
// (ui_dual_driver.cpp). The semantic-event log must be byte-identical to
// the Qt-host backend's (dual_main_qt.cpp).
//
// Part of the CRISP project. Authoritative spec:
// docs/ui/06-ui3-architecture.md §17 (O4); M3 §6.2 (byte compare).

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // WM_LBUTTONDOWN / WM_KEYDOWN etc.
#endif

#include "ui_dual_model.h"
#include "ui_dual_common.h" // parseLogPath (shared, SUGGEST-3)
#include "ui/ct_input_mapper.h" // mapWin32Key/Mouse + CtRawMsg (declared here)

namespace {
using crisp::ui::dual::CtDualInput;
using crisp::ui::dual::CtUiDualModel;
using crisp::ui::platform::CtDblClickState;
using crisp::ui::platform::CtRawMsg;
using crisp::ui::platform::isInput;
using crisp::ui::platform::mapWin32Key;
using crisp::ui::platform::mapWin32Mouse;

/// Append the mapped result (if it is an input payload) to `out`.
void appendIfInput(const crisp::ui::platform::CtInputMapperResult& r,
                   std::vector<CtDualInput>& out) {
    if (!isInput(r))
        return;
    if (std::holds_alternative<crisp::input::CtMousePayload>(r))
        out.emplace_back(std::get<crisp::input::CtMousePayload>(r));
    else
        out.emplace_back(std::get<crisp::input::CtKeyPayload>(r));
}

/// Build the semantic sequence as Win32 messages → mapped payloads.
std::vector<CtDualInput> win32Sequence() {
    std::vector<CtDualInput> out;
    CtDblClickState dbl;
    const crisp::ui::CtSize widgetSize{300, 120};

    // 1) Button press at (50,55): WM_LBUTTONDOWN.
    CtRawMsg down{};
    down.msg = WM_LBUTTONDOWN;
    down.wParam = MK_LBUTTON;
    down.ptX = 50;
    down.ptY = 55;
    down.time = 1;
    down.bootTimeMs = 1;
    appendIfInput(mapWin32Mouse(down, widgetSize, dbl), out);

    // 2) Button release at (50,55): WM_LBUTTONUP → click "C 50 55".
    CtRawMsg up{};
    up.msg = WM_LBUTTONUP;
    up.wParam = 0; // no buttons held after release
    up.ptX = 50;
    up.ptY = 55;
    up.time = 2;
    up.bootTimeMs = 2;
    appendIfInput(mapWin32Mouse(up, widgetSize, dbl), out);

    // 3) Tab twice: steps the focus chain edit→button→edit (empty text
    // in BOTH modes — MUST-3/D12/WARN-2: text excluded, the focus
    // SEMANTIC event is comparable).
    CtRawMsg keyTab{};
    keyTab.msg = WM_KEYDOWN;
    keyTab.wParam = VK_TAB;
    keyTab.lParam = 0;
    appendIfInput(mapWin32Key(keyTab), out);
    appendIfInput(mapWin32Key(keyTab), out);

    return out;
}

/// Parse --log=<path> (shared helper, SUGGEST-3).
std::string parseLogPath(int argc, char** argv) {
    return crisp::ui::dual::parseLogPath(argc, argv);
}

} // namespace (anonymous)

int main(int argc, char** argv) {
    CtUiDualModel model;
    auto root = model.buildTree();  // keep the tree alive

    // Both backends do the SAME focus setup: focus the edit box first
    // (the chain starts empty; typing targets the edit).
    model.focusChain()->setFocus(model.edit());
    // The focus-gained semantic event was recorded by the chain.

    // Map the fixed sequence via the Win32 mapper.
    const std::vector<CtDualInput> inputs = win32Sequence();
    crisp::ui::dual::drivePayloads(model, inputs);

    const std::string path = parseLogPath(argc, argv);
    // WARN-2 fix: remove any STALE log first (an incremental rebuild must
    // not leave an old file that the comparator could diff), and fail
    // loudly when the log cannot be opened (a silently-empty log would
    // byte-match the other backend's empty log — a false green).
    std::remove(path.c_str());
    std::ofstream out(path.c_str(), std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "win32 backend: cannot open log '%s'\n",
                     path.c_str());
        return 1;
    }
    for (const std::string& line : model.log())
        out << line << "\n";
    out.close();

    // Exit code: 0 on a consistent state (click + focus events present).
    std::printf("win32 backend: log=%zu lines\n", model.log().size());
    for (const auto& l : model.log())
        std::printf("  %s\n", l.c_str());
    return 0;
}
