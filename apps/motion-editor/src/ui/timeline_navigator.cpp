#include "timeline_navigator.h"
#include "timeline_navigator_math.h"

#include <QAction>

#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSlider>
#include <QTimer>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QSignalBlocker>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>

namespace motion::ui {
namespace {

constexpr int kRulerHorizontalPadding = 20;
constexpr int kTimelineHeaderWidth = 180;
constexpr int kTimelineRowHeight = 34;
constexpr int kTimelineClipHandleWidth = 7;
constexpr int kTimelineSnapTolerancePixels = 8;
constexpr std::int64_t kMaximumFrame = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kSecondsPerHour = 60 * 60;
const QString kMotionMediaMimeType = QStringLiteral("application/x-creative-suite-motion-media");

int rulerAxisLeft(int width, int header_width) noexcept
{
    return std::min(std::max(0, header_width + kRulerHorizontalPadding),
                    std::max(0, width - kRulerHorizontalPadding));
}

int rulerAxisRight(int width) noexcept
{
    return std::max(0, width - kRulerHorizontalPadding);
}

std::filesystem::path pathFromQString(const QString& value)
{
    const auto utf8 = value.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(utf8.constData());
    return std::filesystem::path(std::u8string(first, first + utf8.size()));
}

QString utf8Text(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString formatFrameRate(model::FrameRate frame_rate)
{
    return QString::number(frame_rate.asDouble(), 'g', 6);
}

QString formatTimelinePosition(
    std::int64_t frame,
    model::FrameRate frame_rate,
    TimelineDisplayMode display_mode)
{
    if (display_mode == TimelineDisplayMode::Frames) {
        return QString::number(static_cast<qlonglong>(frame));
    }
    return utf8Text(detail::formatElapsedTime(
        frame, frame_rate.numerator, frame_rate.denominator));
}

std::int64_t framesPerHour(model::FrameRate frame_rate) noexcept
{
    const auto hour_frames_numerator = frame_rate.numerator * kSecondsPerHour;
    const auto frame_count = (hour_frames_numerator + frame_rate.denominator - 1)
        / frame_rate.denominator;
    return std::max<std::int64_t>(1, frame_count);
}

std::int64_t initialVisibleEndFrame(model::FrameRate frame_rate) noexcept
{
    return framesPerHour(frame_rate) - 1;
}

struct TimelineViewMapping {
    int width = 0;
    int header_width = 0;
    std::int64_t range_end_frame = 0;
    std::int64_t start_frame = 0;
    std::int64_t frames_per_view = 1;

    [[nodiscard]] int axisLeft() const noexcept
    {
        return rulerAxisLeft(width, header_width);
    }

    [[nodiscard]] int axisRight() const noexcept
    {
        return rulerAxisRight(width);
    }

    [[nodiscard]] int axisWidth() const noexcept
    {
        return std::max(0, axisRight() - axisLeft());
    }

    [[nodiscard]] std::int64_t viewEndFrame() const noexcept
    {
        const auto bounded_start = std::clamp(start_frame, std::int64_t{0}, range_end_frame);
        const auto last_offset = std::max<std::int64_t>(0, frames_per_view - 1);
        return std::min(range_end_frame,
            detail::saturatingFrameAdd(bounded_start, last_offset));
    }

    [[nodiscard]] std::int64_t frameAtX(int x) const noexcept
    {
        const auto bounded_start = std::clamp(start_frame, std::int64_t{0}, range_end_frame);
        const int left = axisLeft();
        const int span = axisWidth();
        if (span <= 0 || x <= left || frames_per_view <= 1) {
            return bounded_start;
        }
        const auto available = range_end_frame - bounded_start;
        if (x >= axisRight()) {
            return std::min(range_end_frame, viewEndFrame());
        }
        const long double fraction = static_cast<long double>(x - left) /
            static_cast<long double>(span);
        const long double offset_value = std::floor(
            fraction * static_cast<long double>(frames_per_view - 1) + 0.5L);
        if (offset_value >= static_cast<long double>(available)) {
            return range_end_frame;
        }
        const auto offset = static_cast<std::int64_t>(offset_value);
        return bounded_start + offset;
    }

    [[nodiscard]] int xForFrame(std::int64_t frame) const noexcept
    {
        const int left = axisLeft();
        const int span = axisWidth();
        if (span <= 0 || frames_per_view <= 1) {
            return left;
        }
        const auto bounded = std::clamp(frame, std::int64_t{0}, range_end_frame);
        if (bounded <= start_frame) {
            return left;
        }
        if (bounded - start_frame >= frames_per_view - 1) {
            return axisRight();
        }
        const auto offset = bounded - start_frame;
        const long double fraction = static_cast<long double>(offset) /
            static_cast<long double>(frames_per_view - 1);
        return left + static_cast<int>(std::llround(fraction * span));
    }
};

std::int64_t firstTickAtOrAfter(std::int64_t start, std::int64_t step) noexcept
{
    if (step <= 1) {
        return start;
    }
    const auto remainder = start % step;
    if (remainder == 0) {
        return start;
    }
    const auto increment = step - remainder;
    return detail::saturatingFrameAdd(start, increment);
}

std::int64_t roundedFrameClamped(long double value, std::int64_t maximum) noexcept
{
    if (value <= 0.0L) {
        return 0;
    }
    if (value >= static_cast<long double>(maximum)) {
        return maximum;
    }
    const auto rounded = std::round(value);
    if (rounded >= static_cast<long double>(maximum)) {
        return maximum;
    }
    return static_cast<std::int64_t>(rounded);
}

struct LayerRow {
    model::LayerId id = 0;
    model::LayerKind kind = model::LayerKind::Image;
    QString name;
    bool visible = true;
    std::int64_t start_frame = 0;
    std::int64_t duration_frames = 0;
    std::int64_t maximum_duration_frames = 0;
    creative_suite::animation::TransformKeyframes keyframes;
};

using TransformProperty = creative_suite::animation::TransformProperty;

constexpr std::array<TransformProperty, 5> kTimelineProperties{{
    TransformProperty::PositionX,
    TransformProperty::PositionY,
    TransformProperty::Scale,
    TransformProperty::Rotation,
    TransformProperty::Opacity,
}};

QString propertyName(TransformProperty property)
{
    switch (property) {
    case TransformProperty::PositionX: return QStringLiteral("Position X");
    case TransformProperty::PositionY: return QStringLiteral("Position Y");
    case TransformProperty::Scale: return QStringLiteral("Scale");
    case TransformProperty::Rotation: return QStringLiteral("Rotation");
    case TransformProperty::Opacity: return QStringLiteral("Opacity");
    }
    return {};
}

QColor propertyColor(TransformProperty property)
{
    switch (property) {
    case TransformProperty::PositionX: return QColor(100, 190, 255);
    case TransformProperty::PositionY: return QColor(130, 225, 180);
    case TransformProperty::Scale: return QColor(210, 160, 255);
    case TransformProperty::Rotation: return QColor(255, 190, 90);
    case TransformProperty::Opacity: return QColor(255, 125, 150);
    }
    return QColor(255, 255, 255);
}

struct VisualTimelineRow {
    enum class Kind : std::uint8_t {
        Layer,
        TransformGroup,
        Property,
    };

    std::size_t layer_index = 0;
    Kind kind = Kind::Layer;
    std::optional<TransformProperty> property;
};

class LayerRowsWidget final : public QWidget {
public:
    explicit LayerRowsWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("motion-timeline-layer-rows"));
        setAcceptDrops(true);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        setMinimumHeight(kTimelineRowHeight);
    }

    void setRows(std::vector<LayerRow> rows)
    {
        rows_ = std::move(rows);
        std::unordered_set<model::LayerId> existing;
        existing.reserve(rows_.size());
        for (const auto& row : rows_) existing.insert(row.id);
        std::erase_if(expanded_layers_, [&existing](model::LayerId id) {
            return !existing.contains(id);
        });
        std::erase_if(expanded_transform_groups_, [&existing](model::LayerId id) {
            return !existing.contains(id);
        });
        rebuildVisualRows();
    }

    void setLayerExpanded(model::LayerId id, bool expanded)
    {
        if (findRow(id) == nullptr) return;
        if (expanded) expanded_layers_.insert(id);
        else expanded_layers_.erase(id);
        rebuildVisualRows();
    }

    void setTransformGroupExpanded(model::LayerId id, bool expanded)
    {
        if (findRow(id) == nullptr) return;
        if (expanded && expanded_layers_.contains(id)) {
            expanded_transform_groups_.insert(id);
        } else {
            expanded_transform_groups_.erase(id);
        }
        rebuildVisualRows();
    }

    void collapseAllLayerTracks()
    {
        expanded_layers_.clear();
        expanded_transform_groups_.clear();
        rebuildVisualRows();
    }

