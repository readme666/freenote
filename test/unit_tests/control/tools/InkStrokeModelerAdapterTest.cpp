/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "control/tools/InkStrokeModelerAdapter.h"

namespace {

using StrokePrediction::InkStrokeModelerAdapter;
using StrokePrediction::InputSample;

constexpr double SCREEN_PIXELS_PER_CM = 96.0 / 2.54;

#ifdef XOURNALPP_ENABLE_INK_STROKE_MODELER

struct FinishedStroke {
    StrokePrediction::ModeledPoint lastPointBeforeTipUp;
    std::vector<StrokePrediction::ModeledPoint> tipUpPoints;
    double contactX{};
    double contactY{};
    double inputStep{};
};

auto runFastStrokeToTipUp(std::uint32_t releaseGapMs, bool curvedTail = false) -> FinishedStroke {
    InkStrokeModelerAdapter adapter(15.0);
    EXPECT_TRUE(adapter.beginStroke({.screenX = 0.0, .screenY = 0.0, .pressure = 0.55, .timestampMs = 0}, 1.0,
                                    SCREEN_PIXELS_PER_CM));

    FinishedStroke stroke;
    stroke.inputStep = 6.0;
    for (std::uint32_t i = 1; i <= 32; ++i) {
        const double y = curvedTail && i > 24 ? 1.5 * static_cast<double>((i - 24) * (i - 24)) : 0.0;
        const auto& frame = adapter.updateStroke(
                {.screenX = stroke.inputStep * i, .screenY = y, .pressure = 0.55, .timestampMs = 8 * i}, 1.0);
        EXPECT_FALSE(frame.degradedToRaw);
        if (!frame.actual.empty()) {
            stroke.lastPointBeforeTipUp = frame.actual.back();
        }
        stroke.contactX = stroke.inputStep * i;
        stroke.contactY = y;
    }

    const auto& finished = adapter.finishStroke({.screenX = stroke.contactX,
                                                 .screenY = stroke.contactY,
                                                 .pressure = 0.0,
                                                 .timestampMs = 32 * 8 + releaseGapMs});
    EXPECT_FALSE(finished.degradedToRaw);
    stroke.tipUpPoints = finished.actual;
    return stroke;
}

struct TipUpGeometry {
    double arcLength{};
    double endpointError{};
    double backwardsTravel{};
    double maximumOvershoot{};
};

auto measureTipUpGeometry(const FinishedStroke& stroke) -> TipUpGeometry {
    TipUpGeometry geometry;
    auto previous = stroke.lastPointBeforeTipUp;
    for (const auto& point: stroke.tipUpPoints) {
        geometry.arcLength += std::hypot(point.pageX - previous.pageX, point.pageY - previous.pageY);
        geometry.backwardsTravel += std::max(0.0, previous.pageX - point.pageX);
        geometry.maximumOvershoot = std::max(geometry.maximumOvershoot, point.pageX - stroke.contactX);
        previous = point;
    }
    geometry.endpointError = std::hypot(previous.pageX - stroke.contactX, previous.pageY - stroke.contactY);
    return geometry;
}
#endif

TEST(InkStrokeModelerAdapterTest, ReportsWhetherTheOptionalBackendWasBuilt) {
#ifdef XOURNALPP_ENABLE_INK_STROKE_MODELER
    EXPECT_TRUE(InkStrokeModelerAdapter::isCompiledIn());
#else
    EXPECT_FALSE(InkStrokeModelerAdapter::isCompiledIn());
#endif
}

#ifdef XOURNALPP_ENABLE_INK_STROKE_MODELER

TEST(InkStrokeModelerAdapterTest, ProducesBoundedPredictionForSteadyMotion) {
    InkStrokeModelerAdapter adapter(15.0);
    ASSERT_TRUE(adapter.beginStroke({.screenX = 0.0, .screenY = 0.0, .pressure = 0.5, .timestampMs = 100}, 1.0,
                                    SCREEN_PIXELS_PER_CM));

    bool sawPrediction = false;
    double latestActualX = 0.0;
    for (std::uint32_t i = 1; i <= 40; ++i) {
        const auto& frame = adapter.updateStroke(
                {.screenX = 4.0 * i, .screenY = 0.0, .pressure = 0.5, .timestampMs = 100 + 8 * i}, 1.0);
        ASSERT_FALSE(frame.degradedToRaw);
        if (!frame.actual.empty()) {
            latestActualX = frame.actual.back().pageX;
        }
        if (!frame.prediction.empty()) {
            sawPrediction = true;
            EXPECT_LE(frame.prediction.back().timeSeconds, 8.0 * i / 1000.0 + 0.020001);
            EXPECT_LE(std::hypot(frame.prediction.back().pageX - latestActualX, frame.prediction.back().pageY),
                      SCREEN_PIXELS_PER_CM + 1e-3);
        }
    }

    EXPECT_TRUE(sawPrediction);
}

// Regression: prediction used to be recomputed on every input event although
// it is only rendered at the display cadence. On a 250 Hz stylus that is four
// hidden Kalman runs per rendered frame. The computation is throttled to 8 ms;
// at a 4 ms input cadence every other event must keep the previous prediction.
TEST(InkStrokeModelerAdapterTest, ThrottlesPredictionToTheDisplayCadence) {
    InkStrokeModelerAdapter adapter(15.0);
    ASSERT_TRUE(adapter.beginStroke({.screenX = 0.0, .screenY = 0.0, .pressure = 0.5, .timestampMs = 0}, 1.0,
                                    SCREEN_PIXELS_PER_CM));

    int predictionEvents = 0;
    int throttledEvents = 0;
    for (std::uint32_t i = 1; i <= 40; ++i) {
        const auto& frame = adapter.updateStroke(
                {.screenX = 3.0 * i, .screenY = 0.0, .pressure = 0.5, .timestampMs = 4 * i}, 1.0);
        ASSERT_FALSE(frame.degradedToRaw);
        if (!frame.replacePrediction) {
            ++throttledEvents;
        }
        if (!frame.prediction.empty()) {
            ++predictionEvents;
        }
    }

    // 40 events at 4 ms: the even-indexed ones (20 of them) fall below the 8 ms
    // prediction interval and must reuse the previous prediction.
    EXPECT_GE(throttledEvents, 18);
    // The remaining events still recompute the prediction; it must not be
    // starved into invisibility.
    EXPECT_GE(predictionEvents, 12);
    EXPECT_TRUE(adapter.isActive());
}

TEST(InkStrokeModelerAdapterTest, FastTipUpConvergesToTheLastContactWithoutAHook) {
    const FinishedStroke stroke = runFastStrokeToTipUp(8);
    ASSERT_FALSE(stroke.tipUpPoints.empty());
    const TipUpGeometry geometry = measureTipUpGeometry(stroke);
    const double directDistance = std::hypot(stroke.contactX - stroke.lastPointBeforeTipUp.pageX,
                                             stroke.contactY - stroke.lastPointBeforeTipUp.pageY);
    const double geometricTolerance = stroke.inputStep * 0.02;

    EXPECT_LE(geometry.arcLength, directDistance + geometricTolerance);
    EXPECT_LE(geometry.backwardsTravel, geometricTolerance);
    EXPECT_LE(geometry.maximumOvershoot, geometricTolerance);
    EXPECT_LE(geometry.endpointError, geometricTolerance);
}

TEST(InkStrokeModelerAdapterTest, ReleaseTimestampGapDoesNotChangeFastTipUpGeometry) {
    const FinishedStroke immediate = runFastStrokeToTipUp(0, true);
    const FinishedStroke oneSampleLater = runFastStrokeToTipUp(8, true);
    const FinishedStroke severalSamplesLater = runFastStrokeToTipUp(32, true);
    ASSERT_FALSE(immediate.tipUpPoints.empty());
    ASSERT_FALSE(oneSampleLater.tipUpPoints.empty());
    ASSERT_FALSE(severalSamplesLater.tipUpPoints.empty());

    const TipUpGeometry reference = measureTipUpGeometry(immediate);
    const double geometricTolerance = immediate.inputStep * 0.02;
    for (const FinishedStroke* stroke: {&oneSampleLater, &severalSamplesLater}) {
        const TipUpGeometry geometry = measureTipUpGeometry(*stroke);
        EXPECT_NEAR(geometry.arcLength, reference.arcLength, geometricTolerance);
        EXPECT_NEAR(geometry.endpointError, reference.endpointError, geometricTolerance);
        EXPECT_NEAR(geometry.backwardsTravel, reference.backwardsTravel, geometricTolerance);
        EXPECT_NEAR(geometry.maximumOvershoot, reference.maximumOvershoot, geometricTolerance);
    }
}

TEST(InkStrokeModelerAdapterTest, IgnoresExactDuplicates) {
    InkStrokeModelerAdapter adapter(15.0);
    ASSERT_TRUE(adapter.beginStroke({.screenX = 1.0, .screenY = 2.0, .pressure = 0.4, .timestampMs = 100}, 1.0,
                                    SCREEN_PIXELS_PER_CM));

    const InputSample move{.screenX = 5.0, .screenY = 2.0, .pressure = 0.4, .timestampMs = 108};
    ASSERT_FALSE(adapter.updateStroke(move, 1.0).degradedToRaw);

    const auto& duplicate = adapter.updateStroke(move, 1.0);
    EXPECT_FALSE(duplicate.degradedToRaw);
    EXPECT_FALSE(duplicate.replacePrediction);
    EXPECT_TRUE(duplicate.actual.empty());
}

// Regression: a single out-of-order timestamp used to be read as a ~49-day
// forward jump by the unsigned subtraction, which killed the modeler for the
// rest of the stroke.
TEST(InkStrokeModelerAdapterTest, AbsorbsOutOfOrderTimestampsInsteadOfDegrading) {
    InkStrokeModelerAdapter adapter(15.0);
    ASSERT_TRUE(adapter.beginStroke({.screenX = 0.0, .screenY = 0.0, .pressure = 0.4, .timestampMs = 100}, 1.0,
                                    SCREEN_PIXELS_PER_CM));
    for (std::uint32_t i = 1; i <= 4; ++i) {
        ASSERT_FALSE(
                adapter.updateStroke({.screenX = 4.0 * i, .screenY = 0.0, .pressure = 0.4, .timestampMs = 100 + 8 * i},
                                     1.0)
                        .degradedToRaw);
    }

    const auto& backward =
            adapter.updateStroke({.screenX = 20.0, .screenY = 0.0, .pressure = 0.4, .timestampMs = 131}, 1.0);
    EXPECT_FALSE(backward.degradedToRaw);
    EXPECT_TRUE(adapter.isActive());

    // The stroke keeps being modeled afterwards.
    const auto& next =
            adapter.updateStroke({.screenX = 24.0, .screenY = 0.0, .pressure = 0.4, .timestampMs = 140}, 1.0);
    EXPECT_FALSE(next.degradedToRaw);
    EXPECT_FALSE(next.actual.empty());
    EXPECT_TRUE(adapter.isActive());
}

TEST(InkStrokeModelerAdapterTest, HandlesTheGdkTimestampWraparound) {
    InkStrokeModelerAdapter adapter(15.0);
    ASSERT_TRUE(adapter.beginStroke({.screenX = 0.0,
                                     .screenY = 0.0,
                                     .pressure = 0.6,
                                     .timestampMs = std::numeric_limits<std::uint32_t>::max() - 3},
                                    1.0, SCREEN_PIXELS_PER_CM));

    const auto& frame =
            adapter.updateStroke({.screenX = 4.0, .screenY = 0.0, .pressure = 0.6, .timestampMs = 4}, 1.0);
    EXPECT_FALSE(frame.degradedToRaw);
    EXPECT_TRUE(adapter.isActive());
}

TEST(InkStrokeModelerAdapterTest, KeepsContactPressureOnTipUp) {
    InkStrokeModelerAdapter adapter(15.0, 4.0);
    ASSERT_TRUE(adapter.beginStroke({.screenX = 0.0, .screenY = 0.0, .pressure = 2.4, .timestampMs = 0}, 1.0,
                                    SCREEN_PIXELS_PER_CM));
    for (std::uint32_t i = 1; i <= 8; ++i) {
        ASSERT_FALSE(
                adapter.updateStroke({.screenX = 3.0 * i, .screenY = 0.0, .pressure = 2.4, .timestampMs = 8 * i}, 1.0)
                        .degradedToRaw);
    }

    // PenInputHandler clamps a physical tip-up pressure of zero to the user's
    // minimum pressure (0.05 by default), so the adapter must not trust it.
    const auto& finished =
            adapter.finishStroke({.screenX = 1000.0, .screenY = 500.0, .pressure = 0.05, .timestampMs = 72});
    ASSERT_FALSE(finished.degradedToRaw);
    ASSERT_FALSE(finished.actual.empty());
    EXPECT_TRUE(std::all_of(finished.actual.begin(), finished.actual.end(),
                            [](const auto& point) { return std::isfinite(point.pressure) && point.pressure > 2.0; }));
    EXPECT_LT(finished.actual.back().pageX, 30.0);
    EXPECT_LT(std::abs(finished.actual.back().pageY), 1.0);
    EXPECT_FALSE(adapter.isActive());
}

// The pen rested on the surface before being lifted. The release event carries
// no usable motion, so the sub-stroke is completed at the last accepted input
// rather than being dragged towards a stale coordinate.
TEST(InkStrokeModelerAdapterTest, FlushesConfirmedTailWhenThePenPausedBeforeLifting) {
    InkStrokeModelerAdapter adapter(15.0);
    ASSERT_TRUE(adapter.beginStroke({.screenX = 0.0, .screenY = 0.0, .pressure = 0.5, .timestampMs = 0}, 1.0,
                                    SCREEN_PIXELS_PER_CM));
    for (std::uint32_t i = 1; i <= 8; ++i) {
        ASSERT_FALSE(adapter.updateStroke(
                                    {.screenX = 4.0 * i, .screenY = 2.0 * i, .pressure = 0.5, .timestampMs = 8 * i},
                                    1.0)
                             .degradedToRaw);
    }

    const auto& finished =
            adapter.finishStroke({.screenX = 500.0, .screenY = 500.0, .pressure = 0.05, .timestampMs = 1000});
    EXPECT_FALSE(finished.degradedToRaw);
    EXPECT_FALSE(finished.actual.empty());
    EXPECT_FALSE(adapter.isActive());
    EXPECT_TRUE(std::all_of(finished.actual.begin(), finished.actual.end(), [](const auto& point) {
        return std::isfinite(point.pageX) && std::isfinite(point.pageY) && point.pageX < 40.0 && point.pageY < 20.0;
    }));
}

// Regression: pausing mid-stroke for longer than MAX_INPUT_GAP_SECONDS used to
// disable the modeler for the rest of the stroke, so the ink texture visibly
// switched from smoothed to raw. It is a sub-stroke boundary, not a failure.
TEST(InkStrokeModelerAdapterTest, LongPauseRestartsTheModelInsteadOfDegrading) {
    InkStrokeModelerAdapter adapter(15.0);
    ASSERT_TRUE(adapter.beginStroke({.screenX = 0.0, .screenY = 0.0, .pressure = 0.5, .timestampMs = 0}, 1.0,
                                    SCREEN_PIXELS_PER_CM));
    for (std::uint32_t i = 1; i <= 8; ++i) {
        ASSERT_FALSE(
                adapter.updateStroke({.screenX = 4.0 * i, .screenY = 0.0, .pressure = 0.5, .timestampMs = 8 * i}, 1.0)
                        .degradedToRaw);
    }

    // 600 ms of no input, then the user carries on drawing from where the pen
    // was resting.
    const auto& resumed =
            adapter.updateStroke({.screenX = 36.0, .screenY = 0.0, .pressure = 0.5, .timestampMs = 664}, 1.0);
    EXPECT_FALSE(resumed.degradedToRaw);
    EXPECT_TRUE(adapter.isActive());
    // The tail modeled up to the pause plus the re-seeded contact point.
    EXPECT_FALSE(resumed.actual.empty());

    bool sawPredictionAfterResume = false;
    for (std::uint32_t i = 1; i <= 24; ++i) {
        const auto& frame = adapter.updateStroke(
                {.screenX = 36.0 + 4.0 * i, .screenY = 0.0, .pressure = 0.5, .timestampMs = 664 + 8 * i}, 1.0);
        ASSERT_FALSE(frame.degradedToRaw);
        EXPECT_FALSE(frame.actual.empty());
        sawPredictionAfterResume = sawPredictionAfterResume || !frame.prediction.empty();
    }
    // Smoothing and prediction both survive the pause.
    EXPECT_TRUE(sawPredictionAfterResume);

    const auto& finished =
            adapter.finishStroke({.screenX = 132.0, .screenY = 0.0, .pressure = 0.05, .timestampMs = 856});
    EXPECT_FALSE(finished.degradedToRaw);
    ASSERT_FALSE(finished.actual.empty());
    EXPECT_NEAR(finished.actual.back().pageX, 132.0, 1.0);
}

// Regression: events sharing one millisecond timestamp used to re-arm the
// prediction warm-up counter on every occurrence, so on a device that reports
// faster than 1 kHz (or whose events GDK coalesces) the prediction the user
// enabled never became visible at all.
TEST(InkStrokeModelerAdapterTest, CollapsedMillisecondTimestampsDoNotSuppressPrediction) {
    InkStrokeModelerAdapter adapter(15.0);
    ASSERT_TRUE(adapter.beginStroke({.screenX = 0.0, .screenY = 0.0, .pressure = 0.5, .timestampMs = 0}, 1.0,
                                    SCREEN_PIXELS_PER_CM));

    bool sawPrediction = false;
    double x = 0.0;
    for (std::uint32_t batch = 1; batch <= 40; ++batch) {
        // Two samples reported under the same millisecond, as a coalesced batch.
        for (int inBatch = 0; inBatch < 2; ++inBatch) {
            x += 3.0;
            const auto& frame = adapter.updateStroke(
                    {.screenX = x, .screenY = 0.0, .pressure = 0.5, .timestampMs = 8 * batch}, 1.0);
            ASSERT_FALSE(frame.degradedToRaw);
            sawPrediction = sawPrediction || !frame.prediction.empty();
        }
    }

    EXPECT_TRUE(sawPrediction);
    EXPECT_TRUE(adapter.isActive());
}

// Regression: each collapsed timestamp used to advance model time by a full
// millisecond, so over a long stroke the model believed far more time had
// passed than really had and consistently underestimated the pen speed.
TEST(InkStrokeModelerAdapterTest, ModelTimeNeverDriftsAheadOfRealTime) {
    InkStrokeModelerAdapter adapter(15.0);
    ASSERT_TRUE(adapter.beginStroke({.screenX = 0.0, .screenY = 0.0, .pressure = 0.5, .timestampMs = 0}, 1.0,
                                    SCREEN_PIXELS_PER_CM));

    double x = 0.0;
    for (std::uint32_t batch = 1; batch <= 200; ++batch) {
        const std::uint32_t timestampMs = 8 * batch;
        for (int inBatch = 0; inBatch < 3; ++inBatch) {
            x += 2.0;
            const auto& frame =
                    adapter.updateStroke({.screenX = x, .screenY = 0.0, .pressure = 0.5, .timestampMs = timestampMs},
                                         1.0);
            ASSERT_FALSE(frame.degradedToRaw);
            // Sub-millisecond ordering steps stay inside the millisecond the
            // device reported, so the error is bounded by 1 ms and, crucially,
            // does not accumulate over the 400 collapsed samples above.
            const double realElapsedSeconds = static_cast<double>(timestampMs) / 1000.0;
            for (const auto& point: frame.actual) {
                EXPECT_LE(point.timeSeconds, realElapsedSeconds + 0.001 + 1e-9);
            }
        }
    }
}

TEST(InkStrokeModelerAdapterTest, ConvertsPhysicalCoordinatesAtNonDefaultZoom) {
    InkStrokeModelerAdapter adapter(15.0);
    ASSERT_TRUE(adapter.beginStroke({.screenX = 100.0, .screenY = 50.0, .pressure = 0.5, .timestampMs = 0}, 2.0, 50.0));
    for (std::uint32_t i = 1; i <= 8; ++i) {
        ASSERT_FALSE(adapter.updateStroke({.screenX = 100.0 + 2.5 * i,
                                           .screenY = 50.0,
                                           .pressure = 0.5,
                                           .timestampMs = 8 * i},
                                          2.0)
                             .degradedToRaw);
    }
    const auto& finished =
            adapter.finishStroke({.screenX = -500.0, .screenY = -500.0, .pressure = 0.05, .timestampMs = 72});
    ASSERT_FALSE(finished.degradedToRaw);
    ASSERT_FALSE(finished.actual.empty());
    EXPECT_NEAR(finished.actual.back().pageX, 60.0, 0.1);
    EXPECT_NEAR(finished.actual.back().pageY, 25.0, 0.1);
}

// Regression: the zoom was captured once at button-down, so zooming with the
// pen still down mapped every later modeled point through the stale factor.
TEST(InkStrokeModelerAdapterTest, ReanchorsWhenTheZoomChangesMidStroke) {
    InkStrokeModelerAdapter adapter(15.0);
    ASSERT_TRUE(adapter.beginStroke({.screenX = 100.0, .screenY = 40.0, .pressure = 0.5, .timestampMs = 0}, 1.0, 50.0));
    for (std::uint32_t i = 1; i <= 6; ++i) {
        const auto& frame = adapter.updateStroke(
                {.screenX = 100.0 + 3.0 * i, .screenY = 40.0, .pressure = 0.5, .timestampMs = 8 * i}, 1.0);
        ASSERT_FALSE(frame.degradedToRaw);
        for (const auto& point: frame.actual) {
            EXPECT_NEAR(point.pageY, 40.0, 0.5);
        }
    }

    // The user zooms to 200% without lifting the pen.
    const auto& rezoomed =
            adapter.updateStroke({.screenX = 118.0, .screenY = 40.0, .pressure = 0.5, .timestampMs = 56}, 2.0);
    EXPECT_FALSE(rezoomed.degradedToRaw);
    EXPECT_TRUE(adapter.isActive());

    for (std::uint32_t i = 1; i <= 8; ++i) {
        const auto& frame = adapter.updateStroke(
                {.screenX = 118.0 + 3.0 * i, .screenY = 40.0, .pressure = 0.5, .timestampMs = 56 + 8 * i}, 2.0);
        ASSERT_FALSE(frame.degradedToRaw);
    }

    const auto& finished =
            adapter.finishStroke({.screenX = 142.0, .screenY = 40.0, .pressure = 0.05, .timestampMs = 128});
    ASSERT_FALSE(finished.degradedToRaw);
    ASSERT_FALSE(finished.actual.empty());
    // Page coordinates after the change must use the new zoom, not the old one.
    EXPECT_NEAR(finished.actual.back().pageX, 142.0 / 2.0, 0.5);
    EXPECT_NEAR(finished.actual.back().pageY, 40.0 / 2.0, 0.5);
}

#else

TEST(InkStrokeModelerAdapterTest, FailsOpenWhenTheOptionalBackendIsAbsent) {
    InkStrokeModelerAdapter adapter(15.0);
    EXPECT_FALSE(adapter.beginStroke({}, 1.0, SCREEN_PIXELS_PER_CM));
    EXPECT_TRUE(adapter.updateStroke({}, 1.0).degradedToRaw);
}

#endif

}  // namespace
