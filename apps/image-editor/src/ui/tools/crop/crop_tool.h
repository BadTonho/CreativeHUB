#pragma once

#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>

#include <optional>

class QPainter;

namespace image_editor {

struct CropToolContext {
    QSize image_size;
    QRectF image_target;
    qreal zoom = 1.0;
};

class CropTool final {
public:
    void beginGesture(const QPointF& image_position) noexcept;
    void updateGesture(const QPointF& image_position) noexcept;
    [[nodiscard]] std::optional<QRect> finishGesture(
        const QPointF& image_position, const QSize& image_size) noexcept;
    [[nodiscard]] bool cancelGesture() noexcept;

    [[nodiscard]] bool gestureActive() const noexcept { return gesture_active_; }
    [[nodiscard]] std::optional<QRectF> preview(const QSize& image_size) const noexcept;
    void paintOverlay(QPainter& painter, const CropToolContext& context) const;

private:
    QPointF gesture_start_;
    QPointF gesture_current_;
    bool gesture_active_ = false;
};

} // namespace image_editor
