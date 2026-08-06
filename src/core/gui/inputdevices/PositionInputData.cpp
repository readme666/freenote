#include "PositionInputData.h"

auto PositionInputData::isShiftDown() const -> bool { return state & GDK_SHIFT_MASK; }

auto PositionInputData::isControlDown() const -> bool { return state & GDK_CONTROL_MASK; }

auto PositionInputData::isAltDown() const -> bool { return state & GDK_MOD1_MASK; }

auto PositionInputData::isStylusTipUpMotion() const -> bool {
    // Only a stylus reports a tip-up motion. Touchscreens are allowed to report
    // a pressure axis too, and some of them report zero at the start of a
    // contact -- treating that as a tip-up would swallow the whole stroke.
    if (!isStylus) {
        return false;
    }
    // The fallback preserves the historical semantics for callers which
    // construct PositionInputData directly and therefore have no raw value.
    return rawPressure == 0.0 || (rawPressure == Point::NO_PRESSURE && pressure == 0.0);
}
