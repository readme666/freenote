/*
 * Xournal++
 *
 * Low-latency adaptive filter for pointer input
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>

namespace StrokeStabilizer {

/**
 * @brief Scalar implementation of the 1 Euro filter.
 *
 * The filter uses a low cutoff while the signal moves slowly to suppress
 * jitter, then raises the cutoff with speed to keep latency low. Timestamps
 * are expressed in milliseconds and may wrap around.
 *
 * See Casiez, Roussel and Vogel, "1 Euro Filter: A Simple Speed-based
 * Low-pass Filter for Noisy Input in Interactive Systems", CHI 2012.
 */
class OneEuroFilter {
public:
    OneEuroFilter(double minCutoff, double beta, double derivativeCutoff = 1.0,
                  double initialSamplingFrequency = 120.0);

    /**
     * @brief Reset all filter history to an exact sample.
     */
    void reset(double value, std::uint32_t timestamp);

    /**
     * @brief Filter a sample and return its stabilized value.
     */
    auto filter(double value, std::uint32_t timestamp) -> double;

private:
    class LowPass {
    public:
        void reset(double value);
        auto filter(double value, double alpha) -> double;
        auto value() const -> double;

    private:
        double filteredValue{};
    };

    static auto smoothingFactor(double cutoff, double deltaSeconds) -> double;
    auto deltaSeconds(std::uint32_t timestamp) -> double;

    double minCutoff;
    double beta;
    double derivativeCutoff;
    double fallbackDeltaSeconds;
    std::uint32_t previousTimestamp{};
    bool initialized{false};
    LowPass signal;
    LowPass derivative;
};

/**
 * @brief Rotation-invariant 2D implementation of the 1 Euro filter.
 *
 * Both axes share a cutoff derived from the magnitude of the filtered 2D
 * velocity. This avoids adding more lag to diagonal strokes than to horizontal
 * or vertical strokes with the same physical speed.
 */
class OneEuroFilter2D {
public:
    struct Sample {
        double x{};
        double y{};
    };

    OneEuroFilter2D(double minCutoff, double beta, double derivativeCutoff = 1.0,
                    double initialSamplingFrequency = 120.0);

    void reset(Sample value, std::uint32_t timestamp);
    auto filter(Sample value, std::uint32_t timestamp) -> Sample;

private:
    class LowPass {
    public:
        void reset(Sample value);
        auto filter(Sample value, double alpha) -> Sample;
        auto value() const -> Sample;

    private:
        Sample filteredValue;
    };

    static auto smoothingFactor(double cutoff, double deltaSeconds) -> double;
    auto deltaSeconds(std::uint32_t timestamp) -> double;

    double minCutoff;
    double beta;
    double derivativeCutoff;
    double fallbackDeltaSeconds;
    std::uint32_t previousTimestamp{};
    bool initialized{false};
    LowPass signal;
    LowPass derivative;
};

}  // namespace StrokeStabilizer
