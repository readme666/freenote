#include "PredictiveStrokeToolView.h"

#include <algorithm>
#include <cmath>

#include "model/Point.h"
#include "model/Stroke.h"
#include "util/Assert.h"
#include "util/Color.h"
#include "util/Range.h"
#include "util/raii/CairoWrappers.h"
#include "view/Repaintable.h"
#include "view/StrokeViewHelper.h"

namespace xoj::view {

PredictiveStrokeToolView::PredictiveStrokeToolView(const StrokeHandler* strokeHandler, const Stroke& stroke,
                                                   Repaintable* parent):
        StrokeToolView(strokeHandler, stroke, parent) {
    // drawPrediction() paints straight onto the target surface instead of going
    // through the Mask, so the prediction may overlap the confirmed stroke.
    // That is only invisible because BaseStrokeToolView forces alpha to 255 for
    // non-highlighter tools and because an unfilled stroke has nothing else to
    // blend. StrokeHandler enforces the same condition before selecting this
    // view; pin it here so the two cannot drift apart.
    xoj_assert(stroke.getToolType() == StrokeTool::PEN && stroke.getFill() == -1);
}

PredictiveStrokeToolView::~PredictiveStrokeToolView() noexcept = default;

void PredictiveStrokeToolView::draw(cairo_t* cr) const {
    StrokeToolView::draw(cr);
    drawPrediction(cr);
}

void PredictiveStrokeToolView::drawWithoutDrawingAids(cairo_t* cr) const {
    // XojPageView calls this method while burning the finalized ToolView into
    // the page buffer. Prediction points must never reach that buffer.
    StrokeToolView::draw(cr);
}

void PredictiveStrokeToolView::drawPrediction(cairo_t* cr) const {
    if (prediction.size() < 2) {
        return;
    }

    xoj::util::CairoSaveGuard saveGuard(cr);
    // Cairo's current path is not part of save/restore. Start an isolated
    // path and match the round caps/joins used by StrokeToolView's mask.
    cairo_new_path(cr);
    cairo_set_operator(cr, cairoOp);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    Util::cairo_set_source_argb(cr, strokeColor);

    if (prediction.front().z == Point::NO_PRESSURE) {
        StrokeViewHelper::drawNoPressure(cr, prediction, strokeWidth, lineStyle, dashOffset);
    } else {
        StrokeViewHelper::drawWithPressure(cr, prediction, lineStyle, dashOffset);
    }
}

auto PredictiveStrokeToolView::predictionRange() const -> Range {
    if (prediction.empty()) {
        return {};
    }

    Range range(prediction.front().x, prediction.front().y);
    double maxWidth = strokeWidth;
    for (const Point& point: prediction) {
        range.addPoint(point.x, point.y);
        if (point.z != Point::NO_PRESSURE && std::isfinite(point.z)) {
            maxWidth = std::max(maxWidth, point.z);
        }
    }
    range.addPadding(0.5 * maxWidth + 1.0);
    return range;
}

void PredictiveStrokeToolView::replacePrediction(const std::vector<Point>& newPrediction, bool requestRepaint) {
    const Range oldRange = predictionRange();
    prediction = newPrediction;
    const Range newRange = predictionRange();

    if (!requestRepaint) {
        return;
    }
    const Range dirty = oldRange.empty() ? newRange : (newRange.empty() ? oldRange : oldRange.unite(newRange));
    if (!dirty.empty()) {
        parent->flagDirtyRegion(dirty);
    }
}

void PredictiveStrokeToolView::on(PredictionReplacementRequest, const std::vector<Point>& newPrediction) {
    replacePrediction(newPrediction, true);
}

void PredictiveStrokeToolView::on(StrokeReplacementRequest request, const Stroke& newStroke) {
    const Range oldPrediction = predictionRange();
    prediction.clear();
    StrokeToolView::on(request, newStroke);
    if (!oldPrediction.empty()) {
        parent->flagDirtyRegion(oldPrediction);
    }
}

void PredictiveStrokeToolView::deleteOn(CancellationRequest request, const Range& range) {
    const Range transientRange = predictionRange();
    prediction.clear();
    StrokeToolView::deleteOn(
            request, transientRange.empty() ? range : (range.empty() ? transientRange : range.unite(transientRange)));
}

void PredictiveStrokeToolView::deleteOn(FinalizationRequest request, const Range& range) {
    const Range transientRange = predictionRange();
    prediction.clear();
    StrokeToolView::deleteOn(
            request, transientRange.empty() ? range : (range.empty() ? transientRange : range.unite(transientRange)));
}

}  // namespace xoj::view
