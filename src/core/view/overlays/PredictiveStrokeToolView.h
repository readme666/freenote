/*
 * Xournal++
 *
 * Active stroke view with a replaceable prediction tail
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <vector>

#include <cairo.h>

#include "StrokeToolView.h"

namespace xoj::view {

class PredictiveStrokeToolView final: public StrokeToolView {
public:
    PredictiveStrokeToolView(const StrokeHandler* strokeHandler, const Stroke& stroke, Repaintable* parent);
    ~PredictiveStrokeToolView() noexcept override;

    void draw(cairo_t* cr) const override;

    /** Draw only confirmed points when the view is committed to the page buffer. */
    void drawWithoutDrawingAids(cairo_t* cr) const override;

    void on(PredictionReplacementRequest, const std::vector<Point>& prediction) override;
    void on(StrokeReplacementRequest, const Stroke& newStroke) override;
    void deleteOn(CancellationRequest, const Range& rg) override;
    void deleteOn(FinalizationRequest, const Range& rg) override;

private:
    [[nodiscard]] auto predictionRange() const -> Range;
    void drawPrediction(cairo_t* cr) const;
    void replacePrediction(const std::vector<Point>& prediction, bool requestRepaint);

private:
    std::vector<Point> prediction;
};

}  // namespace xoj::view
