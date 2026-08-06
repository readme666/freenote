/*
 * Xournal++
 *
 * Adapter for Google Ink Stroke Modeler
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace StrokePrediction {

struct InputSample {
    double screenX{};
    double screenY{};
    /// Xournal++ filtered pressure; may exceed one when the user applies a multiplier.
    double pressure{-1.0};
    std::uint32_t timestampMs{};
};

struct ModeledPoint {
    double pageX{};
    double pageY{};
    /// Pressure restored to Xournal++'s filtered/display scale.
    double pressure{-1.0};
    double timeSeconds{};
};

struct InkFrame {
    std::vector<ModeledPoint> actual;
    std::vector<ModeledPoint> prediction;
    bool replacePrediction{true};
    bool degradedToRaw{false};
};

/**
 * Bridges Xournal++ input coordinates and Google Ink Stroke Modeler.
 *
 * Update results are final and may be stored in the document. Prediction
 * results are transient: each frame replaces the previous prediction in full.
 *
 * The model works in screen physical centimetres, which is the space Google's
 * default tuning constants (travel speeds, estimation distances) are expressed
 * in. Screen pixels are already a physical unit, so that conversion needs the
 * display resolution only. Converting the modeled result back to page
 * coordinates does need the zoom level, and the zoom may change mid-stroke,
 * hence updateStroke() takes the live value.
 */
class InkStrokeModelerAdapter final {
public:
    /** pressureScale maps Xournal++ pressure to Google's required [0, 1] interval. */
    explicit InkStrokeModelerAdapter(double predictionIntervalMs, double pressureScale = 1.0);
    ~InkStrokeModelerAdapter();

    InkStrokeModelerAdapter(const InkStrokeModelerAdapter&) = delete;
    auto operator=(const InkStrokeModelerAdapter&) -> InkStrokeModelerAdapter& = delete;

    static bool isCompiledIn();

    /**
     * @param zoom current zoom level
     * @param screenPixelsPerCm screen pixels per physical centimetre (zoom independent)
     */
    bool beginStroke(const InputSample& sample, double zoom, double screenPixelsPerCm);

    /**
     * Feed one input sample.
     *
     * The returned reference points into the adapter and is only valid until the
     * next call on this instance. Consume or copy it before calling again.
     */
    const InkFrame& updateStroke(const InputSample& sample, double zoom);

    /** Same reference lifetime as updateStroke(). */
    const InkFrame& finishStroke(const InputSample& sample);
    void cancelStroke() noexcept;

    [[nodiscard]] bool isActive() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace StrokePrediction
