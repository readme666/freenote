/*
 * Xournal++
 *
 * Base class for device input handling
 * Data to do an input
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gdk/gdk.h>  // for GdkModifierType
#include <glib.h>     // for guint32

#include "model/Point.h"  // for Point::NO_PRESSURE

#include "DeviceId.h"

class PositionInputData {
public:
    bool isShiftDown() const;
    bool isControlDown() const;
    bool isAltDown() const;

public:
    double x;  ///< in pixel coordinates, relative to the page's upper-left corner
    double y;  ///< in pixel coordinates, relative to the page's upper-left corner
    double pressure;
    /**
     * Pressure exactly as reported by the device, before minimum-pressure and
     * multiplier settings are applied.  Keeping this separate is important:
     * some styluses report a final motion event with zero pressure while the
     * tip is already lifting, and its coordinates are no longer a contact
     * point.
     */
    double rawPressure{Point::NO_PRESSURE};
    guint32 timestamp;
    DeviceId deviceId;
    /**
     * True only for events routed through the stylus/eraser input handler.
     * This is kept separate from pressure because touchscreens can report
     * pressure too.
     */
    bool isStylus{false};

    /**
     * State flags from GDKevent (Shift down etc.)
     */
    GdkModifierType state;

    /** Whether a motion event represents a pressure-sensitive tip leaving contact. */
    [[nodiscard]] bool isStylusTipUpMotion() const;
};
