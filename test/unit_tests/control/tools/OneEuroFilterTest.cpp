/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include "control/tools/OneEuroFilter.h"
#include "control/tools/StrokeStabilizer.h"
#include "model/Point.h"

using StrokeStabilizer::OneEuroFilter;
using StrokeStabilizer::OneEuroFilter2D;

TEST(OneEuroFilterTest, PreservesFirstSampleAndResetValue) {
    OneEuroFilter filter(1.0, 0.01);

    EXPECT_DOUBLE_EQ(filter.filter(42.0, 100), 42.0);
    filter.filter(100.0, 108);
    filter.reset(-7.0, 200);
    EXPECT_DOUBLE_EQ(filter.filter(-7.0, 208), -7.0);
}

TEST(OneEuroFilterTest, AttenuatesStationaryJitter) {
    OneEuroFilter filter(1.0, 0.01);
    filter.reset(100.0, 0);

    double rawSquaredError = 0.0;
    double filteredSquaredError = 0.0;
    for (std::uint32_t i = 1; i <= 240; ++i) {
        const double raw = 100.0 + (i % 2 == 0 ? 1.0 : -1.0);
        const double filtered = filter.filter(raw, i * 8);
        if (i > 40) {
            rawSquaredError += (raw - 100.0) * (raw - 100.0);
            filteredSquaredError += (filtered - 100.0) * (filtered - 100.0);
        }
    }

    EXPECT_LT(filteredSquaredError, rawSquaredError * 0.05);
}

TEST(OneEuroFilterTest, SpeedAdaptationReducesFastMotionLag) {
    OneEuroFilter fixedCutoff(1.0, 0.0);
    OneEuroFilter adaptive(1.0, 0.01);
    fixedCutoff.reset(0.0, 0);
    adaptive.reset(0.0, 0);

    double fixed = 0.0;
    double adapted = 0.0;
    for (std::uint32_t i = 1; i <= 60; ++i) {
        const double position = static_cast<double>(i) * 8.0;
        fixed = fixedCutoff.filter(position, i * 8);
        adapted = adaptive.filter(position, i * 8);
    }

    EXPECT_LT(480.0 - adapted, (480.0 - fixed) * 0.35);
}

TEST(OneEuroFilterTest, IsStableAcrossSamplingRates) {
    auto runRamp = [](std::uint32_t intervalMs) {
        OneEuroFilter filter(1.0, 0.01);
        filter.reset(0.0, 0);
        double filtered = 0.0;
        for (std::uint32_t timestamp = intervalMs; timestamp <= 960; timestamp += intervalMs) {
            filtered = filter.filter(static_cast<double>(timestamp) * 0.5, timestamp);
        }
        return filtered;
    };

    EXPECT_NEAR(runRamp(4), runRamp(16), 4.0);
}

TEST(OneEuroFilterTest, CatchesUpAfterAStationaryPause) {
    OneEuroFilter filter(1.0, 0.01);
    filter.reset(0.0, 0);

    EXPECT_GT(filter.filter(100.0, 2000), 90.0);
}

TEST(OneEuroFilterTest, HandlesDuplicateWrappingAndInvalidTimestamps) {
    OneEuroFilter filter(1.0, 0.01);
    filter.reset(0.0, std::numeric_limits<std::uint32_t>::max() - 3);

    EXPECT_TRUE(std::isfinite(filter.filter(1.0, std::numeric_limits<std::uint32_t>::max() - 3)));
    EXPECT_TRUE(std::isfinite(filter.filter(2.0, 2)));
    EXPECT_TRUE(std::isfinite(filter.filter(3.0, 1)));
}

TEST(OneEuroFilter2DTest, HasTheSameLagInEveryDirection) {
    auto runRamp = [](double stepX, double stepY) {
        OneEuroFilter2D filter(1.0, 0.02, 2.0);
        filter.reset({}, 0);

        OneEuroFilter2D::Sample filtered;
        for (std::uint32_t i = 1; i <= 60; ++i) {
            filtered = filter.filter({stepX * i, stepY * i}, i * 8);
        }

        return std::hypot(stepX * 60 - filtered.x, stepY * 60 - filtered.y);
    };

    const double diagonalStep = 8.0 / std::sqrt(2.0);
    EXPECT_NEAR(runRamp(8.0, 0.0), runRamp(diagonalStep, diagonalStep), 1e-9);
}

TEST(OneEuroFilter2DTest, ReducesLagOnShortDiagonalStrokes) {
    OneEuroFilter legacyX(1.0, 0.01);
    OneEuroFilter legacyY(1.0, 0.01);
    OneEuroFilter2D improved(1.0, 0.01, 2.0);
    legacyX.reset(0.0, 0);
    legacyY.reset(0.0, 0);
    improved.reset({}, 0);

    const double step = 8.0 / std::sqrt(2.0);
    double legacyFilteredX = 0.0;
    double legacyFilteredY = 0.0;
    OneEuroFilter2D::Sample improvedFiltered;
    for (std::uint32_t i = 1; i <= 10; ++i) {
        legacyFilteredX = legacyX.filter(step * i, i * 8);
        legacyFilteredY = legacyY.filter(step * i, i * 8);
        improvedFiltered = improved.filter({step * i, step * i}, i * 8);
    }

    const double legacyLag = std::hypot(step * 10 - legacyFilteredX, step * 10 - legacyFilteredY);
    const double improvedLag = std::hypot(step * 10 - improvedFiltered.x, step * 10 - improvedFiltered.y);
    EXPECT_LT(improvedLag, legacyLag * 0.75);
}

