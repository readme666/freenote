#include "OneEuroFilter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace {
constexpr double DEFAULT_MIN_CUTOFF = 1.0;
constexpr double DEFAULT_DERIVATIVE_CUTOFF = 1.0;
constexpr double DEFAULT_SAMPLING_FREQUENCY = 120.0;
constexpr std::uint32_t MAX_CADENCE_UPDATE_GAP_MS = 100;
// GDK timestamps are unsigned milliseconds. Under modular arithmetic, a
// difference in the lower half of the range is forward (including a wrap),
// while a difference in the upper half is a backwards/out-of-order sample.
constexpr std::uint32_t MAX_FORWARD_EVENT_GAP_MS = std::numeric_limits<std::uint32_t>::max() / 2;

auto positiveOr(double value, double fallback) -> double {
    return std::isfinite(value) && value > 0.0 ? value : fallback;
}
}  // namespace

namespace StrokeStabilizer {

OneEuroFilter::OneEuroFilter(double minCutoff, double beta, double derivativeCutoff, double initialSamplingFrequency):
        minCutoff(positiveOr(minCutoff, DEFAULT_MIN_CUTOFF)),
        beta(std::isfinite(beta) ? std::max(0.0, beta) : 0.0),
        derivativeCutoff(positiveOr(derivativeCutoff, DEFAULT_DERIVATIVE_CUTOFF)),
        fallbackDeltaSeconds(1.0 / positiveOr(initialSamplingFrequency, DEFAULT_SAMPLING_FREQUENCY)) {}

void OneEuroFilter::LowPass::reset(double value) { this->filteredValue = value; }

auto OneEuroFilter::LowPass::filter(double value, double alpha) -> double {
    this->filteredValue += alpha * (value - this->filteredValue);
    return this->filteredValue;
}

auto OneEuroFilter::LowPass::value() const -> double { return this->filteredValue; }

auto OneEuroFilter::smoothingFactor(double cutoff, double deltaSeconds) -> double {
    const double timeConstant = 1.0 / (2.0 * std::numbers::pi * cutoff);
    return 1.0 / (1.0 + timeConstant / deltaSeconds);
}

auto OneEuroFilter::deltaSeconds(std::uint32_t timestamp) -> double {
    // Unsigned subtraction deliberately supports the regular guint32/GDK
    // timestamp wraparound. A backwards or otherwise implausible timestamp
    // produces a very large value and falls back to the last known cadence.
    const std::uint32_t elapsedMs = timestamp - this->previousTimestamp;

    if (elapsedMs == 0 || elapsedMs > MAX_FORWARD_EVENT_GAP_MS) {
        return this->fallbackDeltaSeconds;
    }

    this->previousTimestamp = timestamp;
    const double elapsedSeconds = static_cast<double>(elapsedMs) / 1000.0;
    if (elapsedMs <= MAX_CADENCE_UPDATE_GAP_MS) {
        this->fallbackDeltaSeconds = elapsedSeconds;
    }
    return elapsedSeconds;
}

void OneEuroFilter::reset(double value, std::uint32_t timestamp) {
    this->previousTimestamp = timestamp;
    this->signal.reset(value);
    this->derivative.reset(0.0);
    this->initialized = true;
}

auto OneEuroFilter::filter(double value, std::uint32_t timestamp) -> double {
    if (!std::isfinite(value)) {
        return this->initialized ? this->signal.value() : value;
    }

    if (!this->initialized) {
        this->reset(value, timestamp);
        return value;
    }

    const double elapsed = this->deltaSeconds(timestamp);
    // Measuring against the filtered signal (rather than the previous noisy
    // sample) prevents alternating digitizer noise from cancelling out the
    // estimated movement speed.
    const double rawDerivative = (value - this->signal.value()) / elapsed;

    const double filteredDerivative =
            this->derivative.filter(rawDerivative, smoothingFactor(this->derivativeCutoff, elapsed));
    const double cutoff = this->minCutoff + this->beta * std::abs(filteredDerivative);
    return this->signal.filter(value, smoothingFactor(cutoff, elapsed));
}

OneEuroFilter2D::OneEuroFilter2D(double minCutoff, double beta, double derivativeCutoff,
                                 double initialSamplingFrequency):
        minCutoff(positiveOr(minCutoff, DEFAULT_MIN_CUTOFF)),
        beta(std::isfinite(beta) ? std::max(0.0, beta) : 0.0),
        derivativeCutoff(positiveOr(derivativeCutoff, DEFAULT_DERIVATIVE_CUTOFF)),
        fallbackDeltaSeconds(1.0 / positiveOr(initialSamplingFrequency, DEFAULT_SAMPLING_FREQUENCY)) {}

void OneEuroFilter2D::LowPass::reset(Sample value) { this->filteredValue = value; }

auto OneEuroFilter2D::LowPass::filter(Sample value, double alpha) -> Sample {
    this->filteredValue.x += alpha * (value.x - this->filteredValue.x);
    this->filteredValue.y += alpha * (value.y - this->filteredValue.y);
    return this->filteredValue;
}

auto OneEuroFilter2D::LowPass::value() const -> Sample { return this->filteredValue; }

auto OneEuroFilter2D::smoothingFactor(double cutoff, double deltaSeconds) -> double {
    const double timeConstant = 1.0 / (2.0 * std::numbers::pi * cutoff);
    return 1.0 / (1.0 + timeConstant / deltaSeconds);
}

auto OneEuroFilter2D::deltaSeconds(std::uint32_t timestamp) -> double {
    const std::uint32_t elapsedMs = timestamp - this->previousTimestamp;
    if (elapsedMs == 0 || elapsedMs > MAX_FORWARD_EVENT_GAP_MS) {
        return this->fallbackDeltaSeconds;
    }

    this->previousTimestamp = timestamp;
    const double elapsedSeconds = static_cast<double>(elapsedMs) / 1000.0;
    if (elapsedMs <= MAX_CADENCE_UPDATE_GAP_MS) {
        this->fallbackDeltaSeconds = elapsedSeconds;
    }
    return elapsedSeconds;
}

void OneEuroFilter2D::reset(Sample value, std::uint32_t timestamp) {
    this->previousTimestamp = timestamp;
    this->signal.reset(value);
    this->derivative.reset({});
    this->initialized = true;
}

auto OneEuroFilter2D::filter(Sample value, std::uint32_t timestamp) -> Sample {
    if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
        return this->initialized ? this->signal.value() : value;
    }

    if (!this->initialized) {
        this->reset(value, timestamp);
        return value;
    }

    const double elapsed = this->deltaSeconds(timestamp);
    const Sample previous = this->signal.value();
    const Sample rawDerivative{(value.x - previous.x) / elapsed, (value.y - previous.y) / elapsed};
    const Sample filteredDerivative =
            this->derivative.filter(rawDerivative, smoothingFactor(this->derivativeCutoff, elapsed));
    const double speed = std::hypot(filteredDerivative.x, filteredDerivative.y);
    const double cutoff = this->minCutoff + this->beta * speed;
    return this->signal.filter(value, smoothingFactor(cutoff, elapsed));
}

}  // namespace StrokeStabilizer
