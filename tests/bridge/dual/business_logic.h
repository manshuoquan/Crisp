#pragma once

// business_logic.h — shared pure business state machine for dual-run
// verification (M3, §6.1).
//
// Part of the CRISP project. PURE logic: no Qt, no CRISP types. Both backends
// (main_qt.cpp and main_bridge.cpp) feed the SAME driving sequence into the
// SAME CtDualModel and write the SAME log() lines, so the two outputs must be
// byte-identical (§6.2). The widget-event sequence is: mouse moves -> key
// presses -> one timeout.

#include <string>
#include <vector>

/// Pure business state machine: counts mouse moves, applies key presses,
/// fires a timeout after N moves. No Qt/CRISP types — both backends drive it.
struct CtDualModel {
    /// Mouse move observed at (x, y).
    void onMouseMove(int x, int y) {
        ++mMouseMoves;
        mLastX = x;
        mLastY = y;
        mLog.push_back("M " + std::to_string(x) + " " + std::to_string(y));
    }

    /// Key press observed.
    void onKeyPress(int key) {
        ++mKeyPresses;
        mLog.push_back("K " + std::to_string(key));
    }

    /// Timeout observed.
    void onTimeout() {
        ++mTimeouts;
        mLog.push_back("T");
    }

    /// Deterministic "log": append one line per event (stable ordering).
    std::vector<std::string>& log() { return mLog; }

    int mMouseMoves = 0;
    int mKeyPresses = 0;
    int mTimeouts = 0;
    int mLastX = 0;
    int mLastY = 0;

private:
    std::vector<std::string> mLog;  // "M x y" / "K key" / "T"
};
