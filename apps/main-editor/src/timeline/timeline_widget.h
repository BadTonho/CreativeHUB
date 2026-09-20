#pragma once

#include "timeline_model.h"

#include <QString>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class QMouseEvent;
class QPaintEvent;
class QPointF;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;

namespace timeline {

class TimelineWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setClips(const std::vector<TimelineClip>& clips);
    void clearClips();
    void setActiveClipIndex(std::optional<std::size_t> clip_index);
    void setPlayheadFrame(std::int64_t frame_index);
    void setRazorMode(bool enabled);
    [[nodiscard]] bool razorMode() const noexcept;

signals:
    void clipSelected(qint64 clip_index);
    void clipMoveRequested(qint64 from_index, qint64 to_index);
    void clipSplitRequested(qint64 clip_index, qint64 local_frame);
    void seekStarted();
    void seekRequested(qint64 frame_index);
    void mediaDropRequested(const QString& source_path);

protected:
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] bool isTrackPosition(const QPointF& position) const noexcept;
    [[nodiscard]] std::optional<std::size_t> activeClipIndex() const noexcept;
    [[nodiscard]] std::optional<std::size_t> clipIndexAtPosition(double x) const noexcept;
    [[nodiscard]] std::optional<std::size_t> insertionBoundaryAtPosition(
        double x) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> frameAtPosition(
        std::size_t clip_index,
        double x) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> frameAtPosition(double x) const noexcept;
    [[nodiscard]] std::optional<double> playheadFraction() const noexcept;
    [[nodiscard]] double displayedPlayheadFrame() const noexcept;

    std::vector<TimelineClip> clips_;
    std::optional<std::size_t> active_clip_index_;
    std::int64_t playhead_frame_ = 0;
    std::optional<std::int64_t> drag_frame_;
    bool dragging_ = false;
    bool moving_clip_ = false;
    std::size_t moving_clip_index_ = 0;
    std::optional<std::size_t> move_target_index_;
    bool razor_mode_ = false;
    bool razor_clicking_ = false;
    bool razor_gesture_moved_ = false;
    std::size_t razor_clip_index_ = 0;
    std::int64_t razor_frame_ = 0;
    double razor_press_x_ = 0.0;
    double razor_press_y_ = 0.0;
    bool drag_hovering_ = false;
};

} // namespace timeline
