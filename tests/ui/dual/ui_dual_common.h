#pragma once

// ui_dual_common.h — shared helpers for the UI dual-run backends
// (tests/ui/dual). Eliminates the duplicated parseLogPath (audit SUGGEST-3)
// and documents the dual-run scope.
//
// Part of the CRISP project. Authoritative spec:
// docs/ui/06-ui3-architecture.md §17 (O4).

#include <string>

namespace crisp::ui::dual {

/// Parse --log=<path> from argv (default "ui_dual.log").
inline std::string parseLogPath(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const std::string prefix = "--log=";
        if (arg.compare(0, prefix.size(), prefix) == 0)
            return arg.substr(prefix.size());
    }
    return "ui_dual.log";
}

} // namespace crisp::ui::dual