    void setViewState(std::int64_t end_frame,
                      std::int64_t current_frame,
                      std::int64_t start_frame,
                      std::int64_t frames_per_view)
    {
        visible_end_frame_ = std::max<std::int64_t>(0, end_frame);
        current_frame_ = std::clamp(current_frame, std::int64_t{0}, visible_end_frame_);
        view_start_frame_ = std::clamp(start_frame, std::int64_t{0}, visible_end_frame_);
        frames_per_view_ = std::max<std::int64_t>(1, frames_per_view);
        update();
    }

    void setSelectedLayerId(model::LayerId id)
    {
        selected_layer_id_ = id;
        update();
    }

    std::function<void(const std::filesystem::path&, std::int64_t, model::LayerId)> media_drop;
    std::function<void(model::LayerId)> layer_selected;
    std::function<void(model::LayerId, std::int64_t)> layer_move;
    std::function<void(model::LayerId, std::int64_t)> layer_resize;
    std::function<void(model::LayerId, std::size_t)> layer_reorder;
    std::function<void(model::LayerId, bool)> layer_visibility;
    std::function<void(model::LayerId)> layer_remove;
    std::function<void(model::LayerId, TransformProperty, std::int64_t)> keyframe_selected;
    std::function<bool(model::LayerId, TransformProperty, std::int64_t, std::int64_t)>
        keyframe_move;
    std::function<void(int)> zoom_step_requested;
    std::function<void(int)> viewport_width_changed;

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (viewport_width_changed) {
            viewport_width_changed(event->size().width());
        }
    }

    QSize sizeHint() const override
    {
        return {520, std::max(kTimelineRowHeight,
            static_cast<int>(visual_rows_.size()) * kTimelineRowHeight)};
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(31, 35, 41));
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (rows_.empty()) {
            painter.setPen(QColor(170, 176, 184));
            painter.drawText(rect(), Qt::AlignCenter,
                             QStringLiteral("Drag media here to add a layer"));
        }

