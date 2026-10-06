#include "linear_gradient_tool.h"

#include <algorithm>
#include <cmath>

namespace image_editor {

bool LinearGradientTool::beginGesture(const QPointF& start,
                                      const QSize& document_size,
                                      const QColor& color) noexcept {
    if (document_size.width() <= 0 || document_size.height() <= 0 ||
        !std::isfinite(start.x()) || !std::isfinite(start.y()) ||
        start.x() < 0.0 || start.y() < 0.0 ||
        start.x() > document_size.width() - 1 ||
        start.y() > document_size.height() - 1 || !color.isValid()) {
        return false;
    }
    document_size_ = document_size;
    start_ = start;
    end_ = start;
    color_ = color;
    active_ = true;
    return true;
}

bool LinearGradientTool::updateGesture(const QPointF& end) noexcept {
    if (!active_ || !std::isfinite(end.x()) || !std::isfinite(end.y())) return false;
    end_ = clampToDocument(end);
    return true;
}

std::optional<ImageLinearGradientData> LinearGradientTool::finishGesture(
    const QPointF& end) noexcept {
    if (!active_ || !std::isfinite(end.x()) || !std::isfinite(end.y())) {
        cancelGesture();
        return std::nullopt;
    }
    end_ = clampToDocument(end);
    std::optional<ImageLinearGradientData> result;
    if (start_ != end_) {
        ImageLinearGradientData gradient;
        gradient.start = start_;
        gradient.end = end_;
        gradient.color = color_;
        result = std::move(gradient);
    }
    cancelGesture();
    return result;
}

void LinearGradientTool::cancelGesture() noexcept {
    active_ = false;
    document_size_ = {};
    start_ = {};
    end_ = {};
    color_ = {};
}

QPointF LinearGradientTool::clampToDocument(const QPointF& point) const noexcept {
    return {std::clamp(point.x(), 0.0, static_cast<qreal>(document_size_.width() - 1)),
            std::clamp(point.y(), 0.0, static_cast<qreal>(document_size_.height() - 1))};
}

} // namespace image_editor
