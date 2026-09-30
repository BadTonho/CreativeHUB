#include "property_curve_editor.h"

#include <QMouseEvent>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace motion::ui {
namespace {

using creative_suite::animation::CubicBezierEasing;
using creative_suite::animation::InterpolationMode;
using creative_suite::animation::Keyframe;
using creative_suite::animation::TransformProperty;

constexpr int kLeftMargin = 54;
constexpr int kTopMargin = 12;
constexpr int kRightMargin = 16;
constexpr int kBottomMargin = 28;

QString propertyName(TransformProperty property)
{
    switch (property) {
    case TransformProperty::PositionX: return QStringLiteral("Position X");
    case TransformProperty::PositionY: return QStringLiteral("Position Y");
    case TransformProperty::Scale: return QStringLiteral("Scale");
    case TransformProperty::Rotation: return QStringLiteral("Rotation");
    case TransformProperty::Opacity: return QStringLiteral("Opacity");
    }
    return QStringLiteral("Transform");
}

QColor propertyColor(TransformProperty property)
{
    switch (property) {
    case TransformProperty::PositionX: return QColor(110, 190, 255);
    case TransformProperty::PositionY: return QColor(105, 220, 190);
    case TransformProperty::Scale: return QColor(190, 160, 255);
    case TransformProperty::Rotation: return QColor(255, 183, 54);
    case TransformProperty::Opacity: return QColor(255, 125, 145);
    }
    return QColor(255, 183, 54);
}

bool closeTo(double left, double right)
{
    return std::abs(left - right) <= 1e-6;
}

} // namespace

PropertyCurveEditor::PropertyCurveEditor(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("motion-property-curve-editor"));
    setMinimumHeight(148);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMouseTracking(true);
}

void PropertyCurveEditor::setCurve(
    std::uint64_t layer_id,
    TransformProperty property,
    std::vector<Keyframe> keyframes,
    std::optional<std::int64_t> selected_segment_start)
{
    layer_id_ = layer_id;
    property_ = property;
    keyframes_ = std::move(keyframes);
    selected_segment_start_ = selected_segment_start;
    if (!selectedSegmentIndex()) {
        selected_segment_start_.reset();
        if (keyframes_.size() >= 2) selected_segment_start_ = keyframes_.front().frame;
    }
    update();
}

void PropertyCurveEditor::setSelectionHandler(SelectionHandler handler)
{
    selection_handler_ = std::move(handler);
}

void PropertyCurveEditor::setEasingEditedHandler(EasingEditedHandler handler)
{
    easing_edited_handler_ = std::move(handler);
}

QPoint PropertyCurveEditor::controlHandlePosition(int control_index) const
{
    return pointForControl(control_index).toPoint();
}

QRectF PropertyCurveEditor::plotRect() const noexcept
{
    return QRectF(kLeftMargin, kTopMargin,
                  std::max(1, width() - kLeftMargin - kRightMargin),
                  std::max(1, height() - kTopMargin - kBottomMargin));
}

QPointF PropertyCurveEditor::pointForValue(double frame, double value) const noexcept
{
    const QRectF plot = plotRect();
    if (keyframes_.size() < 2) return {plot.left(), plot.center().y()};

    const long double first_frame = static_cast<long double>(keyframes_.front().frame);
    const long double last_frame = static_cast<long double>(keyframes_.back().frame);
    const long double frame_span = std::max(1.0L, last_frame - first_frame);
    const double min_frame = static_cast<double>(first_frame);
    const double frame_fraction = std::clamp(
        (frame - min_frame) / static_cast<double>(frame_span), 0.0, 1.0);

    double min_value = keyframes_.front().value;
    double max_value = min_value;
    for (const auto& keyframe : keyframes_) {
        min_value = std::min(min_value, keyframe.value);
        max_value = std::max(max_value, keyframe.value);
    }
    double value_padding = (max_value - min_value) * 0.12;
    if (value_padding <= std::numeric_limits<double>::epsilon()) {
        value_padding = std::max(1.0, std::abs(min_value) * 0.12);
    }
    min_value -= value_padding;
    max_value += value_padding;
    const double value_fraction = std::clamp(
        (value - min_value) / (max_value - min_value), 0.0, 1.0);
    return {plot.left() + frame_fraction * plot.width(),
            plot.bottom() - value_fraction * plot.height()};
}

