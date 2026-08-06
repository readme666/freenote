#include "InkStrokeModelerAdapter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

#include <glib.h>

#ifdef XOURNALPP_ENABLE_INK_STROKE_MODELER
#include <absl/status/status.h>
#include <ink_stroke_modeler/params.h>
#include <ink_stroke_modeler/stroke_modeler.h>
#include <ink_stroke_modeler/types.h>
#endif

namespace StrokePrediction {
namespace {

constexpr double MAX_INPUT_GAP_SECONDS = 0.5;
constexpr double PREDICTION_SUPPRESSION_GAP_SECONDS = 0.075;
constexpr int PREDICTION_WARMUP_SAMPLES = 4;
constexpr double MAX_VISIBLE_LEAD_SECONDS = 0.020;
constexpr double MAX_VISIBLE_TAIL_CM = 1.0;
/// Prediction is only rendered at the display cadence, so recomputing it for
/// every input event (up to 250 Hz) runs the Kalman predictor several times
/// per visible frame. Recompute at most every 8 ms; stale predictions are
/// bounded to one input interval and re-anchored by the view's repaint.
constexpr double PREDICTION_MIN_INTERVAL_SECONDS = 0.008;

/**
 * GDK timestamps are unsigned milliseconds which wrap around. A small unsigned
 * difference is a genuine forward step (including across the wrap), while a
 * difference in the top half of the range is an event that arrived out of
 * order. Same threshold as StrokeStabilizer::OneEuroFilter.
 */
constexpr std::uint32_t BACKWARDS_TIMESTAMP_THRESHOLD_MS = std::numeric_limits<std::uint32_t>::max() / 2;

/**
 * Several input events routinely share one millisecond timestamp, either
 * because the device samples faster than 1 kHz or because GDK coalesced them.
 * The model requires strictly increasing times, so such samples are spread
 * inside the millisecond they belong to: the k-th extra sample is placed at
 * k/(k+1) of it. That is strictly increasing, never reaches the next
 * millisecond, and splits the interval evenly for the common case of a single
 * extra sample -- placing it at 1/2 ms rather than immediately after its
 * predecessor, which would otherwise read as a near-infinite velocity spike.
 * Model time is recomputed from the accumulated millisecond count on every
 * genuine forward step, so these steps refine the ordering without ever letting
 * model time drift ahead of real time.
 */
constexpr double MILLISECOND_SECONDS = 0.001;
constexpr int MAX_COALESCED_SUB_STEPS = 15;

/** Outcome of mapping a device timestamp onto the model's time axis. */
enum class TimeStep {
    Accepted,  ///< model time moved forward, possibly only within the current millisecond
    Ignore,    ///< no usable time left in this millisecond; treat the sample as a duplicate
    Restart,   ///< the gap is too long to model as one continuous motion
};

#ifdef XOURNALPP_ENABLE_INK_STROKE_MODELER
bool isFiniteSample(const InputSample& sample) {
    return std::isfinite(sample.screenX) && std::isfinite(sample.screenY) && std::isfinite(sample.pressure);
}

double normalizedPressure(double pressure, double fallback) {
    if (std::isfinite(pressure) && pressure >= 0.0) {
        return std::clamp(pressure, 0.0, 1.0);
    }
    return fallback;
}
#endif

}  // namespace

struct InkStrokeModelerAdapter::Impl {
    Impl(double intervalMs, double pressureScale):
            predictionIntervalSeconds(std::clamp(intervalMs, 4.0, 20.0) / 1000.0),
            pressureScale(std::isfinite(pressureScale) && pressureScale > 0.0 ? pressureScale : 1.0) {}

