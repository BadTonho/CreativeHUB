#include "area_selection_tool.h"

#include "image_document_store.h"

#include <QPainter>
#include <QPen>
#include <utility>

namespace image_editor {
namespace {

QPainterPath rectPath(const QRectF& rect) {
    QPainterPath path;
    if (!rect.isEmpty()) path.addRect(rect);
    return path;
}

} // namespace

void AreaSelectionTool::setOptions(Shape shape, CombineMode combine_mode) noexcept {
    shape_ = shape;
    combine_mode_ = combine_mode;
}

void AreaSelectionTool::setLassoMode(bool enabled) noexcept {
    if (lasso_mode_ == enabled) return;
    static_cast<void>(cancelGesture());
    lasso_mode_ = enabled;
}

void AreaSelectionTool::beginGesture(const QPointF& image_position) noexcept {
    gesture_active_ = true;
    gesture_start_ = image_position;
    gesture_current_ = image_position;
    if (lasso_mode_) lasso_tool_.beginGesture(image_position);
}

void AreaSelectionTool::updateGesture(const QPointF& image_position) noexcept {
    if (!gesture_active_) return;
    gesture_current_ = image_position;
    if (lasso_mode_) lasso_tool_.updateGesture(image_position);
}

AreaSelectionTool::FinishResult AreaSelectionTool::finishGesture(
    const QPointF& image_position, const QRectF& image_bounds) {
    if (!gesture_active_) return {};

    gesture_current_ = image_position;
    QPainterPath gesture;
    if (lasso_mode_) {
        auto lasso_result = lasso_tool_.finishGesture(image_position, image_bounds);
        if (lasso_result.status == LassoTool::FinishStatus::Rejected) {
            resetGesture();
            return {FinishStatus::Rejected, std::move(lasso_result.rejection_reason)};
        }
        gesture = std::move(lasso_result.path);
    } else {
        gesture = gesturePath(image_bounds);
    }
    if (gesture.isEmpty()) {
        resetGesture();
        return {};
    }
    resetGesture();

    QPainterPath combined = combinedPath(gesture, image_bounds);
    if (combined.elementCount() > ImageDocumentStore::kMaximumStrokeClipPathElements) {
        return {FinishStatus::Rejected,
                QStringLiteral("The selection would exceed the supported geometry limit.")};
    }

    selection_path_ = std::move(combined);
    selection_active_ = true;
    return {FinishStatus::Applied, {}};
}

bool AreaSelectionTool::cancelGesture() noexcept {
    if (!gesture_active_) return false;
    resetGesture();
    return true;
}

bool AreaSelectionTool::clearSelection() noexcept {
    const bool changed = selection_active_ || !selection_path_.isEmpty();
    selection_active_ = false;
    selection_path_ = {};
    static_cast<void>(cancelGesture());
    return changed;
}

bool AreaSelectionTool::translateSelection(const QPoint& delta) noexcept {
    if (!selection_active_ || delta.isNull()) return false;
    selection_path_.translate(delta);
    return true;
}

std::optional<QPainterPath> AreaSelectionTool::clipPath(
    const QRectF& image_bounds) const {
    if (!selection_active_) return {};
    return visibleSelectionPath(image_bounds);
}

QPainterPath AreaSelectionTool::previewPath(const QRectF& image_bounds) const {
    QPainterPath displayed = visibleSelectionPath(image_bounds);
    if (gesture_active_) {
        const QPainterPath gesture = gesturePath(image_bounds);
        if (!gesture.isEmpty()) displayed = combinedPath(gesture, image_bounds);
    }
    if (selection_active_ || gesture_active_) {
        displayed = displayed.intersected(rectPath(image_bounds));
    }
    return displayed;
}

void AreaSelectionTool::paintOverlay(
    QPainter& painter, const AreaSelectionToolRenderContext& context) const {
    if (!selection_active_ && !gesture_active_) return;

    const QPainterPath widget_selection =
        context.image_to_widget.map(previewPath(context.image_bounds));
    painter.save();
    painter.setClipRect(context.image_target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillPath(widget_selection, QColor(70, 165, 235, 28));
    QPen dark_outline(QColor(20, 24, 30), 2.0, Qt::DashLine);
    dark_outline.setCosmetic(true);
    dark_outline.setDashOffset(0.0);
    painter.setPen(dark_outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(widget_selection);
    QPen light_outline(QColor(245, 248, 252), 1.0, Qt::DashLine);
    light_outline.setCosmetic(true);
    light_outline.setDashOffset(3.0);
    painter.setPen(light_outline);
    painter.drawPath(widget_selection);
    painter.restore();
}

QPainterPath AreaSelectionTool::visibleSelectionPath(
    const QRectF& image_bounds) const {
    if (!selection_active_ || image_bounds.isEmpty()) return {};
    return selection_path_.intersected(rectPath(image_bounds));
}

QPainterPath AreaSelectionTool::gesturePath(const QRectF& image_bounds) const {
    if (!gesture_active_ || image_bounds.isEmpty()) return {};
    if (lasso_mode_) return lasso_tool_.previewPath(image_bounds);

    const QRectF bounds = QRectF(gesture_start_, gesture_current_).normalized();
    if (bounds.width() <= 0.0 || bounds.height() <= 0.0) return {};
    QPainterPath path;
    if (shape_ == Shape::Ellipse) path.addEllipse(bounds);
    else path.addRect(bounds);
    return path.intersected(rectPath(image_bounds));
}

void AreaSelectionTool::resetGesture() noexcept {
    gesture_active_ = false;
    static_cast<void>(lasso_tool_.cancelGesture());
    gesture_start_ = {};
    gesture_current_ = {};
}

QPainterPath AreaSelectionTool::combinedPath(const QPainterPath& gesture,
                                             const QRectF& image_bounds) const {
    QPainterPath combined;
    if (combine_mode_ == CombineMode::Replace) {
        combined = gesture;
    } else if (combine_mode_ == CombineMode::Add) {
        combined = selection_active_
            ? visibleSelectionPath(image_bounds).united(gesture) : gesture;
    } else {
        combined = selection_active_
            ? visibleSelectionPath(image_bounds).subtracted(gesture) : QPainterPath{};
    }
    return combined.intersected(rectPath(image_bounds));
}

} // namespace image_editor
