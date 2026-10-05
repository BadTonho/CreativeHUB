#include "crop_tool.h"

#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace image_editor {

void CropTool::beginGesture(const QPointF& image_position) noexcept {
    gesture_start_ = image_position;
    gesture_current_ = image_position;
    gesture_active_ = true;
}

void CropTool::updateGesture(const QPointF& image_position) noexcept {
    if (gesture_active_) gesture_current_ = image_position;
}

std::optional<QRect> CropTool::finishGesture(const QPointF& image_position,
                                             const QSize& image_size) noexcept {
    if (!gesture_active_) return std::nullopt;
    gesture_current_ = image_position;
    gesture_active_ = false;
    if (image_size.width() <= 0 || image_size.height() <= 0) return std::nullopt;

    const QRectF image_bounds(QPointF(0.0, 0.0), QSizeF(image_size));
    const QRectF clipped = QRectF(gesture_start_, gesture_current_)
                               .normalized().intersected(image_bounds);
    if (clipped.isEmpty()) return std::nullopt;

    const int left = std::clamp(static_cast<int>(std::floor(clipped.left())),
                                0, image_size.width());
    const int top = std::clamp(static_cast<int>(std::floor(clipped.top())),
                               0, image_size.height());
    const int right = std::clamp(static_cast<int>(std::ceil(clipped.right())),
                                 0, image_size.width());
    const int bottom = std::clamp(static_cast<int>(std::ceil(clipped.bottom())),
                                  0, image_size.height());
    const QRect result(left, top, right - left, bottom - top);
    if (result.width() <= 1 || result.height() <= 1) return std::nullopt;
    return result;
}

bool CropTool::cancelGesture() noexcept {
    if (!gesture_active_) return false;
    gesture_active_ = false;
    gesture_start_ = {};
    gesture_current_ = {};
    return true;
}

std::optional<QRectF> CropTool::preview(const QSize& image_size) const noexcept {
    if (!gesture_active_ || image_size.width() <= 0 || image_size.height() <= 0)
        return std::nullopt;
    const QRectF image_bounds(QPointF(0.0, 0.0), QSizeF(image_size));
    const QRectF clipped = QRectF(gesture_start_, gesture_current_)
                               .normalized().intersected(image_bounds);
    if (clipped.isEmpty()) return std::nullopt;
    return clipped;
}

void CropTool::paintOverlay(QPainter& painter,
                            const CropToolContext& context) const {
    const auto crop_preview = preview(context.image_size);
    if (!crop_preview || context.zoom <= 0.0) return;

    const QRectF widget_selection(
        context.image_target.left() + crop_preview->left() * context.zoom,
        context.image_target.top() + crop_preview->top() * context.zoom,
        crop_preview->width() * context.zoom,
        crop_preview->height() * context.zoom);
    painter.save();
    painter.setClipRect(context.image_target);
    painter.fillRect(widget_selection, QColor(38, 150, 220, 36));
    painter.setPen(QPen(QColor(120, 205, 255), 1.5, Qt::DashLine));
    painter.drawRect(widget_selection);
    painter.restore();
}

} // namespace image_editor