    double predictionIntervalSeconds;
    double pressureScale;
    InkFrame frame;
    bool active{false};
    bool warned{false};
    double zoom{1.0};
    double screenPixelsPerCm{96.0 / 2.54};
    double originScreenX{};
    double originScreenY{};
    double inputTimeSeconds{};
    std::uint32_t lastTimestampMs{};
    /// Milliseconds accumulated from genuine forward steps since the stroke (re)started.
    std::uint64_t elapsedMs{};
    /// Sub-millisecond steps already handed out for the current millisecond.
    int coalescedSubSteps{};
    InputSample lastSample{};
    bool haveLastSample{false};
    double lastPressure{-1.0};
    InputSample lastSuccessfulSample{};
    double lastSuccessfulInputTimeSeconds{};
    double lastSuccessfulPressure{-1.0};
    bool haveSuccessfulSample{false};
    int predictionWarmupSamples{};
    /// Model time of the last successful Predict(), used to throttle the
    /// Kalman run to the display cadence. -1 means "no prediction yet".
    double lastPredictionTimeSeconds{-1.0};

#ifdef XOURNALPP_ENABLE_INK_STROKE_MODELER
    ink::stroke_model::StrokeModeler modeler;
    std::vector<ink::stroke_model::Result> modelResults;
    std::vector<ink::stroke_model::Result> predictedResults;

    auto makeParams() const -> ink::stroke_model::StrokeModelParams {
        using ink::stroke_model::Duration;
        using ink::stroke_model::KalmanPredictorParams;
        using ink::stroke_model::StrokeModelParams;

        return StrokeModelParams{
                .wobble_smoother_params{
                        .is_enabled = true, .timeout = Duration(0.040), .speed_floor = 1.31F, .speed_ceiling = 1.44F},
                .position_modeler_params{.spring_mass_constant = 11.0F / 32400.0F, .drag_constant = 72.0F},
                .sampling_params{.min_output_rate = 180.0,
                                 .end_of_stroke_stopping_distance = 0.001F,
                                 .end_of_stroke_max_iterations = 20,
                                 .max_outputs_per_call = 256},
                .stylus_state_modeler_params{.use_stroke_normal_projection = false},
                .prediction_params = KalmanPredictorParams{.process_noise = 0.00026458,
                                                           .measurement_noise = 0.026458,
                                                           .min_stable_iteration = 4,
                                                           .max_time_samples = 20,
                                                           .min_catchup_velocity = 0.01F,
                                                           .acceleration_weight = 0.5F,
                                                           .jerk_weight = 0.1F,
                                                           .prediction_interval = Duration(predictionIntervalSeconds),
                                                           .confidence_params{.desired_number_of_samples = 10,
                                                                              .max_estimation_distance = 0.04F,
                                                                              .min_travel_speed = 3.0F,
                                                                              .max_travel_speed = 15.0F,
                                                                              .max_linear_deviation = 0.2F,
                                                                              .baseline_linearity_confidence = 0.4F}}};
    }

    auto toModelPosition(const InputSample& sample) const -> ink::stroke_model::Vec2 {
        return {.x = static_cast<float>((sample.screenX - originScreenX) / screenPixelsPerCm),
                .y = static_cast<float>((sample.screenY - originScreenY) / screenPixelsPerCm)};
    }

    auto toInput(const InputSample& sample, ink::stroke_model::Input::EventType eventType) -> ink::stroke_model::Input {
        const double modelPressure = sample.pressure < 0.0 ? sample.pressure : sample.pressure / pressureScale;
        const double pressure = normalizedPressure(modelPressure, lastPressure);
        if (pressure >= 0.0) {
            lastPressure = pressure;
        }
        return {.event_type = eventType,
                .position = toModelPosition(sample),
                .time = ink::stroke_model::Time(inputTimeSeconds),
                .pressure = static_cast<float>(pressure),
                .tilt = -1.0F,
                .orientation = -1.0F};
    }

    auto toPagePoint(const ink::stroke_model::Result& result) const -> ModeledPoint {
        const double pressure = normalizedPressure(result.pressure, lastPressure);
        return {.pageX = (originScreenX + static_cast<double>(result.position.x) * screenPixelsPerCm) / zoom,
                .pageY = (originScreenY + static_cast<double>(result.position.y) * screenPixelsPerCm) / zoom,
                .pressure = pressure < 0.0 ? pressure : pressure * pressureScale,
                .timeSeconds = result.time.Value()};
    }

