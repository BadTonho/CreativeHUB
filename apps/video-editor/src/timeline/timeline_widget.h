#pragma once

#include "timeline_model.h"
#include "timeline_geometry.h"
#include "timeline_drop_validation.h"
#include "timeline_interaction_painter.h"
#include "timeline_interaction_controller.h"
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
class QPainter;
class QContextMenuEvent;
class QWheelEvent;
class QPoint;
class QEvent;
class QObject;

namespace timeline {

class TimelineTrackHeaderOverlay;

class TimelineWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setTracks(const std::vector<TimelineTrack>& tracks);
    void setFrameRate(FrameRate frame_rate) noexcept;
    void setClips(const std::vector<TimelineClip>& clips);
    void clearClips();
    void setActiveClip(std::optional<ClipLocation> location);
    void setActiveClipIndex(std::optional<std::size_t> clip_index);
    void setPlayheadFrame(std::int64_t frame_index);
    [[nodiscard]] std::int64_t playheadFrame() const noexcept;
    [[nodiscard]] std::int64_t displayedPlayheadFrame() const noexcept;
    [[nodiscard]] QString playheadTimecode() const;
    void setRazorMode(bool enabled);
    [[nodiscard]] bool razorMode() const noexcept;
    void setMoveRequiresAlt(bool enabled);
    [[nodiscard]] bool moveRequiresAlt() const noexcept;
    void setReadOnly(bool read_only);
    [[nodiscard]] bool isReadOnly() const noexcept;
    void setSnapEnabled(bool enabled);
    [[nodiscard]] bool snapEnabled() const noexcept;
    // Rendering bridge used by the fixed header overlay hosted by the
    // Timeline scroll area's viewport.
    [[nodiscard]] int trackHeaderOverlayWidth() const noexcept;
    void paintTrackHeaderOverlay(QPainter& painter, int vertical_offset) const;
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
    void clipSelected(timeline::TrackId track_id, timeline::ClipId clip_id);
    void editImageClipRequested(timeline::ClipId clip_id);
    void clipSelectionCleared();
    void clipMoveRequested(
        timeline::ClipId clip_id,
        timeline::TrackId target_track_id,
        qint64 timeline_start_frame);
    void clipSplitRequested(timeline::ClipId clip_id, qint64 local_frame);
    void clipEdgeTrimRequested(
        timeline::ClipId clip_id,
        qint64 edge,
        qint64 boundary_frame,
        qint64 mode);
    void mediaDropRequested(
        const QString& source_path,
        timeline::TrackId track_id,
        qint64 timeline_frame);
    void effectDropRequested(
        const QString& effect_id,
        timeline::TrackId track_id,
        qint64 timeline_frame);
    void transitionSelected(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id);
    void transitionSelectionCleared();
    void transitionAddRequested(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id,
        qint64 kind);
    void transitionRemoveRequested(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id);
    void trimStarted();
    void seekStarted();
    void seekRequested(qint64 frame_index);
    void zoomRequested(double factor);
    void zoomChanged(double factor);
    void trackRowHeightChanged(double height);
    void snapEnabledChanged(bool enabled);
    void trackHeaderVisualsChanged();
    void playheadVisualChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    [[nodiscard]] QRectF trackRect(std::size_t index) const noexcept;
    [[nodiscard]] TimelineGeometry geometry() const noexcept;
    [[nodiscard]] QRectF rulerRect() const noexcept;
    [[nodiscard]] double rowHeight() const noexcept;
    [[nodiscard]] QRectF trackContentRect(std::size_t index) const noexcept;
    [[nodiscard]] QRectF clipRect(const ClipLocation& location) const noexcept;
    [[nodiscard]] const TimelineClip& displayedClip(
        const ClipLocation& location) const noexcept;
    void paintTrackHeaderCell(
        QPainter& painter,
        std::size_t track_index,
        const QRectF& row) const;
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
    [[nodiscard]] std::optional<ClipEdge> trimEdgeAt(const ClipLocation&, double x) const noexcept;
    [[nodiscard]] ClipEdgeEditMode trimEditModeAt(
        const ClipLocation&, ClipEdge, double x) const noexcept;
    void updateTrimHoverCursor(const QPointF& position);
    [[nodiscard]] std::optional<std::int64_t> trimBoundaryAt(double x) const noexcept;
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>>
    transitionClipIndexesAt(double x, double y) const noexcept;
    void showTransitionMenu(const QPoint& position, const QPoint& global_position);
    void showImageClipMenu(const ClipLocation& location, const QPoint& global_position);
    void emitSelected(const ClipLocation& location);
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
    [[nodiscard]] timeline::SnapPlacement snapPlacement(
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
    bool move_requires_alt_ = false;
    bool read_only_ = false;
    FrameRate frame_rate_;
    TimelineInteractionController interaction_controller_;
    bool razor_mode_ = false;
    QPointF move_preview_position_{};
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