        const auto mapping = viewMapping();
        for (std::size_t visual_index = 0; visual_index < visual_rows_.size(); ++visual_index) {
            const auto& visual = visual_rows_[visual_index];
            const auto& row = rows_[visual.layer_index];
            const int top = static_cast<int>(visual_index) * kTimelineRowHeight;
            const bool layer_row = visual.kind == VisualTimelineRow::Kind::Layer;
            const bool transform_group =
                visual.kind == VisualTimelineRow::Kind::TransformGroup;
            const bool property_track = visual.kind == VisualTimelineRow::Kind::Property;
            const QRect row_rect(0, top, width(), kTimelineRowHeight);
            const bool selected = row.id == selected_layer_id_;
            QColor row_background;
            if (property_track) {
                row_background = QColor(34, 38, 45);
            } else if (transform_group) {
                row_background = QColor(39, 43, 50);
            } else if (selected) {
                row_background = QColor(64, 69, 78);
            } else {
                row_background = visual.layer_index % 2 == 0
                    ? QColor(39, 43, 50) : QColor(35, 39, 46);
            }
            painter.fillRect(row_rect, row_background);
            painter.fillRect(QRect(0, top, kTimelineHeaderWidth, kTimelineRowHeight),
                property_track ? QColor(39, 43, 50)
                    : (transform_group ? QColor(47, 51, 58) : QColor(43, 47, 54)));
            painter.setPen(QPen(QColor(63, 68, 76), 1));
            painter.drawLine(0, top + kTimelineRowHeight - 1,
                             width(), top + kTimelineRowHeight - 1);

            if (layer_row) {
                const QPoint eye_center(15, top + kTimelineRowHeight / 2);
                painter.setBrush(row.visible ? QColor(210, 215, 222) : Qt::NoBrush);
                painter.setPen(QPen(row.visible ? QColor(210, 215, 222) : QColor(130, 136, 144), 1.5));
                painter.drawEllipse(eye_center, 5, 4);
                if (row.visible) {
                    painter.setPen(QPen(QColor(43, 47, 54), 1));
                    painter.drawPoint(eye_center);
                }
                const bool expanded = expanded_layers_.contains(row.id);
                QPolygon disclosure;
                if (expanded) {
                    disclosure << QPoint(34, top + 13) << QPoint(44, top + 13)
                               << QPoint(39, top + 20);
                } else {
                    disclosure << QPoint(36, top + 11) << QPoint(43, top + 17)
                               << QPoint(36, top + 23);
                }
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(185, 190, 198));
                painter.drawPolygon(disclosure);

                const auto end_frame = row.start_frame > kMaximumFrame - row.duration_frames
                    ? kMaximumFrame : row.start_frame + row.duration_frames;
                if (end_frame >= view_start_frame_ && row.start_frame <= mapping.viewEndFrame()) {
                    const int clip_left = mapping.xForFrame(row.start_frame);
                    const int clip_right = std::max(clip_left + 2, mapping.xForFrame(end_frame));
                    const QRect clip_rect(clip_left, top + 5,
                                          std::max(2, clip_right - clip_left),
                                          kTimelineRowHeight - 10);
                    QColor clip_color;
                    switch (row.kind) {
                    case model::LayerKind::Image: clip_color = QColor(54, 115, 160); break;
                    case model::LayerKind::Video: clip_color = QColor(57, 132, 101); break;
                    case model::LayerKind::Text: clip_color = QColor(91, 89, 174); break;
                    case model::LayerKind::Shape: clip_color = QColor(164, 104, 52); break;
                    }
                    if (!row.visible) clip_color = clip_color.darker(190);
                    painter.setPen(QPen(selected ? QColor(255, 183, 54) : clip_color.lighter(125),
                                        selected ? 2 : 1));
                    painter.setBrush(clip_color);
                    painter.drawRoundedRect(clip_rect, 3, 3);
                    painter.setPen(QColor(245, 247, 250));
                    painter.drawText(clip_rect.adjusted(6, 0, -8, 0),
                                     Qt::AlignLeft | Qt::AlignVCenter, row.name);

                    if (interaction_layer_id_ == row.id &&
                        (interaction_mode_ == InteractionMode::Move ||
                         interaction_mode_ == InteractionMode::Resize)) {
                        const auto preview_start = interaction_mode_ == InteractionMode::Move
                            ? preview_value_ : row.start_frame;
                        const auto preview_duration = interaction_mode_ == InteractionMode::Resize
                            ? preview_value_ : row.duration_frames;
                        const auto preview_end = preview_start > kMaximumFrame - preview_duration
                            ? kMaximumFrame : preview_start + preview_duration;
                        const int preview_left = mapping.xForFrame(preview_start);
                        const int preview_right = std::max(
                            preview_left + 2, mapping.xForFrame(preview_end));
                        painter.setPen(QPen(QColor(255, 183, 54), 2, Qt::DashLine));
                        painter.setBrush(QColor(255, 183, 54, 55));
                        painter.drawRoundedRect(QRect(preview_left, top + 3,
                            std::max(2, preview_right - preview_left), kTimelineRowHeight - 6), 3, 3);
                    }
                }
            } else if (transform_group) {
                const bool expanded = expanded_transform_groups_.contains(row.id);
                QPolygon disclosure;
                if (expanded) {
                    disclosure << QPoint(50, top + 13) << QPoint(60, top + 13)
                               << QPoint(55, top + 20);
                } else {
                    disclosure << QPoint(52, top + 11) << QPoint(59, top + 17)
                               << QPoint(52, top + 23);
                }
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(185, 190, 198));
                painter.drawPolygon(disclosure);
                painter.setPen(QColor(228, 231, 235));
                painter.drawText(QRect(66, top, kTimelineHeaderWidth - 72, kTimelineRowHeight),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 QStringLiteral("Transform"));
            } else {
                const auto property = *visual.property;
                painter.setPen(propertyColor(property));
                painter.drawText(QRect(68, top, kTimelineHeaderWidth - 74, kTimelineRowHeight),
                                 Qt::AlignLeft | Qt::AlignVCenter, propertyName(property));
                const int lane_center_y = top + kTimelineRowHeight / 2;
                painter.setPen(QPen(QColor(68, 74, 83), 1));
                painter.drawLine(mapping.axisLeft(), lane_center_y, width(), lane_center_y);
                const auto& keyframes = creative_suite::animation::keyframesFor(
                    row.keyframes, property);
                for (const auto& keyframe : keyframes) {
                    if (keyframe.frame < 0 || keyframe.frame >= row.duration_frames) continue;
                    const auto composition_frame = row.start_frame + keyframe.frame;
                    if (composition_frame < view_start_frame_ ||
                        composition_frame > mapping.viewEndFrame()) continue;
                    if (interaction_mode_ == InteractionMode::MoveKeyframe &&
                        interaction_layer_id_ == row.id && interaction_property_ == property &&
                        keyframe.frame == interaction_keyframe_frame_ &&
                        preview_value_ != interaction_keyframe_frame_) continue;
                    const int x = mapping.xForFrame(composition_frame);
                    QPolygon diamond;
                    diamond << QPoint(x, lane_center_y - 6) << QPoint(x + 6, lane_center_y)
                            << QPoint(x, lane_center_y + 6) << QPoint(x - 6, lane_center_y);
                    painter.setPen(QPen(propertyColor(property).lighter(135), 1));
                    painter.setBrush(propertyColor(property));
                    painter.drawPolygon(diamond);
                }
                if (interaction_mode_ == InteractionMode::MoveKeyframe &&
                    interaction_layer_id_ == row.id && interaction_property_ == property) {
                    const auto composition_frame = row.start_frame + preview_value_;
                    if (composition_frame >= view_start_frame_ &&
                        composition_frame <= mapping.viewEndFrame()) {
                        const int x = mapping.xForFrame(composition_frame);
                        QPolygon diamond;
                        diamond << QPoint(x, lane_center_y - 7) << QPoint(x + 7, lane_center_y)
                                << QPoint(x, lane_center_y + 7) << QPoint(x - 7, lane_center_y);
                        painter.setPen(QPen(QColor(255, 183, 54), 2));
                        painter.setBrush(propertyColor(property));
                        painter.drawPolygon(diamond);
                    }
                }
            }
        }

        if (current_frame_ >= view_start_frame_ && current_frame_ <= mapping.viewEndFrame()) {
            const int playhead_x = mapping.xForFrame(current_frame_);
            painter.setPen(QPen(QColor(255, 183, 54), 1));
            painter.drawLine(playhead_x, 0, playhead_x, height());
        }

        if (drag_preview_active_) {
            const int visual = rowAtY(drag_preview_y_);
            const int top = visual >= 0 ? visual * kTimelineRowHeight : 0;
            painter.setPen(QPen(QColor(255, 183, 54), 2, Qt::DashLine));
            painter.drawLine(drag_preview_x_, top, drag_preview_x_, top + kTimelineRowHeight);
            painter.setBrush(QColor(255, 183, 54, 55));
            painter.setPen(QPen(QColor(255, 183, 54), 1, Qt::DashLine));
            painter.drawRoundedRect(QRect(drag_preview_x_, top + 5, 74,
                kTimelineRowHeight - 10), 3, 3);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        setFocus(Qt::MouseFocusReason);
        const auto position = event->position().toPoint();
        const int row_index = rowAtY(position.y());
        if (row_index < 0) return;
        const auto& visual = visual_rows_[static_cast<std::size_t>(row_index)];
        const auto& row = rows_[visual.layer_index];
        if (visual.kind == VisualTimelineRow::Kind::Layer && position.x() < 29) {
            if (layer_visibility) layer_visibility(row.id, !row.visible);
            event->accept();
            return;
        }
        if (layer_selected) layer_selected(row.id);
        interaction_layer_id_ = row.id;
        press_position_ = position;
        if (visual.kind == VisualTimelineRow::Kind::Property) {
            const auto property = *visual.property;
            const int lane_center_y = row_index * kTimelineRowHeight + kTimelineRowHeight / 2;
            const auto& keyframes = creative_suite::animation::keyframesFor(
                row.keyframes, property);
            for (const auto& keyframe : keyframes) {
                if (keyframe.frame < 0 || keyframe.frame >= row.duration_frames) continue;
                const int marker_x = viewMapping().xForFrame(row.start_frame + keyframe.frame);
                if (std::abs(position.x() - marker_x) <= 8 &&
                    std::abs(position.y() - lane_center_y) <= 9) {
                    interaction_mode_ = InteractionMode::MoveKeyframe;
                    interaction_property_ = property;
                    interaction_keyframe_frame_ = keyframe.frame;
                    preview_value_ = keyframe.frame;
                    event->accept();
                    return;
                }
            }
            interaction_mode_ = InteractionMode::None;
            interaction_layer_id_ = 0;
            event->accept();
            return;
        }
        if (visual.kind == VisualTimelineRow::Kind::Layer && position.x() < 48) {
            setLayerExpanded(row.id, !expanded_layers_.contains(row.id));
            interaction_layer_id_ = 0;
            event->accept();
            return;
        }
        if (visual.kind == VisualTimelineRow::Kind::TransformGroup) {
            if (position.x() < 64) {
                setTransformGroupExpanded(
                    row.id, !expanded_transform_groups_.contains(row.id));
            }
            interaction_layer_id_ = 0;
            event->accept();
            return;
        }
        if (press_position_.x() < kTimelineHeaderWidth) {
            interaction_mode_ = InteractionMode::Reorder;
            target_row_ = static_cast<int>(visual.layer_index);
        } else {
            const auto end_frame = row.start_frame > kMaximumFrame - row.duration_frames
                ? kMaximumFrame : row.start_frame + row.duration_frames;
            const int right = viewMapping().xForFrame(end_frame);
            const int left = viewMapping().xForFrame(row.start_frame);
            const int visible_right = std::max(left + 2, right);
            if (press_position_.x() < left || press_position_.x() > visible_right) {
                event->accept();
                return;
            }
            const int clip_width = visible_right - left;
            const int handle_width = std::min(kTimelineClipHandleWidth,
                                                std::max(1, clip_width / 3));
            interaction_mode_ = press_position_.x() >= visible_right - handle_width
                ? InteractionMode::Resize : InteractionMode::Move;
            grabbed_frame_offset_ = frameAtX(press_position_.x()) - row.start_frame;
            preview_value_ = interaction_mode_ == InteractionMode::Move
                ? row.start_frame : row.duration_frames;
        }
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const int x = event->position().toPoint().x();
        const int y = event->position().toPoint().y();
        if ((event->buttons() & Qt::LeftButton) != 0 &&
            interaction_mode_ != InteractionMode::None) {
            if (interaction_mode_ == InteractionMode::Reorder) {
                const int visual_index = rowAtY(y);
                if (visual_index >= 0) {
                    target_row_ = static_cast<int>(
                        visual_rows_[static_cast<std::size_t>(visual_index)].layer_index);
                }
            } else if (interaction_mode_ == InteractionMode::MoveKeyframe) {
                if (const auto* row = findRow(interaction_layer_id_);
                    row != nullptr && row->duration_frames > 0) {
                    const auto composition_frame = frameAtX(x);
                    preview_value_ = std::clamp(
                        composition_frame - row->start_frame,
                        std::int64_t{0}, row->duration_frames - 1);
                }
            } else if (const auto* row = findRow(interaction_layer_id_); row != nullptr) {
                if (interaction_mode_ == InteractionMode::Move) {
                    const auto raw = std::max<std::int64_t>(0,
                        frameAtX(x) - grabbed_frame_offset_);
                    preview_value_ = snappedFrame(raw, interaction_layer_id_);
                } else {
                    auto boundary = snappedFrame(frameAtX(x), interaction_layer_id_);
                    auto duration = std::max<std::int64_t>(1, boundary - row->start_frame);
                    if (row->maximum_duration_frames > 0) {
                        duration = std::min(duration, row->maximum_duration_frames);
                    }
                    duration = std::min(duration,
                        kMaximumFrame - row->start_frame);
                    preview_value_ = std::max<std::int64_t>(1, duration);
                }
            }
            update();
            event->accept();
            return;
        }
        if (interaction_mode_ == InteractionMode::None) {
            const int row_index = rowAtY(y);
            if (row_index >= 0 &&
                visual_rows_[static_cast<std::size_t>(row_index)].kind ==
                    VisualTimelineRow::Kind::Layer &&
                x >= kTimelineHeaderWidth) {
                const auto& row = rows_[visual_rows_[static_cast<std::size_t>(row_index)].layer_index];
                const auto end_frame = row.start_frame > kMaximumFrame - row.duration_frames
                    ? kMaximumFrame : row.start_frame + row.duration_frames;
                const int left = viewMapping().xForFrame(row.start_frame);
                const int right = viewMapping().xForFrame(end_frame);
                const int visible_right = std::max(left + 2, right);
                if (x < left || x > visible_right) {
                    unsetCursor();
                } else {
                    const int clip_width = visible_right - left;
                    const int handle_width = std::min(kTimelineClipHandleWidth,
                                                        std::max(1, clip_width / 3));
                    setCursor(x >= visible_right - handle_width
                        ? Qt::SizeHorCursor : Qt::OpenHandCursor);
                }
            } else {
                unsetCursor();
            }
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || interaction_mode_ == InteractionMode::None) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        const auto id = interaction_layer_id_;
        const auto mode = interaction_mode_;
        const auto value = preview_value_;
        const auto target = target_row_;
        const auto property = interaction_property_;
        const auto old_keyframe = interaction_keyframe_frame_;
        const bool moved = (event->position().toPoint() - press_position_).manhattanLength() >= 4;
        interaction_mode_ = InteractionMode::None;
        interaction_layer_id_ = 0;
        update();
        if (mode == InteractionMode::Reorder && moved && target >= 0 && layer_reorder) {
            layer_reorder(id, static_cast<std::size_t>(target));
        } else if (mode == InteractionMode::Move && moved && layer_move) {
            layer_move(id, value);
        } else if (mode == InteractionMode::Resize && moved && layer_resize) {
            layer_resize(id, value);
        } else if (mode == InteractionMode::MoveKeyframe) {
            if (moved && value != old_keyframe) {
                if (keyframe_move) keyframe_move(id, property, old_keyframe, value);
            } else if (keyframe_selected) {
                keyframe_selected(id, property, old_keyframe);
            }
        }
        event->accept();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            const int delta = event->angleDelta().y() != 0
                ? event->angleDelta().y() : event->pixelDelta().y();
            if (delta != 0 && zoom_step_requested) {
                zoom_step_requested(delta > 0 ? 1 : -1);
                event->accept();
                return;
            }
        }
        event->ignore();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Delete && selected_layer_id_ != 0 && layer_remove) {
            layer_remove(selected_layer_id_);
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasFormat(kMotionMediaMimeType) && media_drop) {
            event->acceptProposedAction();
            drag_preview_active_ = true;
            const int x = event->position().toPoint().x();
            drag_preview_x_ = viewMapping().xForFrame(snappedFrame(frameAtX(x), 0));
            drag_preview_y_ = event->position().toPoint().y();
            update();
            return;
        }
        event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (!event->mimeData()->hasFormat(kMotionMediaMimeType) || !media_drop) {
            event->ignore();
            return;
        }
        const auto frame = snappedFrame(frameAtX(event->position().toPoint().x()), 0);
        drag_preview_x_ = viewMapping().xForFrame(frame);
        drag_preview_y_ = event->position().toPoint().y();
        drag_preview_active_ = true;
        update();
        event->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override
    {
        drag_preview_active_ = false;
        update();
        event->accept();
    }

    void dropEvent(QDropEvent* event) override
    {
        if (!event->mimeData()->hasFormat(kMotionMediaMimeType) || !media_drop) {
            event->ignore();
            return;
        }
        const auto position = event->position().toPoint();
        const auto frame = snappedFrame(frameAtX(position.x()), 0);
        const int row_index = rowAtY(position.y());
        const auto before = row_index >= 0
            ? rows_[visual_rows_[static_cast<std::size_t>(row_index)].layer_index].id
            : model::LayerId{0};
        const auto path = pathFromQString(QString::fromUtf8(
            event->mimeData()->data(kMotionMediaMimeType)));
        drag_preview_active_ = false;
        update();
        media_drop(path, frame, before);
        event->acceptProposedAction();
    }

