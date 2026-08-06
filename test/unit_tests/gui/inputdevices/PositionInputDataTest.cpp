/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "gui/inputdevices/PositionInputData.h"
#include "model/Point.h"

namespace {
PositionInputData stylusEvent() {
    PositionInputData pos{};
    pos.isStylus = true;
    return pos;
}
}  // namespace

TEST(PositionInputDataTest, DetectsRawTipUpAfterDisplayPressureWasClamped) {
    PositionInputData pos = stylusEvent();
    pos.rawPressure = 0.0;
    pos.pressure = 0.05;

    EXPECT_TRUE(pos.isStylusTipUpMotion());
}

TEST(PositionInputDataTest, DoesNotConfuseLowContactPressureWithTipUp) {
    PositionInputData pos = stylusEvent();
    pos.rawPressure = 0.001;
    pos.pressure = 0.05;

    EXPECT_FALSE(pos.isStylusTipUpMotion());
}

TEST(PositionInputDataTest, RetainsLegacyFilteredZeroFallbackWithoutRawPressure) {
    PositionInputData pos = stylusEvent();
    ASSERT_EQ(pos.rawPressure, Point::NO_PRESSURE);

    pos.pressure = 0.0;
    EXPECT_TRUE(pos.isStylusTipUpMotion());

    pos.pressure = 0.05;
    EXPECT_FALSE(pos.isStylusTipUpMotion());
}

TEST(PositionInputDataTest, NonStylusDevicesNeverReportTipUp) {
    // Touchscreens are allowed to expose a pressure axis, and some report zero
    // at the start of a contact. Such an event must not abort the stroke.
    PositionInputData pos{};
    ASSERT_FALSE(pos.isStylus);

    pos.rawPressure = 0.0;
    pos.pressure = 0.0;
    EXPECT_FALSE(pos.isStylusTipUpMotion());
}
