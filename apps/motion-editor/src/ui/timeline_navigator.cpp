#include "timeline_navigator.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
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

struct LayerRow {
    model::LayerId id = 0;
    model::LayerKind kind = model::LayerKind::Image;
    QString name;
    bool visible = true;
    std::int64_t start_frame = 0;
    std::int64_t duration_frames = 0;
    std::int64_t maximum_duration_frames = 0;
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
        setMinimumHeight(std::max(kTimelineRowHeight,
            static_cast<int>(rows_.size()) * kTimelineRowHeight));
        resize(width(), std::max(kTimelineRowHeight,
            static_cast<int>(rows_.size()) * kTimelineRowHeight));
        updateGeometry();
        update();
    }

    void setRange(std::int64_t end_frame, std::int64_t current_frame)
    {
        visible_end_frame_ = std::max<std::int64_t>(0, end_frame);
        current_frame_ = std::clamp(current_frame, std::int64_t{0}, visible_end_frame_);
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

protected:
    QSize sizeHint() const override
    {
        return {520, std::max(kTimelineRowHeight,
            static_cast<int>(rows_.size()) * kTimelineRowHeight)};
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

        for (std::size_t index = 0; index < rows_.size(); ++index) {
            const auto& row = rows_[index];
            const int top = static_cast<int>(index) * kTimelineRowHeight;
            const QRect row_rect(0, top, width(), kTimelineRowHeight);
            const bool selected = row.id == selected_layer_id_;
            painter.fillRect(row_rect,
                selected ? QColor(64, 69, 78)
                         : (index % 2 == 0 ? QColor(39, 43, 50) : QColor(35, 39, 46)));
            painter.fillRect(QRect(0, top, kTimelineHeaderWidth, kTimelineRowHeight),
                             QColor(43, 47, 54));
            painter.setPen(QPen(QColor(63, 68, 76), 1));
            painter.drawLine(0, top + kTimelineRowHeight - 1,
                             width(), top + kTimelineRowHeight - 1);

            const QPoint eye_center(15, top + kTimelineRowHeight / 2);
            painter.setBrush(row.visible ? QColor(210, 215, 222) : Qt::NoBrush);
            painter.setPen(QPen(row.visible ? QColor(210, 215, 222) : QColor(130, 136, 144), 1.5));
            painter.drawEllipse(eye_center, 5, 4);
            if (row.visible) {
                painter.setPen(QPen(QColor(43, 47, 54), 1));
                painter.drawPoint(eye_center);
            }
            painter.setPen(QColor(228, 231, 235));
            painter.drawText(QRect(29, top, kTimelineHeaderWidth - 35, kTimelineRowHeight),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             row.name.isEmpty() ? QStringLiteral("Media Layer") : row.name);

            const int clip_left = xForFrame(row.start_frame);
            const auto end_frame = row.start_frame > kMaximumFrame - row.duration_frames
                ? kMaximumFrame : row.start_frame + row.duration_frames;
            const int clip_right = std::max(clip_left + 2, xForFrame(end_frame));
            const QRect clip_rect(clip_left, top + 5,
                                  std::max(2, clip_right - clip_left),
                                  kTimelineRowHeight - 10);
            QColor clip_color = row.kind == model::LayerKind::Image
                ? QColor(54, 115, 160) : QColor(57, 132, 101);
            if (!row.visible) clip_color = clip_color.darker(190);
            painter.setPen(QPen(selected ? QColor(255, 183, 54) : clip_color.lighter(125),
                                selected ? 2 : 1));
            painter.setBrush(clip_color);
            painter.drawRoundedRect(clip_rect, 3, 3);
            painter.setPen(QColor(245, 247, 250));
            painter.drawText(clip_rect.adjusted(6, 0, -8, 0),
                             Qt::AlignLeft | Qt::AlignVCenter, row.name);

            if (interaction_layer_id_ == row.id && interaction_mode_ != InteractionMode::None) {
                const auto preview_start = interaction_mode_ == InteractionMode::Move
                    ? preview_value_ : row.start_frame;
                const auto preview_duration = interaction_mode_ == InteractionMode::Resize
                    ? preview_value_ : row.duration_frames;
                const int preview_left = xForFrame(preview_start);
                const int preview_right = std::max(
                    preview_left + 2, xForFrame(preview_start + preview_duration));
                painter.setPen(QPen(QColor(255, 183, 54), 2, Qt::DashLine));
                painter.setBrush(QColor(255, 183, 54, 55));
                painter.drawRoundedRect(QRect(preview_left, top + 3,
                    std::max(2, preview_right - preview_left), kTimelineRowHeight - 6), 3, 3);
            }
        }

        const int playhead_x = xForFrame(current_frame_);
        painter.setPen(QPen(QColor(255, 183, 54), 1));
        painter.drawLine(playhead_x, 0, playhead_x, height());

        if (drag_preview_active_) {
            const int row = rowAtY(drag_preview_y_);
            const int top = row >= 0 ? row * kTimelineRowHeight : 0;
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
        const int row_index = rowAtY(event->position().toPoint().y());
        if (row_index < 0) return;
        const auto& row = rows_[static_cast<std::size_t>(row_index)];
        if (event->position().toPoint().x() < 29) {
            if (layer_visibility) layer_visibility(row.id, !row.visible);
            event->accept();
            return;
        }
        if (layer_selected) layer_selected(row.id);
        interaction_layer_id_ = row.id;
        press_position_ = event->position().toPoint();
        if (press_position_.x() < kTimelineHeaderWidth) {
            interaction_mode_ = InteractionMode::Reorder;
            target_row_ = row_index;
        } else {
            const int right = xForFrame(row.start_frame + row.duration_frames);
            const int left = xForFrame(row.start_frame);
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
                target_row_ = std::clamp(rowAtY(y), 0,
                    static_cast<int>(rows_.size()) - 1);
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
            if (row_index >= 0 && x >= kTimelineHeaderWidth) {
                const auto& row = rows_[static_cast<std::size_t>(row_index)];
                const int left = xForFrame(row.start_frame);
                const int right = xForFrame(row.start_frame + row.duration_frames);
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
        }
        event->accept();
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
            drag_preview_x_ = xForFrame(snappedFrame(frameAtX(x), 0));
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
        drag_preview_x_ = xForFrame(frame);
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
            ? rows_[static_cast<std::size_t>(row_index)].id : model::LayerId{0};
        const auto path = pathFromQString(QString::fromUtf8(
            event->mimeData()->data(kMotionMediaMimeType)));
        drag_preview_active_ = false;
        update();
        media_drop(path, frame, before);
        event->acceptProposedAction();
    }

private:
    enum class InteractionMode { None, Move, Resize, Reorder };

    [[nodiscard]] int rowAtY(int y) const noexcept
    {
        if (y < 0) return -1;
        const auto index = y / kTimelineRowHeight;
        return index >= 0 && static_cast<std::size_t>(index) < rows_.size() ? index : -1;
    }

    [[nodiscard]] const LayerRow* findRow(model::LayerId id) const noexcept
    {
        const auto found = std::find_if(rows_.begin(), rows_.end(), [id](const LayerRow& row) {
            return row.id == id;
        });
        return found == rows_.end() ? nullptr : &*found;
    }

    [[nodiscard]] int axisLeft() const noexcept
    {
        return rulerAxisLeft(width(), kTimelineHeaderWidth);
    }

    [[nodiscard]] int axisRight() const noexcept
    {
        return rulerAxisRight(width());
    }

    [[nodiscard]] std::int64_t frameAtX(int x) const noexcept
    {
        if (visible_end_frame_ <= 0) return 0;
        const int left = axisLeft();
        const int right = axisRight();
        const int span = right - left;
        if (span <= 0 || x <= left) return 0;
        if (x >= right) return visible_end_frame_;
        const long double fraction = static_cast<long double>(x - left) / span;
        const long double result = std::floor(
            fraction * static_cast<long double>(visible_end_frame_) + 0.5L);
        return std::clamp<std::int64_t>(
            static_cast<std::int64_t>(result), 0, visible_end_frame_);
    }

    [[nodiscard]] int xForFrame(std::int64_t frame) const noexcept
    {
        const int left = axisLeft();
        const int span = std::max(0, axisRight() - left);
        if (span == 0 || visible_end_frame_ == 0) return left;
        const auto bounded = std::clamp(frame, std::int64_t{0}, visible_end_frame_);
        const long double fraction = static_cast<long double>(bounded) /
            static_cast<long double>(visible_end_frame_);
        return left + static_cast<int>(std::llround(fraction * span));
    }

    [[nodiscard]] std::int64_t snappedFrame(
        std::int64_t frame,
        model::LayerId excluded) const noexcept
    {
        frame = std::clamp(frame, std::int64_t{0}, visible_end_frame_);
        const int span = std::max(1, axisRight() - axisLeft());
        const auto threshold = std::max<std::int64_t>(1,
            static_cast<std::int64_t>(std::ceil(
                static_cast<long double>(kTimelineSnapTolerancePixels) *
                static_cast<long double>(visible_end_frame_) / span)));
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
    std::int64_t visible_end_frame_ = 0;
    std::int64_t current_frame_ = 0;
    model::LayerId selected_layer_id_ = 0;
    InteractionMode interaction_mode_ = InteractionMode::None;
    model::LayerId interaction_layer_id_ = 0;
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

void TimelineRuler::setVisibleEndFrame(std::int64_t frame)
{
    visible_end_frame_ = std::max<std::int64_t>(0, frame);
    update();
}

void TimelineRuler::setHeaderWidth(int width)
{
    header_width_ = std::max(0, width);
    update();
}

void TimelineRuler::setCurrentFrame(std::int64_t frame) noexcept
{
    current_frame_ = std::max<std::int64_t>(0, frame);
    update();
}

void TimelineRuler::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(35, 39, 46));
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int axis_left = rulerAxisLeft(width(), header_width_);
    const int axis_right = std::max(axis_left, rulerAxisRight(width()));
    const int axis_y = 48;
    const int label_y = 17;
    const int axis_width = std::max(0, axis_right - axis_left);
    const auto last_frame = visible_end_frame_;

    painter.setPen(QPen(QColor(110, 118, 129), 1));
    painter.drawLine(axis_left, axis_y, axis_right, axis_y);

    const int label_digits = QString::number(static_cast<qlonglong>(last_frame)).size();
    const int minimum_tick_spacing = std::max(112, label_digits * 8 + 32);
    const int divisions = last_frame == 0 || axis_width <= 0
        ? 0
        : std::clamp(axis_width / minimum_tick_spacing, 1, 8);
    for (int division = 0; division <= divisions; ++division) {
        const long double fraction = divisions == 0
            ? 0.0L
            : static_cast<long double>(division) / static_cast<long double>(divisions);
        const int x = axis_left + static_cast<int>(std::llround(fraction * axis_width));
        const auto frame = frameAtX(x);
        painter.drawLine(x, axis_y - 5, x, axis_y + 5);
        painter.setPen(QColor(196, 201, 208));
        painter.drawText(
            QRect(x - 48, label_y, 96, 18),
            Qt::AlignHCenter | Qt::AlignVCenter,
            QString::number(static_cast<qlonglong>(frame)));
        painter.setPen(QPen(QColor(110, 118, 129), 1));
    }

    const int playhead_x = xForFrame(current_frame_);
    painter.setPen(QPen(QColor(255, 183, 54), 2));
    painter.drawLine(playhead_x, 10, playhead_x, height() - 8);
    painter.setBrush(QColor(255, 183, 54));
    painter.setPen(Qt::NoPen);
    QPolygon marker;
    marker << QPoint(playhead_x - 6, 8) << QPoint(playhead_x + 6, 8)
           << QPoint(playhead_x, 17);
    painter.drawPolygon(marker);
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
    if (mouse_x >= width()) {
        if (!range_extended_during_drag_) {
            range_extended_during_drag_ = true;
            emit extendRangeRequested();
            emit seekRequested(static_cast<qint64>(frameAtX(mouse_x)));
        }
    } else {
        emit seekRequested(static_cast<qint64>(frameAtX(mouse_x)));
    }
    event->accept();
}

void TimelineRuler::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        const int mouse_x = event->position().toPoint().x();
        if (mouse_x != last_mouse_x_) {
            last_mouse_x_ = mouse_x;
            if (mouse_x >= width()) {
                if (!range_extended_during_drag_) {
                    range_extended_during_drag_ = true;
                    emit extendRangeRequested();
                    emit seekRequested(static_cast<qint64>(frameAtX(mouse_x)));
                }
            } else {
                emit seekRequested(static_cast<qint64>(frameAtX(mouse_x)));
            }
        }
        last_mouse_x_ = -1;
        range_extended_during_drag_ = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

std::int64_t TimelineRuler::frameAtX(int x) const noexcept
{
    const auto last_frame = visible_end_frame_;
    if (last_frame == 0) {
        return 0;
    }

    const int axis_left = rulerAxisLeft(width(), header_width_);
    const int axis_right = std::max(axis_left, rulerAxisRight(width()));
    const int axis_width = axis_right - axis_left;
    if (axis_width <= 0 || x <= axis_left) {
        return 0;
    }
    if (x >= axis_right) {
        return last_frame;
    }

    const long double fraction = static_cast<long double>(x - axis_left)
        / static_cast<long double>(axis_width);
    const long double rounded = std::floor(
        fraction * static_cast<long double>(last_frame) + 0.5L);
    if (rounded >= static_cast<long double>(last_frame)) {
        return last_frame;
    }
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(rounded));
}

int TimelineRuler::xForFrame(std::int64_t frame) const noexcept
{
    const int axis_left = rulerAxisLeft(width(), header_width_);
    const int axis_right = std::max(axis_left, rulerAxisRight(width()));
    const int axis_width = axis_right - axis_left;
    const auto last_frame = visible_end_frame_;
    if (axis_width <= 0 || last_frame == 0) {
        return axis_left;
    }

    const long double fraction = static_cast<long double>(frame)
        / static_cast<long double>(last_frame);
    return axis_left + static_cast<int>(std::llround(fraction * axis_width));
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
    frame_label_ = new QLabel(this);
    frame_label_->setObjectName(QStringLiteral("motion-timeline-frame-readout"));
    frame_rate_label_ = new QLabel(this);
    frame_rate_label_->setObjectName(QStringLiteral("motion-timeline-frame-rate"));
    next_frame_button_ = new QPushButton(QStringLiteral("Next frame"), this);
    next_frame_button_->setObjectName(QStringLiteral("motion-timeline-next-frame"));
    controls->addWidget(previous_frame_button_);
    controls->addWidget(frame_label_);
    controls->addStretch(1);
    controls->addWidget(frame_rate_label_);
    controls->addWidget(next_frame_button_);
    layout->addLayout(controls);

    ruler_ = new TimelineRuler(this);
    ruler_->setHeaderWidth(kTimelineHeaderWidth);
    ruler_->setToolTip(QStringLiteral(
        "Starts with one hour. Drag beyond the right edge to extend by one hour per drag. "
        "This range is not the composition end."));
    layout->addWidget(ruler_);

    layer_scroll_area_ = new QScrollArea(this);
    layer_scroll_area_->setObjectName(QStringLiteral("motion-timeline-layer-scroll"));
    layer_scroll_area_->setWidgetResizable(true);
    layer_scroll_area_->setFrameShape(QFrame::NoFrame);
    layer_scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layer_rows_ = new LayerRowsWidget(layer_scroll_area_);
    layer_scroll_area_->setWidget(layer_rows_);
    layout->addWidget(layer_scroll_area_, 1);

    connect(previous_frame_button_, &QPushButton::clicked, this, [this] {
        seekToFrame(current_frame_ - (current_frame_ > 0 ? 1 : 0));
    });
    connect(next_frame_button_, &QPushButton::clicked, this, [this] {
        if (current_frame_ < visible_end_frame_) {
            seekToFrame(current_frame_ + 1);
        }
    });
    connect(ruler_, &TimelineRuler::seekRequested, this, [this](qint64 frame) {
        seekToFrame(static_cast<std::int64_t>(frame));
    });
    connect(ruler_, &TimelineRuler::extendRangeRequested, this, [this] {
        extendViewByOneHour();
    });

    updateControls();
}

void TimelineNavigator::setCompositionTiming(
    model::FrameRate frame_rate)
{
    frame_rate_ = frame_rate;
    current_frame_ = 0;
    visible_end_frame_ = initialVisibleEndFrame(frame_rate);
    ruler_->setVisibleEndFrame(visible_end_frame_);
    ruler_->setCurrentFrame(current_frame_);
    static_cast<LayerRowsWidget*>(layer_rows_)->setRange(visible_end_frame_, current_frame_);
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

std::int64_t TimelineNavigator::visibleEndFrame() const noexcept
{
    return visible_end_frame_;
}

void TimelineNavigator::setLayers(const std::vector<model::CompositionLayer>& layers)
{
    std::vector<LayerRow> rows;
    rows.reserve(layers.size());
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        rows.push_back(LayerRow{
            layer->id,
            layer->kind,
            utf8Text(layer->name),
            layer->visible,
            layer->timeline_start_frame,
            layer->duration_frames,
            layer->maximum_timeline_duration_frames});
    }
    auto* row_widget = static_cast<LayerRowsWidget*>(layer_rows_);
    row_widget->setRows(std::move(rows));
    row_widget->setRange(visible_end_frame_, current_frame_);
}

void TimelineNavigator::setSelectedLayerId(model::LayerId id)
{
    static_cast<LayerRowsWidget*>(layer_rows_)->setSelectedLayerId(id);
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

void TimelineNavigator::seekToFrame(std::int64_t frame)
{
    const auto bounded_frame = std::clamp(frame, std::int64_t{0}, visible_end_frame_);
    if (bounded_frame == current_frame_) {
        return;
    }

    current_frame_ = bounded_frame;
    ruler_->setCurrentFrame(current_frame_);
    static_cast<LayerRowsWidget*>(layer_rows_)->setRange(visible_end_frame_, current_frame_);
    updateControls();
    emit currentFrameChanged(static_cast<qint64>(current_frame_));
}

void TimelineNavigator::extendViewByOneHour() noexcept
{
    const auto remaining_frames = kMaximumFrame - visible_end_frame_;
    const auto extension_frames = std::min(framesPerHour(frame_rate_), remaining_frames);
    if (extension_frames == 0) {
        return;
    }

    visible_end_frame_ += extension_frames;
    ruler_->setVisibleEndFrame(visible_end_frame_);
    static_cast<LayerRowsWidget*>(layer_rows_)->setRange(visible_end_frame_, current_frame_);
    updateControls();
}

void TimelineNavigator::updateControls()
{
    frame_label_->setText(QStringLiteral("Frame %1")
        .arg(static_cast<qlonglong>(current_frame_)));
    frame_rate_label_->setText(QStringLiteral("FPS: %1").arg(formatFrameRate(frame_rate_)));
    previous_frame_button_->setEnabled(current_frame_ > 0);
    next_frame_button_->setEnabled(current_frame_ < visible_end_frame_);
}

} // namespace motion::ui