private:
    enum class InteractionMode { None, Move, Resize, Reorder, MoveKeyframe };

    [[nodiscard]] int rowAtY(int y) const noexcept
    {
        if (y < 0) return -1;
        const auto index = y / kTimelineRowHeight;
        return index >= 0 && static_cast<std::size_t>(index) < visual_rows_.size() ? index : -1;
    }

    [[nodiscard]] const LayerRow* findRow(model::LayerId id) const noexcept
    {
        const auto found = std::find_if(rows_.begin(), rows_.end(), [id](const LayerRow& row) {
            return row.id == id;
        });
        return found == rows_.end() ? nullptr : &*found;
    }

    void rebuildVisualRows()
    {
        visual_rows_.clear();
        visual_rows_.reserve(rows_.size() * 7);
        for (std::size_t index = 0; index < rows_.size(); ++index) {
            const auto layer_id = rows_[index].id;
            visual_rows_.push_back({index, VisualTimelineRow::Kind::Layer, std::nullopt});
            if (!expanded_layers_.contains(rows_[index].id)) continue;
            visual_rows_.push_back(
                {index, VisualTimelineRow::Kind::TransformGroup, std::nullopt});
            if (!expanded_transform_groups_.contains(layer_id)) continue;
            for (const auto property : kTimelineProperties) {
                visual_rows_.push_back({index, VisualTimelineRow::Kind::Property, property});
            }
        }
        const auto row_count = static_cast<int>(visual_rows_.size());
        const int content_height = std::max(kTimelineRowHeight, row_count * kTimelineRowHeight);
        setMinimumHeight(content_height);
        resize(width(), content_height);
        updateGeometry();
        update();
    }

    [[nodiscard]] TimelineViewMapping viewMapping() const noexcept
    {
        return {width(), kTimelineHeaderWidth, visible_end_frame_,
                view_start_frame_, frames_per_view_};
    }

    [[nodiscard]] std::int64_t frameAtX(int x) const noexcept
    {
        return viewMapping().frameAtX(x);
    }

    [[nodiscard]] std::int64_t snappedFrame(
        std::int64_t frame,
        model::LayerId excluded) const noexcept
    {
        frame = std::clamp(frame, std::int64_t{0}, visible_end_frame_);
        const auto mapping = viewMapping();
        const int span = std::max(1, mapping.axisWidth());
        const auto threshold = std::max<std::int64_t>(1,
            static_cast<std::int64_t>(std::ceil(
                static_cast<long double>(kTimelineSnapTolerancePixels) *
                static_cast<long double>(frames_per_view_ - 1) / span)));
        auto snapped = frame;
        auto best_distance = std::numeric_limits<std::int64_t>::max();
        const auto consider = [&](std::int64_t candidate) {
            candidate = std::clamp(candidate, std::int64_t{0}, visible_end_frame_);
            const auto distance = candidate > frame ? candidate - frame : frame - candidate;
            if (distance <= threshold && distance < best_distance) {
                best_distance = distance;
                snapped = candidate;
            }
        };
        consider(0);
        for (const auto& row : rows_) {
            if (row.id == excluded) continue;
            consider(row.start_frame);
            if (row.start_frame <= kMaximumFrame - row.duration_frames)
                consider(row.start_frame + row.duration_frames);
        }
        return snapped;
    }

    std::vector<LayerRow> rows_;
    std::vector<VisualTimelineRow> visual_rows_;
    std::unordered_set<model::LayerId> expanded_layers_;
    std::unordered_set<model::LayerId> expanded_transform_groups_;
    std::int64_t visible_end_frame_ = 0;
    std::int64_t current_frame_ = 0;
    std::int64_t view_start_frame_ = 0;
    std::int64_t frames_per_view_ = 1;
    model::LayerId selected_layer_id_ = 0;
    InteractionMode interaction_mode_ = InteractionMode::None;
    model::LayerId interaction_layer_id_ = 0;
    TransformProperty interaction_property_ = TransformProperty::PositionX;
    std::int64_t interaction_keyframe_frame_ = 0;
    QPoint press_position_;
    std::int64_t grabbed_frame_offset_ = 0;
    std::int64_t preview_value_ = 0;
    int target_row_ = -1;
    bool drag_preview_active_ = false;
    int drag_preview_x_ = 0;
    int drag_preview_y_ = 0;
};

} // namespace

TimelineRuler::TimelineRuler(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("motion-timeline-ruler"));
    setMinimumHeight(82);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
}

void TimelineRuler::setHeaderWidth(int width)
{
    header_width_ = std::max(0, width);
    update();
}

void TimelineRuler::setMappingWidth(int width)
{
    mapping_width_ = std::max(0, width);
    update();
}

int TimelineRuler::mappingWidth() const noexcept
{
    return mapping_width_ > 0 ? mapping_width_ : width();
}

void TimelineRuler::setViewState(std::int64_t end_frame,
                                 std::int64_t current_frame,
                                 std::int64_t start_frame,
                                 std::int64_t frames_per_view,
                                 model::FrameRate frame_rate,
                                 TimelineDisplayMode display_mode)
{
    visible_end_frame_ = std::max<std::int64_t>(0, end_frame);
    current_frame_ = std::clamp(current_frame, std::int64_t{0}, visible_end_frame_);
    view_start_frame_ = std::clamp(start_frame, std::int64_t{0}, visible_end_frame_);
    frames_per_view_ = std::max<std::int64_t>(1, frames_per_view);
    frame_rate_ = frame_rate;
    display_mode_ = display_mode;
    update();
}