    void copyActualResults() {
        frame.actual.reserve(modelResults.size());
        for (const auto& result: modelResults) {
            ModeledPoint point = toPagePoint(result);
            if (std::isfinite(point.pageX) && std::isfinite(point.pageY) && std::isfinite(point.timeSeconds)) {
                frame.actual.emplace_back(point);
            }
        }
    }

    void copyPredictionResults() {
        if (predictionWarmupSamples > 0 || predictedResults.empty()) {
            return;
        }

        ink::stroke_model::Vec2 anchor =
                modelResults.empty() ? toModelPosition(lastSample) : modelResults.back().position;
        frame.prediction.reserve(predictedResults.size());
        for (const auto& result: predictedResults) {
            if (result.time.Value() > inputTimeSeconds + MAX_VISIBLE_LEAD_SECONDS) {
                break;
            }
            const double dx = static_cast<double>(result.position.x - anchor.x);
            const double dy = static_cast<double>(result.position.y - anchor.y);
            if (std::hypot(dx, dy) > MAX_VISIBLE_TAIL_CM) {
                break;
            }
            ModeledPoint point = toPagePoint(result);
            if (!std::isfinite(point.pageX) || !std::isfinite(point.pageY) || !std::isfinite(point.timeSeconds)) {
                frame.prediction.clear();
                return;
            }
            frame.prediction.emplace_back(point);
        }
    }

    void warnOnce(const std::string& message) {
        if (warned) {
            return;
        }
        warned = true;
        // A StrokeHandler, and with it this adapter, is constructed for every
        // stroke, so a per-instance latch alone would still emit one warning per
        // stroke on a persistently misbehaving device. Report the first
        // occurrence per session at warning level and demote the rest.
        static bool reportedThisSession = false;
        if (reportedThisSession) {
            g_debug("Google Ink Stroke Modeler disabled for the current stroke: %s", message.c_str());
            return;
        }
        reportedThisSession = true;
        g_warning("Google Ink Stroke Modeler disabled for the current stroke: %s", message.c_str());
    }

    void recordSuccessfulInput(const InputSample& sample) {
        lastSuccessfulSample = sample;
        lastSuccessfulInputTimeSeconds = inputTimeSeconds;
        lastSuccessfulPressure = lastPressure;
        haveSuccessfulSample = true;
    }

    // Complete the spring model at the last input it actually accepted and
    // append the modeled tail to the current frame. This preserves the modeled
    // bend that was still catching up instead of jumping straight from an older
    // confirmed point.
    //
    // Two ink-stroke-modeler contracts are relied upon here: a failed Update()
    // leaves the modeler state untouched, and an input whose time equals the
    // last accepted input time is valid. Both hold for the vendored version. A
    // failure is not fatal because every caller is already on a recovery path,
    // but it is logged so the missing tail can be explained.
    void flushModelTail() {
        if (!haveSuccessfulSample) {
            return;
        }

        modelResults.clear();
        const double pendingInputTimeSeconds = inputTimeSeconds;
        inputTimeSeconds = lastSuccessfulInputTimeSeconds;
        lastPressure = lastSuccessfulPressure;
        InputSample releaseSample = lastSuccessfulSample;
        if (lastSuccessfulPressure >= 0.0) {
            releaseSample.pressure = lastSuccessfulPressure * pressureScale;
        }
        auto input = toInput(releaseSample, ink::stroke_model::Input::EventType::kUp);
        if (absl::Status status = modeler.Update(input, modelResults); status.ok()) {
            copyActualResults();
        } else {
            g_debug("Google Ink Stroke Modeler: could not flush the modeled tail: %s", status.ToString().c_str());
        }
        inputTimeSeconds = pendingInputTimeSeconds;
    }

    void flushForRawFallback() {
        if (active) {
            flushModelTail();
        }
        active = false;
    }

