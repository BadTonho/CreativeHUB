#pragma once

#include <creative_suite/animation/animation.h>

#include <QWidget>
#include <QPoint>
#include <QRectF>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class QMouseEvent;
class QPaintEvent;

namespace motion::ui {

class PropertyCurveEditor final : public QWidget {
    Q_OBJECT

public:
    using SelectionHandler = std::function<void(std::int64_t)>;
    using EasingEditedHandler = std::function<void(
        std::int64_t,
        creative_suite::animation::CubicBezierEasing)>;

    explicit PropertyCurveEditor(QWidget* parent = nullptr);

    void setCurve(std::uint64_t layer_id,
                  creative_suite::animation::TransformProperty property,
                  std::vector<creative_suite::animation::Keyframe> keyframes,
                  std::optional<std::int64_t> selected_segment_start);
    void setSelectionHandler(SelectionHandler handler);
    void setEasingEditedHandler(EasingEditedHandler handler);
    [[nodiscard]] QPoint controlHandlePosition(int control_index) const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] QRectF plotRect() const noexcept;
    [[nodiscard]] QPointF pointForValue(double frame, double value) const noexcept;
    [[nodiscard]] QPointF pointForControl(int control_index) const noexcept;
    [[nodiscard]] double frameForX(double x) const noexcept;
    [[nodiscard]] double valueForY(double y) const noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedSegmentIndex() const noexcept;
    [[nodiscard]] std::optional<std::size_t> segmentAt(QPointF position) const;
    void selectSegment(std::size_t index);

    std::uint64_t layer_id_ = 0;
    creative_suite::animation::TransformProperty property_{
        creative_suite::animation::TransformProperty::PositionX};
    std::vector<creative_suite::animation::Keyframe> keyframes_;
    std::optional<std::int64_t> selected_segment_start_;
    SelectionHandler selection_handler_;
    EasingEditedHandler easing_edited_handler_;
    std::optional<int> dragging_control_;
    bool easing_changed_during_drag_ = false;
};

} // namespace motion::ui