void TimelineRuler::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(35, 39, 46));
    painter.setRenderHint(QPainter::Antialiasing, true);

    const TimelineViewMapping mapping{
        mappingWidth(), header_width_, visible_end_frame_, view_start_frame_, frames_per_view_};
    const int axis_left = mapping.axisLeft();
    const int axis_right = mapping.axisRight();
    const int axis_y = 48;
    const int label_y = 17;

    painter.setPen(QPen(QColor(110, 118, 129), 1));
    painter.drawLine(axis_left, axis_y, axis_right, axis_y);

    const auto view_end = mapping.viewEndFrame();
    const QFontMetrics font_metrics(painter.font());
    const int label_width = std::max(
        font_metrics.horizontalAdvance(formatTimelinePosition(
            view_start_frame_, frame_rate_, display_mode_)),
        font_metrics.horizontalAdvance(formatTimelinePosition(
            view_end, frame_rate_, display_mode_))) + 12;
    const int label_box_width = std::max(96, label_width);
    const auto tick_step = detail::timelineRulerTickStep(
        frames_per_view_, mapping.axisWidth(), label_width);
    auto tick = firstTickAtOrAfter(view_start_frame_, tick_step);
    if (view_start_frame_ <= view_end && tick > view_end) {
        tick = view_start_frame_;
    }
    int previous_tick_x = std::numeric_limits<int>::min();
    const auto drawTick = [&](std::int64_t frame) {
        const int x = mapping.xForFrame(frame);
        if (x == previous_tick_x) {
            return;
        }
        previous_tick_x = x;
        painter.drawLine(x, axis_y - 5, x, axis_y + 5);
        painter.setPen(QColor(196, 201, 208));
        const auto label = formatTimelinePosition(frame, frame_rate_, display_mode_);
        painter.drawText(
            QRect(x - label_box_width / 2, label_y, label_box_width, 18),
            Qt::AlignHCenter | Qt::AlignVCenter,
            label);
        painter.setPen(QPen(QColor(110, 118, 129), 1));
    };
    if (view_start_frame_ <= view_end && view_start_frame_ % tick_step != 0) {
        drawTick(view_start_frame_);
    }
    for (auto frame = tick; frame <= view_end;) {
        drawTick(frame);
        if (frame > kMaximumFrame - tick_step) {
            break;
        }
        frame += tick_step;
    }
    if (view_end > view_start_frame_ && view_end % tick_step != 0) {
        drawTick(view_end);
    }

    if (current_frame_ >= view_start_frame_ && current_frame_ <= view_end) {
        const int playhead_x = mapping.xForFrame(current_frame_);
        painter.setPen(QPen(QColor(255, 183, 54), 2));
        painter.drawLine(playhead_x, 10, playhead_x, height() - 8);
        painter.setBrush(QColor(255, 183, 54));
        painter.setPen(Qt::NoPen);
        QPolygon marker;
        marker << QPoint(playhead_x - 6, 8) << QPoint(playhead_x + 6, 8)
               << QPoint(playhead_x, 17);
        painter.drawPolygon(marker);
    }
}

void TimelineRuler::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    range_extended_during_drag_ = false;
    last_mouse_x_ = event->position().toPoint().x();
    emit seekRequested(static_cast<qint64>(frameAtX(last_mouse_x_)));
    event->accept();
}

void TimelineRuler::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_ || (event->buttons() & Qt::LeftButton) == 0) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const int mouse_x = event->position().toPoint().x();
    if (mouse_x == last_mouse_x_) {
        event->accept();
        return;
    }
    last_mouse_x_ = mouse_x;
    handleDragX(mouse_x);
    event->accept();
}

void TimelineRuler::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        const int mouse_x = event->position().toPoint().x();
        if (mouse_x != last_mouse_x_) {
            last_mouse_x_ = mouse_x;
            handleDragX(mouse_x);
        }
        last_mouse_x_ = -1;
        range_extended_during_drag_ = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TimelineRuler::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const int delta = event->angleDelta().y() != 0
            ? event->angleDelta().y() : event->pixelDelta().y();
        if (delta != 0) {
            emit zoomStepRequested(delta > 0 ? 1 : -1);
            event->accept();
            return;
        }
    }
    event->ignore();
}

std::int64_t TimelineRuler::frameAtX(int x) const noexcept
{
    return TimelineViewMapping{
        mappingWidth(), header_width_, visible_end_frame_, view_start_frame_, frames_per_view_
    }.frameAtX(x);
}

int TimelineRuler::xForFrame(std::int64_t frame) const noexcept
{
    return TimelineViewMapping{
        mappingWidth(), header_width_, visible_end_frame_, view_start_frame_, frames_per_view_
    }.xForFrame(frame);
}

void TimelineRuler::handleDragX(int x)
{
    const TimelineViewMapping mapping{
        mappingWidth(), header_width_, visible_end_frame_, view_start_frame_, frames_per_view_};
    const bool view_at_range_end = mapping.viewEndFrame() >= visible_end_frame_;
    const int range_end_x = mapping.xForFrame(visible_end_frame_);
    if (view_at_range_end && x > range_end_x) {
        if (!range_extended_during_drag_) {
            range_extended_during_drag_ = true;
            emit extendRangeRequested();
        }
        return;
    }
    emit seekRequested(static_cast<qint64>(frameAtX(x)));
}