TEST(OneEuroFilter2DTest, TracksA120HzFastStrokeWithinOneFrame) {
    OneEuroFilter2D filter(1.0, 0.02, 2.0);
    filter.reset({}, 0);

    const double step = 8.0 / std::sqrt(2.0);
    OneEuroFilter2D::Sample filtered;
    for (std::uint32_t i = 1; i <= 12; ++i) {
        filtered = filter.filter({step * i, step * i}, i * 8);
    }

    EXPECT_LT(std::hypot(step * 12 - filtered.x, step * 12 - filtered.y), 7.0);
}

TEST(OneEuroFilter2DTest, KeepsTransverseNoiseBoundedDuringFastMotion) {
    OneEuroFilter2D filter(1.0, 0.02, 2.0);
    filter.reset({}, 0);

    double squaredError = 0.0;
    std::uint32_t sampleCount = 0;
    for (std::uint32_t i = 1; i <= 240; ++i) {
        const double jitter = i % 2 == 0 ? -1.0 : 1.0;
        const auto filtered = filter.filter({static_cast<double>(i) * 8.0, jitter}, i * 8);
        if (i > 40) {
            squaredError += filtered.y * filtered.y;
            ++sampleCount;
        }
    }

    EXPECT_LT(std::sqrt(squaredError / sampleCount), 0.55);
}

TEST(OneEuroFilter2DTest, RejectsAnInvalidSampleAtomically) {
    OneEuroFilter2D filter(1.0, 0.02, 2.0);
    filter.reset({1.0, 2.0}, 0);
    const auto valid = filter.filter({3.0, 4.0}, 8);
    const auto invalid = filter.filter({std::numeric_limits<double>::quiet_NaN(), 5.0}, 16);

    EXPECT_DOUBLE_EQ(valid.x, invalid.x);
    EXPECT_DOUBLE_EQ(valid.y, invalid.y);
}

namespace {

using StrokeStabilizer::Active;
using StrokeStabilizer::Event;
using StrokeStabilizer::OneEuro;

/**
 * Stop OneEuro::averageAndPaint() after it has produced the filtered event,
 * before it tries to draw through a StrokeHandler.  This keeps the test at the
 * stabilizer integration boundary without needing a GUI or a document model.
 */
class PaintCaptured final {};

class OneEuroProbe final: public OneEuro {
public:
    OneEuroProbe(): Active(true), OneEuro(true, 1.0, 0.01) {}

    void prime(Event event, std::uint32_t timestamp) { resetBuffer(event, timestamp); }
    auto endpoint() -> Event { return getLastEvent(); }

    auto process(const Event& event, std::uint32_t timestamp) -> Event {
        this->painted.reset();
        try {
            averageAndPaint(event, timestamp);
        } catch (const PaintCaptured&) {
            // Expected: setLastPaintedEvent() aborts before drawEvent().
        }
        EXPECT_TRUE(this->painted.has_value());
        return this->painted.value_or(Event{});
    }

protected:
    void setLastPaintedEvent(const Event& event) override {
        this->painted = event;
        throw PaintCaptured{};
    }

private:
    std::optional<Event> painted;
};

TEST(StrokeStabilizerOneEuroTest, InvalidPressureFallsBackWithoutPoisoningTheStroke) {
    OneEuroProbe stabilizer;
    stabilizer.prime(Event(0.0, 0.0, Point::NO_PRESSURE), 0);

    const Event beforePressure = stabilizer.process(Event(1.0, 0.0, std::numeric_limits<double>::quiet_NaN()), 8);
    EXPECT_EQ(Point::NO_PRESSURE, beforePressure.pressure);

    const Event firstPressure = stabilizer.process(Event(2.0, 0.0, 0.4), 16);
    ASSERT_TRUE(std::isfinite(firstPressure.pressure));
    EXPECT_DOUBLE_EQ(0.4, firstPressure.pressure);

    const Event afterNaN = stabilizer.process(Event(3.0, 0.0, std::numeric_limits<double>::quiet_NaN()), 24);
    EXPECT_TRUE(std::isfinite(afterNaN.pressure));
    EXPECT_DOUBLE_EQ(firstPressure.pressure, afterNaN.pressure);

    const Event afterMissing = stabilizer.process(Event(4.0, 0.0, Point::NO_PRESSURE), 32);
    EXPECT_TRUE(std::isfinite(afterMissing.pressure));
    EXPECT_DOUBLE_EQ(firstPressure.pressure, afterMissing.pressure);
}

TEST(StrokeStabilizerOneEuroTest, EndpointKeepsRawPositionAndFilteredPressure) {
    OneEuroProbe stabilizer;
    stabilizer.prime(Event(0.0, 0.0, 0.1), 0);

    const Event painted = stabilizer.process(Event(10.0, 5.0, 1.0), 16);
    const Event endpoint = stabilizer.endpoint();

    EXPECT_LT(painted.x, endpoint.x);
    EXPECT_LT(painted.y, endpoint.y);
    EXPECT_DOUBLE_EQ(10.0, endpoint.x);
    EXPECT_DOUBLE_EQ(5.0, endpoint.y);
    EXPECT_GT(painted.pressure, 0.1);
    EXPECT_LT(painted.pressure, 1.0);
    EXPECT_DOUBLE_EQ(painted.pressure, endpoint.pressure);
}

}  // namespace