    // A long input pause is a sub-stroke boundary, not a failure: the velocity
    // estimate is stale but the user is still drawing the same stroke. Finish
    // the previous segment and re-seed the model at the current sample so the
    // rest of the stroke keeps its smoothing and prediction. The same path
    // re-anchors the screen-to-page mapping when the zoom changed mid-stroke.
    bool restartAt(const InputSample& sample, double newZoom) {
        if (!active) {
            return false;
        }
        flushModelTail();

        if (absl::Status status = modeler.Reset(makeParams()); !status.ok()) {
            warnOnce(status.ToString());
            active = false;
            return false;
        }

        setZoom(newZoom);
        originScreenX = sample.screenX;
        originScreenY = sample.screenY;
        lastTimestampMs = sample.timestampMs;
        elapsedMs = 0;
        coalescedSubSteps = 0;
        inputTimeSeconds = 0.0;
        lastPressure = -1.0;
        haveSuccessfulSample = false;
        lastSample = sample;
        haveLastSample = true;
        // The Kalman predictor needs samples before its estimate is meaningful.
        predictionWarmupSamples = PREDICTION_WARMUP_SAMPLES;
        lastPredictionTimeSeconds = -1.0;

        auto input = toInput(sample, ink::stroke_model::Input::EventType::kDown);
        // flushModelTail() left its own output here and already copied it into
        // the frame; only the kDown results should be copied below.
        modelResults.clear();
        if (absl::Status status = modeler.Update(input, modelResults); !status.ok()) {
            warnOnce(status.ToString());
            active = false;
            return false;
        }
        recordSuccessfulInput(sample);
        copyActualResults();
        return true;
    }
#endif

    void setZoom(double newZoom) {
        if (std::isfinite(newZoom) && newZoom > 0.0) {
            zoom = newZoom;
        }
    }

    void clearFrame() {
        frame.actual.clear();
        frame.prediction.clear();
        frame.replacePrediction = true;
        frame.degradedToRaw = false;
#ifdef XOURNALPP_ENABLE_INK_STROKE_MODELER
        modelResults.clear();
        predictedResults.clear();
#endif
    }

    TimeStep advanceTime(const InputSample& sample) {
        if (!haveLastSample) {
            lastTimestampMs = sample.timestampMs;
            elapsedMs = 0;
            coalescedSubSteps = 0;
            inputTimeSeconds = 0.0;
            return TimeStep::Accepted;
        }

        const std::uint32_t rawDelta = sample.timestampMs - lastTimestampMs;

        if (rawDelta == 0 || rawDelta > BACKWARDS_TIMESTAMP_THRESHOLD_MS) {
            // Either the same millisecond or an out-of-order event. Both carry
            // real positional information, so nudge model time forward inside
            // the current millisecond instead of discarding the sample.
            if (coalescedSubSteps >= MAX_COALESCED_SUB_STEPS) {
                return TimeStep::Ignore;
            }
            ++coalescedSubSteps;
            const double fraction =
                    static_cast<double>(coalescedSubSteps) / static_cast<double>(coalescedSubSteps + 1);
            inputTimeSeconds = static_cast<double>(elapsedMs) * MILLISECOND_SECONDS + fraction * MILLISECOND_SECONDS;
            return TimeStep::Accepted;
        }

        const double deltaSeconds = static_cast<double>(rawDelta) / 1000.0;
        if (deltaSeconds > MAX_INPUT_GAP_SECONDS) {
            return TimeStep::Restart;
        }

        elapsedMs += rawDelta;
        coalescedSubSteps = 0;
        // Derive model time from the accumulated millisecond count rather than
        // summing doubles, so the sub-millisecond steps above can never push
        // model time past the real elapsed time.
        inputTimeSeconds = static_cast<double>(elapsedMs) / 1000.0;
        lastTimestampMs = sample.timestampMs;
        if (deltaSeconds > PREDICTION_SUPPRESSION_GAP_SECONDS) {
            predictionWarmupSamples = PREDICTION_WARMUP_SAMPLES;
        }
        return TimeStep::Accepted;
    }