TimelineNavigator::TimelineNavigator(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("motion-timeline"));
    setMinimumHeight(196);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(4);

    auto* controls = new QHBoxLayout();
    controls->setContentsMargins(0, 0, 0, 0);
    previous_frame_button_ = new QPushButton(QStringLiteral("Previous frame"), this);
    previous_frame_button_->setObjectName(QStringLiteral("motion-timeline-previous-frame"));
    play_pause_button_ = new QPushButton(QStringLiteral("Play"), this);
    play_pause_button_->setObjectName(QStringLiteral("motion-timeline-play-pause"));
    play_pause_button_->setToolTip(QStringLiteral("Play or pause the composition"));
    loop_button_ = new QPushButton(QStringLiteral("Loop"), this);
    loop_button_->setObjectName(QStringLiteral("motion-timeline-loop"));
    loop_button_->setCheckable(true);
    loop_button_->setToolTip(QStringLiteral("Restart playback from frame 0 at the end"));
    display_mode_combo_ = new QComboBox(this);
    display_mode_combo_->setObjectName(QStringLiteral("motion-timeline-display-mode"));
    display_mode_combo_->addItem(QStringLiteral("Time"),
        static_cast<int>(TimelineDisplayMode::Time));
    display_mode_combo_->addItem(QStringLiteral("Frames"),
        static_cast<int>(TimelineDisplayMode::Frames));
    display_mode_combo_->setToolTip(QStringLiteral("Timeline ruler and playhead display"));
    frame_label_ = new QLabel(this);
    frame_label_->setObjectName(QStringLiteral("motion-timeline-position-readout"));
    zoom_out_button_ = new QPushButton(QStringLiteral("−"), this);
    zoom_out_button_->setObjectName(QStringLiteral("motion-timeline-zoom-out"));
    zoom_out_button_->setToolTip(QStringLiteral("Zoom out"));
    zoom_slider_ = new QSlider(Qt::Horizontal, this);
    zoom_slider_->setObjectName(QStringLiteral("motion-timeline-zoom-slider"));
    zoom_slider_->setRange(0, static_cast<int>(detail::kTimelineZoomLevels.size()) - 1);
    zoom_slider_->setValue(detail::kTimelineZoomDefaultIndex);
    zoom_slider_->setFixedWidth(150);
    zoom_slider_->setToolTip(QStringLiteral("Timeline zoom"));
    zoom_level_label_ = new QLabel(this);
    zoom_level_label_->setObjectName(QStringLiteral("motion-timeline-zoom-level"));
    zoom_level_label_->setMinimumWidth(52);
    zoom_level_label_->setAlignment(Qt::AlignCenter);
    zoom_in_button_ = new QPushButton(QStringLiteral("+"), this);
    zoom_in_button_->setObjectName(QStringLiteral("motion-timeline-zoom-in"));
    zoom_in_button_->setToolTip(QStringLiteral("Zoom in"));
    frame_rate_label_ = new QLabel(this);
    frame_rate_label_->setObjectName(QStringLiteral("motion-timeline-frame-rate"));
    next_frame_button_ = new QPushButton(QStringLiteral("Next frame"), this);
    next_frame_button_->setObjectName(QStringLiteral("motion-timeline-next-frame"));
    controls->addWidget(previous_frame_button_);
    controls->addWidget(play_pause_button_);
    controls->addWidget(loop_button_);
    controls->addWidget(display_mode_combo_);
    controls->addWidget(frame_label_);
    controls->addStretch(1);
    controls->addWidget(zoom_out_button_);
    controls->addWidget(zoom_slider_);
    controls->addWidget(zoom_level_label_);
    controls->addWidget(zoom_in_button_);
    controls->addStretch(1);
    controls->addWidget(frame_rate_label_);
    controls->addWidget(next_frame_button_);
    layout->addLayout(controls);

    ruler_ = new TimelineRuler(this);
    ruler_->setHeaderWidth(kTimelineHeaderWidth);
    ruler_->setToolTip(QStringLiteral(
        "At 100%, the initial one-hour range fits the timeline. Use Ctrl+wheel or the zoom "
        "controls to change scale. Drag past the actual range end to extend by one hour per "
        "gesture; scroll to the end first if it is offscreen. This range is not the composition end."));
    layout->addWidget(ruler_);

    layer_scroll_area_ = new QScrollArea(this);
    layer_scroll_area_->setObjectName(QStringLiteral("motion-timeline-layer-scroll"));
    layer_scroll_area_->setWidgetResizable(true);
    layer_scroll_area_->setFrameShape(QFrame::NoFrame);
    layer_scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layer_rows_ = new LayerRowsWidget(layer_scroll_area_);
    layer_scroll_area_->setWidget(layer_rows_);
    layout->addWidget(layer_scroll_area_, 1);

    horizontal_scroll_bar_ = new QScrollBar(Qt::Horizontal, this);
    horizontal_scroll_bar_->setObjectName(QStringLiteral("motion-timeline-horizontal-scroll"));
    horizontal_scroll_bar_->setRange(0, detail::kTimelineScrollResolution);
    horizontal_scroll_bar_->setPageStep(detail::kTimelineScrollResolution);
    layout->addWidget(horizontal_scroll_bar_);

    playback_timer_ = new QTimer(this);
    playback_timer_->setObjectName(QStringLiteral("motion-timeline-playback-timer"));
    playback_timer_->setTimerType(Qt::PreciseTimer);
    connect(playback_timer_, &QTimer::timeout, this, [this] { playbackTick(); });

    connect(previous_frame_button_, &QPushButton::clicked, this, [this] {
        if (previous_frame_action_ != nullptr) previous_frame_action_->trigger();
        else seekToFrame(current_frame_ - (current_frame_ > 0 ? 1 : 0));
    });
    connect(play_pause_button_, &QPushButton::clicked, this, [this] {
        if (play_pause_action_ != nullptr) play_pause_action_->trigger();
        else if (playing_) pausePlayback();
        else startPlayback();
    });
    connect(loop_button_, &QPushButton::toggled, this, [this](bool enabled) {
        if (loop_action_ != nullptr) loop_action_->setChecked(enabled);
        else loop_enabled_ = enabled;
    });
    connect(next_frame_button_, &QPushButton::clicked, this, [this] {
        if (next_frame_action_ != nullptr) next_frame_action_->trigger();
        else if (current_frame_ < visible_end_frame_) {
            seekToFrame(current_frame_ + 1);
        }
    });
    connect(display_mode_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int index) {
            display_mode_ = index == static_cast<int>(TimelineDisplayMode::Frames)
                ? TimelineDisplayMode::Frames : TimelineDisplayMode::Time;
            updateViewWidgets();
            updateControls();
        });
    connect(ruler_, &TimelineRuler::seekRequested, this, [this](qint64 frame) {
        seekToFrame(static_cast<std::int64_t>(frame));
    });
    connect(ruler_, &TimelineRuler::extendRangeRequested, this, [this] {
        extendViewByOneHour();
    });
    connect(ruler_, &TimelineRuler::zoomStepRequested, this, [this](int direction) {
        applyZoomLevel(zoom_level_index_ + (direction > 0 ? 1 : -1));
    });
    static_cast<LayerRowsWidget*>(layer_rows_)->zoom_step_requested = [this](int direction) {
        applyZoomLevel(zoom_level_index_ + direction);
    };
    static_cast<LayerRowsWidget*>(layer_rows_)->viewport_width_changed = [this](int width) {
        ruler_->setMappingWidth(width);
    };
    connect(zoom_slider_, &QSlider::valueChanged, this, [this](int index) {
        applyZoomLevel(index);
    });
    connect(zoom_out_button_, &QPushButton::clicked, this, [this] {
        if (zoom_out_action_ != nullptr) zoom_out_action_->trigger();
        else applyZoomLevel(zoom_level_index_ - 1);
    });
    connect(zoom_in_button_, &QPushButton::clicked, this, [this] {
        if (zoom_in_action_ != nullptr) zoom_in_action_->trigger();
        else applyZoomLevel(zoom_level_index_ + 1);
    });
    connect(horizontal_scroll_bar_, &QScrollBar::valueChanged, this, [this](int value) {
        const auto maximum_start = maximumViewStartFrame();
        const long double fraction = static_cast<long double>(value) /
            static_cast<long double>(detail::kTimelineScrollResolution);
        view_start_frame_ = roundedFrameClamped(
            fraction * static_cast<long double>(maximum_start), maximum_start);
        updateViewWidgets();
    });

    frames_per_view_ = calculateFramesPerView();
    updateHorizontalScrollBar();
    updateViewWidgets();
    updateControls();
}

void TimelineNavigator::setShortcutActions(
    QAction* play_pause,
    QAction* previous_frame,
    QAction* next_frame,
    QAction* loop,
    QAction* zoom_in,
    QAction* zoom_out)
{
    play_pause_action_ = play_pause;
    previous_frame_action_ = previous_frame;
    next_frame_action_ = next_frame;
    loop_action_ = loop;
    zoom_in_action_ = zoom_in;
    zoom_out_action_ = zoom_out;

    if (play_pause_action_ != nullptr) {
        connect(play_pause_action_, &QAction::triggered, this, [this] {
            if (playing_) pausePlayback();
            else startPlayback();
        });
    }
    if (previous_frame_action_ != nullptr) {
        connect(previous_frame_action_, &QAction::triggered, this, [this] {
            seekToFrame(current_frame_ - (current_frame_ > 0 ? 1 : 0));
        });
    }
    if (next_frame_action_ != nullptr) {
        connect(next_frame_action_, &QAction::triggered, this, [this] {
            if (current_frame_ < visible_end_frame_) seekToFrame(current_frame_ + 1);
        });
    }
    if (loop_action_ != nullptr) {
        connect(loop_action_, &QAction::toggled, this, [this](bool enabled) {
            loop_enabled_ = enabled;
            const QSignalBlocker blocker(loop_button_);
            loop_button_->setChecked(enabled);
        });
    }
    if (zoom_in_action_ != nullptr) {
        connect(zoom_in_action_, &QAction::triggered, this, [this] {
            applyZoomLevel(zoom_level_index_ + 1);
        });
    }
    if (zoom_out_action_ != nullptr) {
        connect(zoom_out_action_, &QAction::triggered, this, [this] {
            applyZoomLevel(zoom_level_index_ - 1);
        });
    }
    updateControls();
}

void TimelineNavigator::setCompositionTiming(
    model::FrameRate frame_rate)
{
    pausePlayback(false);
    static_cast<LayerRowsWidget*>(layer_rows_)->collapseAllLayerTracks();
    frame_rate_ = frame_rate;
    current_frame_ = 0;
    playback_start_frame_ = 0;
    playback_end_frame_exclusive_ = 0;
    loop_enabled_ = false;
    {
        const QSignalBlocker blocker(loop_button_);
        loop_button_->setChecked(false);
    }
    if (loop_action_ != nullptr) loop_action_->setChecked(false);
    display_mode_ = TimelineDisplayMode::Time;
    {
        const QSignalBlocker blocker(display_mode_combo_);
        display_mode_combo_->setCurrentIndex(static_cast<int>(display_mode_));
    }
    visible_end_frame_ = initialVisibleEndFrame(frame_rate);
    zoom_level_index_ = detail::kTimelineZoomDefaultIndex;
    zoom_factor_ = detail::kTimelineZoomLevels[static_cast<std::size_t>(zoom_level_index_)];
    frames_per_view_ = calculateFramesPerView();
    view_start_frame_ = 0;
    updateHorizontalScrollBar();
    updateViewWidgets();
    updateControls();
}

void TimelineNavigator::setCurrentFrame(std::int64_t frame)
{
    seekToFrame(frame);
}

std::int64_t TimelineNavigator::currentFrame() const noexcept
{
    return current_frame_;
}

bool TimelineNavigator::isPlaying() const noexcept
{
    return playing_;
}

bool TimelineNavigator::isLoopEnabled() const noexcept
{
    return loop_enabled_;
}

std::int64_t TimelineNavigator::visibleEndFrame() const noexcept
{
    return visible_end_frame_;
}

double TimelineNavigator::zoomFactor() const noexcept
{
    return zoom_factor_;
}

int TimelineNavigator::zoomLevelIndex() const noexcept
{
    return zoom_level_index_;
}

std::int64_t TimelineNavigator::viewStartFrame() const noexcept
{
    return view_start_frame_;
}

