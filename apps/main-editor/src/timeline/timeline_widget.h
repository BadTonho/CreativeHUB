#pragma once

#include "timeline_model.h"
#include "timeline_layout.h"
#include "timeline_zoom.h"

#include <QString>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QMouseEvent;
class QPaintEvent;
class QContextMenuEvent;
class QWheelEvent;
class QPoint;
class QEvent;
class QObject;

namespace timeline {

class TimelineWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setTracks(const std::vector<TimelineTrack>& tracks);
    void setClips(const std::vector<TimelineClip>& clips);
    void clearClips();
    void setActiveClip(std::optional<ClipLocation> location);
    void setActiveClipIndex(std::optional<std::size_t> clip_index);
    void setPlayheadFrame(std::int64_t frame_index);
    [[nodiscard]] std::int64_t playheadFrame() const noexcept;
    void setRazorMode(bool enabled);
    [[nodiscard]] bool razorMode() const noexcept;
    void setMoveRequiresAlt(bool enabled);
    [[nodiscard]] bool moveRequiresAlt() const noexcept;
    void setSnapEnabled(bool enabled);
    [[nodiscard]] bool snapEnabled() const noexcept;
    void setTimelineViewportWidth(int width);
    [[nodiscard]] double zoomFactor() const noexcept;
    void setZoomFactor(double factor);
    [[nodiscard]] double trackRowHeight() const noexcept;
    void setTrackRowHeight(double height);
    [[nodiscard]] double nextZoomFactor(int direction) const noexcept;
    [[nodiscard]] bool canZoomIn() const noexcept;
    [[nodiscard]] bool canZoomOut() const noexcept;
    [[nodiscard]] std::optional<std::int64_t> frameAtContentX(double x) const noexcept;
    [[nodiscard]] double contentXForFrame(std::int64_t frame) const noexcept;
    [[nodiscard]] static QString formatTimecode(
        std::int64_t frame,
        double frame_rate);

signals:
    // Compatibility signals for the original first-track UI path.
    void clipSelected(qint64 clip_index);
    void clipMoveRequested(qint64 from_index, qint64 to_index);
    void clipSplitRequested(qint64 clip_index, qint64 local_frame);
    void clipTrimRequested(qint64 clip_index, qint64 local_start_frame, qint64 local_end_frame);
    void mediaDropRequested(const QString& source_path);

    void clipSelectedAt(qint64 track_index, qint64 clip_index);
    void clipMoveRequestedAt(qint64 from_track, qint64 from_clip, qint64 to_track, qint64 timeline_start_frame);
    void clipSplitRequestedAt(qint64 track_index, qint64 clip_index, qint64 local_frame);
    void clipTrimRequestedAt(qint64 track_index, qint64 clip_index, qint64 local_start_frame, qint64 local_end_frame);
    void mediaDropRequestedAt(const QString& source_path, qint64 track_index, qint64 timeline_frame);
    void effectDropRequestedAt(const QString& effect_id, qint64 track_index, qint64 timeline_frame);
    void transitionSelectedAt(qint64 track_index, qint64 from_clip_index, qint64 to_clip_index);
    void transitionAddRequestedAt(
        qint64 track_index,
        qint64 from_clip_index,
        qint64 to_clip_index,
        qint64 kind);
    void transitionRemoveRequestedAt(
        qint64 track_index,
        qint64 from_clip_index,
        qint64 to_clip_index);
    void trimStarted();
    void seekStarted();
    void seekRequested(qint64 frame_index);
    void zoomRequested(double factor);
    void zoomChanged(double factor);
    void trackRowHeightChanged(double height);
    void snapEnabledChanged(bool enabled);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class TrimEdge { Left, Right };
    struct SnapPlacement {
        std::int64_t start_frame = 0;
        std::optional<std::int64_t> guide_frame;
    };