QPointF PropertyCurveEditor::pointForControl(int control_index) const noexcept
{
    const auto segment = selectedSegmentIndex();
    if (!segment.has_value() || control_index < 0 || control_index > 1) {
        return {};
    }
    const auto& left = keyframes_[*segment];
    const auto& right = keyframes_[*segment + 1];
    const auto& easing = left.easing;
    const double frame_span = static_cast<double>(right.frame - left.frame);
    const double value_span = right.value - left.value;
    const double x = control_index == 0 ? easing.x1 : easing.x2;
    const double y = control_index == 0 ? easing.y1 : easing.y2;
    return pointForValue(static_cast<double>(left.frame) + frame_span * x,
                         left.value + value_span * y);
}

double PropertyCurveEditor::frameForX(double x) const noexcept
{
    const QRectF plot = plotRect();
    if (keyframes_.size() < 2) return 0.0;
    const double first = static_cast<double>(keyframes_.front().frame);
    const double last = static_cast<double>(keyframes_.back().frame);
    const double fraction = std::clamp((x - plot.left()) / plot.width(), 0.0, 1.0);
    return first + (last - first) * fraction;
}

double PropertyCurveEditor::valueForY(double y) const noexcept
{
    if (keyframes_.empty()) return 0.0;
    const QRectF plot = plotRect();
    double min_value = keyframes_.front().value;
    double max_value = min_value;
    for (const auto& keyframe : keyframes_) {
        min_value = std::min(min_value, keyframe.value);
        max_value = std::max(max_value, keyframe.value);
    }
    double padding = (max_value - min_value) * 0.12;
    if (padding <= std::numeric_limits<double>::epsilon()) {
        padding = std::max(1.0, std::abs(min_value) * 0.12);
    }
    min_value -= padding;
    max_value += padding;
    const double fraction = std::clamp((plot.bottom() - y) / plot.height(), 0.0, 1.0);
    return min_value + fraction * (max_value - min_value);
}

std::optional<std::size_t> PropertyCurveEditor::selectedSegmentIndex() const noexcept
{
    if (!selected_segment_start_.has_value()) return std::nullopt;
    const auto found = std::lower_bound(keyframes_.begin(), keyframes_.end(),
        *selected_segment_start_, [](const Keyframe& keyframe, std::int64_t frame) {
            return keyframe.frame < frame;
        });
    if (found == keyframes_.end() || found->frame != *selected_segment_start_ ||
        std::next(found) == keyframes_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(keyframes_.begin(), found));
}

std::optional<std::size_t> PropertyCurveEditor::segmentAt(QPointF position) const
{
    if (keyframes_.size() < 2 || !plotRect().contains(position)) return std::nullopt;
    for (std::size_t index = 0; index + 1 < keyframes_.size(); ++index) {
        const auto& left = keyframes_[index];
        const auto& right = keyframes_[index + 1];
        QPainterPath path;
        bool started = false;
        constexpr int samples = 64;
        for (int sample = 0; sample <= samples; ++sample) {
            const double progress = static_cast<double>(sample) / samples;
            const double eased = creative_suite::animation::evaluateEasing(
                progress, left.interpolation, left.easing);
            const double frame = static_cast<double>(left.frame) +
                static_cast<double>(right.frame - left.frame) * progress;
            const double value = left.value + (right.value - left.value) * eased;
            const auto point = pointForValue(frame, value);
            if (!started) {
                path.moveTo(point);
                started = true;
            } else {
                path.lineTo(point);
            }
        }
        QPainterPathStroker stroker;
        stroker.setWidth(14.0);
        if (stroker.createStroke(path).contains(position)) return index;
    }
    return std::nullopt;
}

void PropertyCurveEditor::selectSegment(std::size_t index)
{
    if (index + 1 >= keyframes_.size()) return;
    const auto frame = keyframes_[index].frame;
    if (selected_segment_start_ == frame) return;
    selected_segment_start_ = frame;
    update();
    if (selection_handler_) selection_handler_(frame);
}