std::int64_t TimelineNavigator::framesPerView() const noexcept
{
    return frames_per_view_;
}

int TimelineNavigator::frameToViewportX(std::int64_t frame) const noexcept
{
    if (ruler_ == nullptr) {
        return 0;
    }
    return TimelineViewMapping{ruler_->mappingWidth(), kTimelineHeaderWidth, visible_end_frame_,
        view_start_frame_, frames_per_view_}.xForFrame(frame);
}

std::int64_t TimelineNavigator::frameAtViewportX(int x) const noexcept
{
    if (ruler_ == nullptr) {
        return 0;
    }
    return TimelineViewMapping{ruler_->mappingWidth(), kTimelineHeaderWidth, visible_end_frame_,
        view_start_frame_, frames_per_view_}.frameAtX(x);
}

void TimelineNavigator::setLayers(const std::vector<model::CompositionLayer>& layers)
{
    std::vector<LayerRow> rows;
    rows.reserve(layers.size());
    playback_end_frame_exclusive_ = 0;
    const auto maximum_frame = std::numeric_limits<std::int64_t>::max();
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        rows.push_back(LayerRow{
            layer->id,
            layer->kind,
            utf8Text(layer->name),
            layer->visible,
            layer->timeline_start_frame,
            layer->duration_frames,
            layer->maximum_timeline_duration_frames,
            layer->keyframes});
        if (layer->timeline_start_frame >= 0 && layer->duration_frames > 0) {
            const auto end_frame = layer->duration_frames >
                    maximum_frame - layer->timeline_start_frame
                ? maximum_frame
                : layer->timeline_start_frame + layer->duration_frames;
            playback_end_frame_exclusive_ = std::max(
                playback_end_frame_exclusive_, end_frame);
        }
    }
    auto* row_widget = static_cast<LayerRowsWidget*>(layer_rows_);
    row_widget->setRows(std::move(rows));
    if (playing_ && (playback_end_frame_exclusive_ == 0 ||
        current_frame_ >= playback_end_frame_exclusive_ - 1)) {
        pausePlayback(false);
        if (playback_end_frame_exclusive_ > 0) {
            setPlayheadFrame(playback_end_frame_exclusive_ - 1);
        }
    }
    updateViewWidgets();
    updateControls();
}

void TimelineNavigator::setSelectedLayerId(model::LayerId id)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->setSelectedLayerId(id);
}

void TimelineNavigator::setLayerExpanded(model::LayerId id, bool expanded)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->setLayerExpanded(id, expanded);
}

void TimelineNavigator::setTransformGroupExpanded(model::LayerId id, bool expanded)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->setTransformGroupExpanded(id, expanded);
}

void TimelineNavigator::setMediaDropHandler(
    std::function<void(const std::filesystem::path&, std::int64_t, model::LayerId)> handler)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->media_drop = std::move(handler);
}

void TimelineNavigator::setLayerSelectedHandler(
    std::function<void(model::LayerId)> handler)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->layer_selected = std::move(handler);
}

void TimelineNavigator::setLayerMoveHandler(
    std::function<void(model::LayerId, std::int64_t)> handler)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->layer_move = std::move(handler);
}

void TimelineNavigator::setLayerResizeHandler(
    std::function<void(model::LayerId, std::int64_t)> handler)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->layer_resize = std::move(handler);
}

void TimelineNavigator::setLayerReorderHandler(
    std::function<void(model::LayerId, std::size_t)> handler)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->layer_reorder = std::move(handler);
}

void TimelineNavigator::setLayerVisibilityHandler(
    std::function<void(model::LayerId, bool)> handler)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->layer_visibility = std::move(handler);
}

void TimelineNavigator::setLayerRemoveHandler(
    std::function<void(model::LayerId)> handler)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->layer_remove = std::move(handler);
}

void TimelineNavigator::setKeyframeSelectedHandler(
    std::function<void(model::LayerId,
                       creative_suite::animation::TransformProperty,
                       std::int64_t)> handler)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->keyframe_selected = std::move(handler);
}

void TimelineNavigator::setKeyframeMoveHandler(
    std::function<bool(model::LayerId,
                       creative_suite::animation::TransformProperty,
                       std::int64_t,
                       std::int64_t)> handler)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->keyframe_move = std::move(handler);
}

void TimelineNavigator::seekToFrame(std::int64_t frame)
{
    if (playing_) pausePlayback(false);
    const auto bounded_frame = std::clamp(frame, std::int64_t{0}, visible_end_frame_);
    setPlayheadFrame(bounded_frame);
}

void TimelineNavigator::startPlayback()
{
    if (playing_ || playback_end_frame_exclusive_ <= 0 ||
        frame_rate_.numerator <= 0 || frame_rate_.denominator <= 0) {
        updateControls();
        return;
    }

    if (current_frame_ >= playback_end_frame_exclusive_ - 1) {
        setPlayheadFrame(0);
    }
    playback_start_frame_ = current_frame_;
    playback_clock_.start();
    const auto frame_interval_ms = static_cast<int>(std::clamp(
        std::ceil(1000.0L * static_cast<long double>(frame_rate_.denominator) /
                  static_cast<long double>(frame_rate_.numerator)),
        1.0L, static_cast<long double>(std::numeric_limits<int>::max())));
    playing_ = true;
    playback_timer_->start(frame_interval_ms);
    updateControls();
}

void TimelineNavigator::pausePlayback(bool update_to_clock)
{
    if (!playing_) return;

    const auto frame_before_pause = current_frame_;
    std::int64_t target_frame = current_frame_;
    if (update_to_clock && playback_end_frame_exclusive_ > 0) {
        const auto elapsed_frames = detail::framesElapsedForNanoseconds(
            playback_clock_.nsecsElapsed(),
            frame_rate_.numerator,
            frame_rate_.denominator);
        if (loop_enabled_) {
            target_frame = detail::loopFrameForElapsed(
                playback_start_frame_, elapsed_frames, playback_end_frame_exclusive_);
        } else {
            const auto frames_until_end = playback_end_frame_exclusive_ - playback_start_frame_;
            target_frame = elapsed_frames >= frames_until_end
                ? playback_end_frame_exclusive_ - 1
                : playback_start_frame_ + elapsed_frames;
        }
    }

    playing_ = false;
    playback_timer_->stop();
    setPlayheadFrame(target_frame);
    if (current_frame_ == frame_before_pause) {
        // The active playback decode may still be in flight and will be
        // discarded once playback stops. Request the frozen frame again even
        // when the timer did not advance the playhead since its last tick.
        emit currentFrameChanged(static_cast<qint64>(current_frame_));
    }
    updateControls();
}

void TimelineNavigator::playbackTick()
{
    if (!playing_ || playback_end_frame_exclusive_ <= 0) return;
    const auto elapsed_frames = detail::framesElapsedForNanoseconds(
        playback_clock_.nsecsElapsed(),
        frame_rate_.numerator,
        frame_rate_.denominator);

    std::int64_t target_frame = 0;
    bool reached_end = false;
    if (loop_enabled_) {
        target_frame = detail::loopFrameForElapsed(
            playback_start_frame_, elapsed_frames, playback_end_frame_exclusive_);
    } else {
        const auto frames_until_end = playback_end_frame_exclusive_ - playback_start_frame_;
        reached_end = elapsed_frames >= frames_until_end;
        target_frame = reached_end
            ? playback_end_frame_exclusive_ - 1
            : playback_start_frame_ + elapsed_frames;
    }

    const auto frame_before_tick = current_frame_;
    if (reached_end) {
        playing_ = false;
        playback_timer_->stop();
    }
    setPlayheadFrame(target_frame);
    if (reached_end && current_frame_ == frame_before_tick) {
        emit currentFrameChanged(static_cast<qint64>(current_frame_));
    }
    updateControls();
}

void TimelineNavigator::setPlayheadFrame(std::int64_t frame)
{
    const auto bounded_frame = std::max<std::int64_t>(0, frame);
    ensureFrameInNavigationRange(bounded_frame);
    if (bounded_frame == current_frame_) {
        updateViewWidgets();
        updateControls();
        return;
    }

    current_frame_ = bounded_frame;
    ensureCurrentFrameVisible();
    updateViewWidgets();
    updateControls();
    emit currentFrameChanged(static_cast<qint64>(current_frame_));
}

void TimelineNavigator::ensureFrameInNavigationRange(std::int64_t frame)
{
    const auto extended_end = detail::extendRangeEndToInclude(
        visible_end_frame_, frame, framesPerHour(frame_rate_));
    if (extended_end == visible_end_frame_) return;
    visible_end_frame_ = extended_end;
    updateHorizontalScrollBar();
}