    [[nodiscard]] QRectF trackRect(std::size_t index) const noexcept;
    [[nodiscard]] QRectF rulerRect() const noexcept;
    [[nodiscard]] double rowHeight() const noexcept;
    [[nodiscard]] QRectF trackContentRect(std::size_t index) const noexcept;
    [[nodiscard]] QRectF clipRect(const ClipLocation& location) const noexcept;
    [[nodiscard]] double frameRate() const noexcept;
    [[nodiscard]] std::int64_t standardDuration() const noexcept;
    [[nodiscard]] std::int64_t displayDuration() const noexcept;
    [[nodiscard]] std::int64_t totalDuration() const noexcept;
    [[nodiscard]] double pixelsPerFrame() const noexcept;
    void updateVerticalExtent();
    void updateHorizontalExtent();
    [[nodiscard]] std::optional<std::size_t> trackAt(double y) const noexcept;
    [[nodiscard]] std::optional<ClipLocation> clipAt(double x, double y) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> globalFrameAt(double x) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> playheadFrameAtRulerX(
        double x) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> localFrameAt(const ClipLocation&, double x) const noexcept;
    [[nodiscard]] std::optional<TrimEdge> trimEdgeAt(const ClipLocation&, double x) const noexcept;
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>>
    transitionClipIndexesAt(double x, double y) const noexcept;
    void showTransitionMenu(const QPoint& position, const QPoint& global_position);
    void emitSelected(const ClipLocation& location);
    void emitLegacySelection(const ClipLocation& location);
    [[nodiscard]] bool isSupportedDrop(
        const QMimeData* mime_data) const noexcept;
    [[nodiscard]] std::int64_t mediaDropDuration(
        const QMimeData* mime_data) const noexcept;
    [[nodiscard]] QString mediaDropLabel(
        const QMimeData* mime_data) const;
    [[nodiscard]] bool placementOverlaps(
        std::size_t track_index,
        std::int64_t start_frame,
        std::int64_t duration_frames,
        std::optional<ClipLocation> excluded = std::nullopt) const noexcept;
    [[nodiscard]] QRectF previewRect(
        std::size_t track_index,
        std::int64_t start_frame,
        std::int64_t duration_frames) const noexcept;
    [[nodiscard]] SnapPlacement snapPlacement(
        std::size_t track_index,
        std::int64_t raw_start_frame,
        std::int64_t duration_frames,
        std::optional<ClipLocation> excluded = std::nullopt) const noexcept;
    void clearDragPreview();
    void clearDropHover();
    [[nodiscard]] bool updateDropHover(
        const QMimeData* mime_data,
        const QPointF& position);
    [[nodiscard]] bool processDrop(
        const QMimeData* mime_data,
        const QPointF& position);

    std::vector<TimelineTrack> tracks_;
    int timeline_viewport_width_ = 0;
    double zoom_factor_ = 1.0;
    double track_row_height_ = kDefaultTrackRowHeight;
    bool snap_enabled_ = true;
    std::optional<ClipLocation> active_clip_;
    std::int64_t playhead_frame_ = 0;
    std::optional<std::int64_t> drag_frame_;
    std::optional<std::int64_t> ruler_frame_;
    std::optional<double> ruler_content_x_;
    bool dragging_ = false;
    bool ruler_seeking_ = false;
    bool move_requires_alt_ = false;
    bool move_pending_ = false;
    bool moving_active_ = false;
    ClipLocation moving_clip_{};
    QPointF move_press_position_{};
    std::optional<std::size_t> move_target_track_;
    std::int64_t move_target_frame_ = 0;
    bool trimming_ = false;
    ClipLocation trimming_clip_{};
    TrimEdge trim_edge_ = TrimEdge::Left;
    std::int64_t trim_start_frame_ = 0;
    std::int64_t trim_end_frame_ = 0;
    bool razor_mode_ = false;
    bool razor_clicking_ = false;
    bool razor_gesture_moved_ = false;
    ClipLocation razor_clip_{};
    std::int64_t razor_frame_ = 0;
    QPointF razor_press_position_{};
    bool seek_pending_ = false;
    QPointF seek_press_position_{};
    ClipLocation seek_clip_{};
    enum class DragPreviewKind { None, MediaDrop };
    DragPreviewKind drag_preview_kind_ = DragPreviewKind::None;
    QPointF drag_preview_position_{};
    std::int64_t drag_preview_duration_frames_ = 1;
    QString drag_preview_label_;
    bool drag_preview_valid_ = false;
    bool drag_hovering_ = false;
    std::optional<std::size_t> drop_hover_track_;
    std::optional<std::int64_t> drop_hover_frame_;
    std::optional<std::int64_t> snap_guide_frame_;
    struct SelectedTransition {
        std::size_t track_index = 0;
        std::size_t from_clip_index = 0;
        std::size_t to_clip_index = 0;

        friend bool operator==(const SelectedTransition&, const SelectedTransition&) = default;
    };
    std::optional<SelectedTransition> selected_transition_;
    bool suppress_next_context_menu_ = false;
};

} // namespace timeline
