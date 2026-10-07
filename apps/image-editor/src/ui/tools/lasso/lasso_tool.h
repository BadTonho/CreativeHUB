#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace image_editor {

class LassoTool final {
public:
    enum class FinishStatus { NoSelection, Completed, Rejected };

    struct FinishResult {
        FinishStatus status = FinishStatus::NoSelection;
        QPainterPath path;
        QString rejection_reason;
    };

    void beginGesture(const QPointF& image_position) noexcept;
    void updateGesture(const QPointF& image_position) noexcept;
    [[nodiscard]] FinishResult finishGesture(const QPointF& image_position,
                                             const QRectF& image_bounds);
    [[nodiscard]] bool cancelGesture() noexcept;
    [[nodiscard]] bool gestureActive() const noexcept { return gesture_active_; }
    [[nodiscard]] QPainterPath previewPath(const QRectF& image_bounds) const;

private:
    void recordPoint(const QPointF& image_position) noexcept;
    void resetGesture() noexcept;

    bool gesture_active_ = false;
    bool gesture_rejected_ = false;
    bool has_area_reference_ = false;
    bool has_area_ = false;
    QPointF start_;
    QPointF area_reference_;
    QVector<QPointF> points_;
};

} // namespace image_editor