void PropertyCurveEditor::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(31, 35, 41));
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF plot = plotRect();
    painter.fillRect(plot, QColor(25, 28, 33));
    painter.setPen(QPen(QColor(57, 63, 72), 1));
    for (int tick = 0; tick <= 4; ++tick) {
        const double x = plot.left() + plot.width() * tick / 4.0;
        const double y = plot.top() + plot.height() * tick / 4.0;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    painter.setPen(QColor(176, 183, 193));
    painter.drawText(QRectF(4, 4, width() - 8, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     propertyName(property_));

    if (keyframes_.size() < 2) {
        painter.setPen(QColor(165, 172, 182));
        painter.drawText(plot, Qt::AlignCenter,
                         QStringLiteral("Add two keyframes to edit this curve"));
        return;
    }

    const auto first_frame = keyframes_.front().frame;
    const auto last_frame = keyframes_.back().frame;
    painter.setPen(QColor(176, 183, 193));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 5, plot.width(), 18),
                     Qt::AlignLeft | Qt::AlignVCenter, QString::number(first_frame));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 5, plot.width(), 18),
                     Qt::AlignRight | Qt::AlignVCenter, QString::number(last_frame));

    for (std::size_t index = 0; index + 1 < keyframes_.size(); ++index) {
        const auto& left = keyframes_[index];
        const auto& right = keyframes_[index + 1];
        QPainterPath path;
        constexpr int samples = 80;
        for (int sample = 0; sample <= samples; ++sample) {
            const double progress = static_cast<double>(sample) / samples;
            const double eased = creative_suite::animation::evaluateEasing(
                progress, left.interpolation, left.easing);
            const double frame = static_cast<double>(left.frame) +
                static_cast<double>(right.frame - left.frame) * progress;
            const double value = left.value + (right.value - left.value) * eased;
            const QPointF point = pointForValue(frame, value);
            if (sample == 0) path.moveTo(point);
            else path.lineTo(point);
        }
        const bool selected = selected_segment_start_ == left.frame;
        painter.setPen(QPen(selected ? QColor(255, 183, 54) : propertyColor(property_),
                            selected ? 3.0 : 2.0));
        painter.drawPath(path);
        painter.setBrush(selected ? QColor(255, 183, 54) : propertyColor(property_));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(pointForValue(static_cast<double>(left.frame), left.value), 4, 4);
        painter.drawEllipse(pointForValue(static_cast<double>(right.frame), right.value), 4, 4);
    }

    const auto selected = selectedSegmentIndex();
    if (!selected.has_value()) return;
    const auto& left = keyframes_[*selected];
    const auto& right = keyframes_[*selected + 1];
    const auto start = pointForValue(static_cast<double>(left.frame), left.value);
    const auto end = pointForValue(static_cast<double>(right.frame), right.value);
    const auto first_control = pointForControl(0);
    const auto second_control = pointForControl(1);
    painter.setPen(QPen(QColor(151, 159, 170), 1, Qt::DashLine));
    painter.drawLine(start, first_control);
    painter.drawLine(end, second_control);
    painter.setPen(QPen(QColor(250, 250, 250), 1));
    painter.setBrush(QColor(80, 180, 255));
    painter.drawEllipse(first_control, 6, 6);
    painter.setBrush(QColor(105, 220, 190));
    painter.drawEllipse(second_control, 6, 6);
}

void PropertyCurveEditor::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const auto segment = selectedSegmentIndex();
    if (segment.has_value()) {
        for (int control = 0; control < 2; ++control) {
            if (QLineF(event->position(), pointForControl(control)).length() <= 11.0) {
                dragging_control_ = control;
                easing_changed_during_drag_ = false;
                event->accept();
                return;
            }
        }
    }
    if (const auto clicked_segment = segmentAt(event->position())) {
        selectSegment(*clicked_segment);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PropertyCurveEditor::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_control_.has_value() ||
        (event->buttons() & Qt::LeftButton) == 0) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const auto segment = selectedSegmentIndex();
    if (!segment.has_value()) return;
    auto& left = keyframes_[*segment];
    const auto& right = keyframes_[*segment + 1];
    const double span = static_cast<double>(right.frame - left.frame);
    if (span <= 0.0) return;
    const double normalized_x = std::clamp(
        (frameForX(event->position().x()) - static_cast<double>(left.frame)) / span,
        0.0, 1.0);
    const double value_span = right.value - left.value;
    const double normalized_y = std::abs(value_span) <= std::numeric_limits<double>::epsilon()
        ? (dragging_control_ == 0 ? left.easing.y1 : left.easing.y2)
        : std::clamp((valueForY(event->position().y()) - left.value) / value_span, 0.0, 1.0);

    auto easing = left.easing;
    if (*dragging_control_ == 0) {
        easing.x1 = std::min(normalized_x, easing.x2);
        easing.y1 = normalized_y;
    } else {
        easing.x2 = std::max(normalized_x, easing.x1);
        easing.y2 = normalized_y;
    }
    if (easing == left.easing && left.interpolation == InterpolationMode::CubicBezier) return;
    left.interpolation = InterpolationMode::CubicBezier;
    left.easing = easing;
    easing_changed_during_drag_ = true;
    update();
    event->accept();
}

void PropertyCurveEditor::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && dragging_control_.has_value()) {
        const auto segment = selectedSegmentIndex();
        if (segment.has_value() && easing_changed_during_drag_ && easing_edited_handler_) {
            const auto& keyframe = keyframes_[*segment];
            easing_edited_handler_(keyframe.frame, keyframe.easing);
        }
        dragging_control_.reset();
        easing_changed_during_drag_ = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

} // namespace motion::ui
