#include "lasso_tool.h"

#include "image_document_store.h"

#include <QPolygonF>

#include <cmath>

#include <utility>

namespace image_editor {
namespace {

QPainterPath boundsPath(const QRectF& bounds) {
    QPainterPath path;
    if (!bounds.isEmpty()) path.addRect(bounds);
    return path;
}

bool hasFilledArea(const QPainterPath& path) {
    for (const QPolygonF& polygon : path.toFillPolygons()) {
        if (polygon.size() < 3) continue;
        qreal twice_area = 0.0;
        for (qsizetype i = 0; i < polygon.size(); ++i) {
            const QPointF& current = polygon.at(i);
            const QPointF& next = polygon.at((i + 1) % polygon.size());
            twice_area += current.x() * next.y() - next.x() * current.y();
        }
        if (std::abs(twice_area) > 1e-6) return true;
    }
    return false;
}

} // namespace

void LassoTool::beginGesture(const QPointF& image_position) noexcept {
    resetGesture();
    gesture_active_ = true;
    start_ = image_position;
    points_.append(image_position);
}

void LassoTool::updateGesture(const QPointF& image_position) noexcept {
    if (!gesture_active_) return;
    recordPoint(image_position);
}

LassoTool::FinishResult LassoTool::finishGesture(
    const QPointF& image_position, const QRectF& image_bounds) {
    if (!gesture_active_) return {};
    recordPoint(image_position);
    if (gesture_rejected_) {
        resetGesture();
        return {FinishStatus::Rejected, {},
                QStringLiteral("The selection would exceed the supported geometry limit.")};
    }

    QPainterPath completed = previewPath(image_bounds);
    if (completed.isEmpty() || !hasFilledArea(completed)) {
        resetGesture();
        return {};
    }
    resetGesture();
    return {FinishStatus::Completed, std::move(completed), {}};
}

bool LassoTool::cancelGesture() noexcept {
    if (!gesture_active_) return false;
    resetGesture();
    return true;
}

QPainterPath LassoTool::previewPath(const QRectF& image_bounds) const {
    if (!gesture_active_ || gesture_rejected_ || !has_area_ ||
        points_.size() < 3 || image_bounds.isEmpty()) return {};
    QPainterPath path;
    path.moveTo(points_.constFirst());
    for (qsizetype i = 1; i < points_.size(); ++i)
        path.lineTo(points_.at(i));
    path.closeSubpath();
    return path.intersected(boundsPath(image_bounds));
}

void LassoTool::recordPoint(const QPointF& image_position) noexcept {
    if (gesture_rejected_ ||
        (!points_.isEmpty() && points_.constLast() == image_position)) return;
    constexpr qsizetype maximum_points =
        ImageDocumentStore::kMaximumStrokeClipPathElements - 2;
    if (points_.size() >= maximum_points) {
        gesture_rejected_ = true;
        return;
    }
    points_.append(image_position);
    if (image_position == start_) return;
    if (!has_area_reference_) {
        area_reference_ = image_position;
        has_area_reference_ = true;
        return;
    }
    if (!has_area_) {
        const QPointF first = area_reference_ - start_;
        const QPointF second = image_position - start_;
        has_area_ = std::abs(first.x() * second.y() - first.y() * second.x()) > 1e-6;
    }
}

void LassoTool::resetGesture() noexcept {
    gesture_active_ = false;
    gesture_rejected_ = false;
    has_area_reference_ = false;
    has_area_ = false;
    start_ = {};
    area_reference_ = {};
    points_.clear();
}

} // namespace image_editor