    bool isDuplicate(const InputSample& sample) const {
        return haveLastSample && sample.timestampMs == lastSample.timestampMs && sample.screenX == lastSample.screenX &&
               sample.screenY == lastSample.screenY && sample.pressure == lastSample.pressure;
    }
};

InkStrokeModelerAdapter::InkStrokeModelerAdapter(double predictionIntervalMs, double pressureScale):
        impl(std::make_unique<Impl>(predictionIntervalMs, pressureScale)) {}

InkStrokeModelerAdapter::~InkStrokeModelerAdapter() = default;

bool InkStrokeModelerAdapter::isCompiledIn() {
#ifdef XOURNALPP_ENABLE_INK_STROKE_MODELER
    return true;
#else
    return false;
#endif
}

bool InkStrokeModelerAdapter::beginStroke(const InputSample& sample, double zoom, double screenPixelsPerCm) {
    impl->clearFrame();
    impl->active = false;
    impl->haveLastSample = false;
    impl->lastPressure = -1.0;
    impl->haveSuccessfulSample = false;
    impl->predictionWarmupSamples = 0;
    impl->lastPredictionTimeSeconds = -1.0;
    impl->elapsedMs = 0;
    impl->coalescedSubSteps = 0;

#ifdef XOURNALPP_ENABLE_INK_STROKE_MODELER
    if (!isFiniteSample(sample) || !std::isfinite(zoom) || zoom <= 0.0 || !std::isfinite(screenPixelsPerCm) ||
        screenPixelsPerCm <= 0.0) {
        impl->warnOnce("invalid initial input or display scale");
        return false;
    }

    impl->zoom = zoom;
    impl->screenPixelsPerCm = screenPixelsPerCm;
    impl->originScreenX = sample.screenX;
    impl->originScreenY = sample.screenY;
    impl->lastSample = sample;
    impl->lastTimestampMs = sample.timestampMs;
    impl->inputTimeSeconds = 0.0;
    impl->haveLastSample = true;

    if (absl::Status status = impl->modeler.Reset(impl->makeParams()); !status.ok()) {
        impl->warnOnce(status.ToString());
        return false;
    }

    auto input = impl->toInput(sample, ink::stroke_model::Input::EventType::kDown);
    if (absl::Status status = impl->modeler.Update(input, impl->modelResults); !status.ok()) {
        impl->warnOnce(status.ToString());
        return false;
    }
    // The kDown output is deliberately dropped: StrokeHandler::onButtonPressEvent
    // already seeded the stroke with this very position from the raw event, so
    // copying it here would only produce a zero-length duplicate segment.
    impl->modelResults.clear();
    impl->recordSuccessfulInput(sample);
    impl->active = true;
    return true;
#else
    (void)sample;
    (void)zoom;
    (void)screenPixelsPerCm;
    return false;
#endif
}

const InkFrame& InkStrokeModelerAdapter::updateStroke(const InputSample& sample, double zoom) {
    impl->clearFrame();
#ifdef XOURNALPP_ENABLE_INK_STROKE_MODELER
    if (!impl->active) {
        impl->frame.degradedToRaw = true;
        return impl->frame;
    }
    if (!isFiniteSample(sample)) {
        impl->warnOnce("non-finite input");
        impl->flushForRawFallback();
        impl->frame.degradedToRaw = true;
        return impl->frame;
    }
    if (impl->isDuplicate(sample)) {
        impl->frame.replacePrediction = false;
        return impl->frame;
    }

    // If the zoom changed since beginStroke, the page-space coordinates would
    // be wrong for everything already confirmed. Restart the model so the
    // mapping uses the current zoom from the new sub-stroke origin onwards.
    if (std::isfinite(zoom) && zoom > 0.0 && zoom != impl->zoom) {
        if (!impl->restartAt(sample, zoom)) {
            impl->frame.degradedToRaw = true;
        }
        return impl->frame;
    }

    const TimeStep step = impl->advanceTime(sample);
    if (step == TimeStep::Ignore) {
        impl->frame.replacePrediction = false;
        return impl->frame;
    }
    if (step == TimeStep::Restart) {
        // A long pause resets the velocity estimate but does not degrade the
        // stroke to raw. Finish the current sub-stroke and re-seed the model.
        if (!impl->restartAt(sample, impl->zoom)) {
            impl->frame.degradedToRaw = true;
        }
        return impl->frame;
    }

    impl->lastSample = sample;
    impl->haveLastSample = true;
    auto input = impl->toInput(sample, ink::stroke_model::Input::EventType::kMove);
    if (absl::Status status = impl->modeler.Update(input, impl->modelResults); !status.ok()) {
        impl->warnOnce(status.ToString());
        impl->flushForRawFallback();
        impl->frame.degradedToRaw = true;
        return impl->frame;
    }
    impl->recordSuccessfulInput(sample);
    impl->copyActualResults();

    if (impl->predictionWarmupSamples > 0) {
        --impl->predictionWarmupSamples;
        return impl->frame;
    }
    if (impl->inputTimeSeconds - impl->lastPredictionTimeSeconds < PREDICTION_MIN_INTERVAL_SECONDS) {
        // Keep the previous prediction until the next display cadence tick.
        impl->frame.replacePrediction = false;
        return impl->frame;
    }
    if (absl::Status status = impl->modeler.Predict(impl->predictedResults); !status.ok()) {
        impl->frame.prediction.clear();
        return impl->frame;
    }
    impl->lastPredictionTimeSeconds = impl->inputTimeSeconds;
    impl->copyPredictionResults();
#else
    (void)sample;
    (void)zoom;
    impl->frame.degradedToRaw = true;
#endif
    return impl->frame;
}

const InkFrame& InkStrokeModelerAdapter::finishStroke(const InputSample& sample) {
    impl->clearFrame();
#ifdef XOURNALPP_ENABLE_INK_STROKE_MODELER
    if (!impl->active) {
        impl->frame.degradedToRaw = true;
        return impl->frame;
    }
    if (!isFiniteSample(sample)) {
        impl->warnOnce("non-finite tip-up input");
        impl->flushForRawFallback();
        impl->frame.degradedToRaw = true;
        return impl->frame;
    }
    if (const TimeStep step = impl->advanceTime(sample); step == TimeStep::Restart) {
        // The pen paused before lifting. There is nothing left to model for
        // this sub-stroke, so just complete it at the last accepted input.
        impl->flushForRawFallback();
        return impl->frame;
    }

    // GDK commonly reports zero pressure for the tip-up event. Feeding that
    // zero into the stylus interpolator creates an artificial needle-thin
    // ending, so keep the last in-contact pressure for kUp.
    InputSample releaseSample = impl->lastSuccessfulSample;
    if (impl->lastPressure >= 0.0) {
        releaseSample.pressure = impl->lastPressure * impl->pressureScale;
    }

    // The release coordinates are deliberately replaced with the last
    // in-contact sample. Its model time must match that position as well.
    // Advancing a stationary kUp anchor to the later GDK release time lets the
    // spring continue with residual velocity before it is pulled back to the
    // endpoint, which can create a hooked terminal segment. The release time
    // above is still validated so a stalled input sequence fails open.
    impl->inputTimeSeconds = impl->lastSuccessfulInputTimeSeconds;

    impl->lastSample = releaseSample;
    impl->haveLastSample = true;
    auto input = impl->toInput(releaseSample, ink::stroke_model::Input::EventType::kUp);
    if (absl::Status status = impl->modeler.Update(input, impl->modelResults); !status.ok()) {
        impl->warnOnce(status.ToString());
        impl->flushForRawFallback();
        impl->frame.degradedToRaw = true;
    } else {
        impl->copyActualResults();
        impl->active = false;
    }
#else
    (void)sample;
    impl->frame.degradedToRaw = true;
#endif
    return impl->frame;
}

void InkStrokeModelerAdapter::cancelStroke() noexcept {
    impl->clearFrame();
    impl->active = false;
    impl->haveLastSample = false;
    impl->haveSuccessfulSample = false;
    impl->elapsedMs = 0;
    impl->coalescedSubSteps = 0;
}

bool InkStrokeModelerAdapter::isActive() const noexcept { return impl->active; }

}  // namespace StrokePrediction