void TimelineNavigator::extendViewByOneHour() noexcept
{
    if (playing_) pausePlayback(false);
    const auto extended_end = detail::saturatingFrameAdd(
        visible_end_frame_, framesPerHour(frame_rate_));
    if (extended_end == visible_end_frame_) {
        return;
    }

    visible_end_frame_ = extended_end;
    current_frame_ = visible_end_frame_;
    updateHorizontalScrollBar();
    setViewStartFrame(maximumViewStartFrame());
    updateViewWidgets();
    updateControls();
    emit currentFrameChanged(static_cast<qint64>(current_frame_));
}

void TimelineNavigator::applyZoomLevel(int index)
{
    const int last_index = static_cast<int>(detail::kTimelineZoomLevels.size()) - 1;
    const int bounded_index = std::clamp(index, 0, last_index);
    const auto next_factor = detail::kTimelineZoomLevels[
        static_cast<std::size_t>(bounded_index)];
    if (bounded_index == zoom_level_index_ && next_factor == zoom_factor_) {
        updateControls();
        return;
    }

    const TimelineViewMapping old_mapping{
        ruler_->mappingWidth(), kTimelineHeaderWidth, visible_end_frame_,
        view_start_frame_, frames_per_view_};
    const bool playhead_visible = current_frame_ >= view_start_frame_ &&
        current_frame_ <= old_mapping.viewEndFrame();
    const int anchor_x = playhead_visible
        ? old_mapping.xForFrame(current_frame_)
        : old_mapping.axisLeft() + old_mapping.axisWidth() / 2;
    const int axis_width = std::max(1, old_mapping.axisWidth());
    const long double anchor_fraction = std::clamp(
        static_cast<long double>(anchor_x - old_mapping.axisLeft()) /
            static_cast<long double>(axis_width),
        0.0L, 1.0L);

    zoom_level_index_ = bounded_index;
    zoom_factor_ = next_factor;
    frames_per_view_ = calculateFramesPerView();
    const long double frame_offset = anchor_fraction *
        static_cast<long double>(std::max<std::int64_t>(0, frames_per_view_ - 1));
    const long double requested_start =
        static_cast<long double>(current_frame_) - frame_offset;
    const auto maximum_start = maximumViewStartFrame();
    const auto new_start = roundedFrameClamped(
        std::clamp(requested_start, 0.0L, static_cast<long double>(maximum_start)),
        maximum_start);
    updateHorizontalScrollBar();
    setViewStartFrame(new_start);
    updateControls();
}

void TimelineNavigator::updateHorizontalScrollBar()
{
    if (horizontal_scroll_bar_ == nullptr) {
        return;
    }
    const auto maximum_start = maximumViewStartFrame();
    const long double total_frames = static_cast<long double>(visible_end_frame_) + 1.0L;
    const long double page_fraction = total_frames > 0.0L
        ? static_cast<long double>(frames_per_view_) / total_frames : 1.0L;
    const int page_step = std::clamp(static_cast<int>(std::llround(
        page_fraction * detail::kTimelineScrollResolution)),
        1, detail::kTimelineScrollResolution);
    const int value = maximum_start == 0 ? 0 : static_cast<int>(std::llround(
        static_cast<long double>(std::clamp(view_start_frame_, std::int64_t{0}, maximum_start)) /
        static_cast<long double>(maximum_start) * detail::kTimelineScrollResolution));
    const QSignalBlocker blocker(horizontal_scroll_bar_);
    horizontal_scroll_bar_->setRange(0, detail::kTimelineScrollResolution);
    horizontal_scroll_bar_->setPageStep(page_step);
    horizontal_scroll_bar_->setSingleStep(std::max(1, page_step / 10));
    horizontal_scroll_bar_->setEnabled(maximum_start > 0);
    horizontal_scroll_bar_->setValue(value);
    const long double fraction = static_cast<long double>(value) /
        detail::kTimelineScrollResolution;
    view_start_frame_ = roundedFrameClamped(
        fraction * static_cast<long double>(maximum_start), maximum_start);
}

void TimelineNavigator::setViewStartFrame(std::int64_t frame)
{
    const auto maximum_start = maximumViewStartFrame();
    const auto bounded = std::clamp(frame, std::int64_t{0}, maximum_start);
    const int value = maximum_start == 0 ? 0 : static_cast<int>(std::llround(
        static_cast<long double>(bounded) / static_cast<long double>(maximum_start) *
        detail::kTimelineScrollResolution));
    const QSignalBlocker blocker(horizontal_scroll_bar_);
    horizontal_scroll_bar_->setValue(value);
    const long double fraction = static_cast<long double>(value) /
        detail::kTimelineScrollResolution;
    view_start_frame_ = roundedFrameClamped(
        fraction * static_cast<long double>(maximum_start), maximum_start);
    updateViewWidgets();
}

void TimelineNavigator::updateViewWidgets()
{
    if (ruler_ == nullptr || layer_rows_ == nullptr) {
        return;
    }
    ruler_->setViewState(
        visible_end_frame_, current_frame_, view_start_frame_, frames_per_view_,
        frame_rate_, display_mode_);
    static_cast<LayerRowsWidget*>(layer_rows_)->setViewState(
        visible_end_frame_, current_frame_, view_start_frame_, frames_per_view_);
}

void TimelineNavigator::ensureCurrentFrameVisible()
{
    const TimelineViewMapping mapping{
        ruler_->mappingWidth(), kTimelineHeaderWidth, visible_end_frame_,
        view_start_frame_, frames_per_view_};
    if (current_frame_ >= view_start_frame_ && current_frame_ <= mapping.viewEndFrame()) {
        return;
    }
    const auto centered_offset = std::max<std::int64_t>(0, (frames_per_view_ - 1) / 2);
    const auto desired_start = current_frame_ > centered_offset
        ? current_frame_ - centered_offset : 0;
    setViewStartFrame(std::min(desired_start, maximumViewStartFrame()));
}

std::int64_t TimelineNavigator::maximumViewStartFrame() const noexcept
{
    const auto last_visible_offset = std::max<std::int64_t>(0, frames_per_view_ - 1);
    return visible_end_frame_ >= last_visible_offset
        ? visible_end_frame_ - last_visible_offset : 0;
}

std::int64_t TimelineNavigator::calculateFramesPerView() const noexcept
{
    const long double raw = std::ceil(
        static_cast<long double>(framesPerHour(frame_rate_)) /
        static_cast<long double>(zoom_factor_));
    return std::max<std::int64_t>(1,
        raw >= static_cast<long double>(kMaximumFrame)
            ? kMaximumFrame : static_cast<std::int64_t>(raw));
}

void TimelineNavigator::updateControls()
{
    frame_label_->setText(display_mode_ == TimelineDisplayMode::Frames
        ? QStringLiteral("Frame %1").arg(static_cast<qlonglong>(current_frame_))
        : formatTimelinePosition(current_frame_, frame_rate_, display_mode_));
    frame_rate_label_->setText(QStringLiteral("FPS: %1").arg(formatFrameRate(frame_rate_)));
    previous_frame_button_->setEnabled(current_frame_ > 0);
    next_frame_button_->setEnabled(current_frame_ < visible_end_frame_);
    play_pause_button_->setText(playing_ ? QStringLiteral("Pause") : QStringLiteral("Play"));
    play_pause_button_->setEnabled(playing_ || playback_end_frame_exclusive_ > 0);
    loop_button_->setEnabled(playing_ || playback_end_frame_exclusive_ > 0);
    zoom_out_button_->setEnabled(zoom_level_index_ > 0);
    zoom_in_button_->setEnabled(
        zoom_level_index_ < static_cast<int>(detail::kTimelineZoomLevels.size()) - 1);
    zoom_level_label_->setText(QStringLiteral("%1%")
        .arg(static_cast<int>(std::lround(zoom_factor_ * 100.0))));
    const QSignalBlocker blocker(zoom_slider_);
    zoom_slider_->setValue(zoom_level_index_);
    if (previous_frame_action_ != nullptr) {
        previous_frame_action_->setEnabled(previous_frame_button_->isEnabled());
    }
    if (next_frame_action_ != nullptr) {
        next_frame_action_->setEnabled(next_frame_button_->isEnabled());
    }
    if (play_pause_action_ != nullptr) {
        play_pause_action_->setEnabled(play_pause_button_->isEnabled());
    }
    if (loop_action_ != nullptr) {
        loop_action_->setEnabled(loop_button_->isEnabled());
        const QSignalBlocker loop_blocker(loop_action_);
        loop_action_->setChecked(loop_enabled_);
    }
    if (zoom_in_action_ != nullptr) {
        zoom_in_action_->setEnabled(zoom_in_button_->isEnabled());
    }
    if (zoom_out_action_ != nullptr) {
        zoom_out_action_->setEnabled(zoom_out_button_->isEnabled());
    }
}

} // namespace motion::ui
